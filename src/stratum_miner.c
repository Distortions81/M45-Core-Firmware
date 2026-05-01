#include "app_state.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>

#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "mbedtls/sha256.h"
#include "sdkconfig.h"
#include "build_info.h"

#if !CONFIG_IDF_TARGET_ESP32
#error "Hardware SHA-256 mining is only implemented for classic ESP32"
#endif
#include "sha/sha_parallel_engine.h"
#include "soc/dport_access.h"
#include "soc/dport_reg.h"
#include "soc/hwcrypto_reg.h"

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define STRATUM_STALE_WORK_CHECK_NONCES 65536U
#define STRATUM_HASH_PROGRESS_NONCES 131072U
#define STRATUM_SOFTWARE_STALE_WORK_CHECK_NONCES 256U

#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
#define STRATUM_MINER_MODEL "M45View"
#else
#define STRATUM_MINER_MODEL "M45Mini"
#endif
#define STRATUM_MINER_VERSION_CHARS 7

static void uint32_to_hex_be(uint32_t value, char* out, size_t out_size);
static void extra_nonce2_to_hex_le(
    const uint8_t extra_nonce2[16], uint8_t byte_count, char* out, size_t out_size);
static void share_queue_clear(void);
static void miner_clear_work(void);
static void miner_publish_current_job(bool preserve_nonce);
static void miner_reset_hashrate_sample(bool zero_visible_hashrate);
static void miner_wake_tasks(void);

static const char* TAG = "stratum";
static const char* DEFAULT_PRIMARY_IP = "74.80.183.165";
static const char* DEFAULT_BACKUP_IP = "54.215.229.72";

typedef struct {
  uint32_t nonce;
  uint8_t extra_nonce2[16];
  uint8_t extra_nonce2_size;
  char job_id[sizeof(((mining_job_t*)0)->job_id)];
  char ntime[sizeof(((mining_job_t*)0)->ntime)];
} found_share_t;

typedef struct {
  found_share_t* shares;
  size_t head;
  volatile size_t count;
  size_t capacity;
} share_queue_t;

typedef struct {
  bool active;
  uint32_t sequence;
  uint32_t midstate[8] __attribute__((aligned(16)));
  uint32_t tail_words[3] __attribute__((aligned(16)));
  uint32_t hw_block0[16] __attribute__((aligned(16)));
  uint32_t hw_block1[16] __attribute__((aligned(16)));
  uint32_t target_words[8] __attribute__((aligned(16)));
  uint32_t target_word0;
  uint32_t next_nonce;
  uint8_t extra_nonce2[16];
  uint8_t extra_nonce2_size;
  char block_hash[sizeof(g_stratum_runtime.current_block)];
  char job_id[sizeof(((mining_job_t*)0)->job_id)];
  char ntime[sizeof(((mining_job_t*)0)->ntime)];
} __attribute__((aligned(16))) miner_work_t;

typedef struct {
  uint32_t hashes;
  uint32_t candidates;
  bool queue_full;
  bool stale_work;
} miner_scan_result_t;

typedef enum {
  SHARE_QUEUE_PUSH_OK = 0,
  SHARE_QUEUE_PUSH_FULL,
  SHARE_QUEUE_PUSH_STALE,
} share_queue_push_result_t;

typedef enum {
  MINER_CANDIDATE_NONE = 0,
  MINER_CANDIDATE_PUSHED,
  MINER_CANDIDATE_QUEUE_FULL,
  MINER_CANDIDATE_STALE,
} miner_candidate_result_t;

static miner_scan_result_t miner_scan_hw_benchmark_range(
    const miner_work_t* work,
    uint32_t start_nonce,
    uint32_t nonce_count,
    uint32_t* checksum);

typedef struct {
  uint8_t index;
} miner_task_context_t;

typedef struct {
  uint8_t index;
  miner_work_t work;
  uint32_t hw_block0[16] __attribute__((aligned(16)));
  uint32_t hw_block1[16] __attribute__((aligned(16)));
  TaskHandle_t notify_task;
  uint32_t start_nonce;
  uint32_t nonce_count;
  uint64_t hashes;
  uint32_t candidates;
  uint32_t checksum;
} synthetic_task_context_t;

static found_share_t g_share_queue_storage[STRATUM_SHARE_QUEUE_MAX];
static share_queue_t g_share_queue = {
    .shares = g_share_queue_storage,
    .capacity = STRATUM_SHARE_QUEUE_MAX,
};
static SemaphoreHandle_t g_share_queue_mutex = NULL;
static portMUX_TYPE g_miner_work_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_miner_stats_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_synthetic_stats_mux = portMUX_INITIALIZER_UNLOCKED;
static miner_work_t g_miner_work __attribute__((aligned(16))) = {0};
static TaskHandle_t g_miner_task_handles[STRATUM_MINER_TASK_COUNT] = {0};
static miner_task_context_t g_miner_task_contexts[STRATUM_MINER_TASK_COUNT] = {0};
static TaskHandle_t g_software_miner_task_handle = NULL;
static miner_task_context_t g_software_miner_task_context = {0};
static TaskHandle_t g_synthetic_task_handles[STRATUM_MINER_TASK_COUNT] = {0};
static synthetic_task_context_t g_synthetic_task_contexts[STRATUM_MINER_TASK_COUNT] = {0};
static TaskHandle_t g_stratum_session_task_handle = NULL;
static volatile bool g_stratum_reconnect_requested = false;
static uint64_t g_hashrate_sample_total = 0;
static int64_t g_hashrate_sample_us = 0;
static uint32_t g_synthetic_hw_block0[16] = {0};
static uint32_t g_synthetic_hw_block1[16] = {0};
static volatile uint32_t g_benchmark_sink = 0;
static char g_stratum_line[STRATUM_LINE_MAX];
static char g_pending_notify_line[STRATUM_LINE_MAX];
static bool g_pending_notify_available = false;

static const DRAM_ATTR uint32_t SHA256_INITIAL_STATE[8] = {
    0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL,
    0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL};


/* Private implementation fragments share the static state above. */
#include "stratum_miner_text_io.inc"
#include "stratum_miner_sha.inc"
#include "stratum_miner_protocol.inc"
#include "stratum_miner_json_hex.inc"
#include "stratum_miner_payout.inc"
#include "stratum_miner_target.inc"
#include "stratum_miner_hw_sha.inc"
#include "stratum_miner_selfcheck.inc"
#include "stratum_miner_parse.inc"
#include "stratum_miner_share_queue.inc"
#include "stratum_miner_engine.inc"
#include "stratum_miner_tasks.inc"
#include "stratum_miner_synthetic.inc"
#include "stratum_miner_session.inc"
