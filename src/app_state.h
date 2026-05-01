#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

#define AP_SSID "m-idf"
#define AP_CHANNEL 6
#define AP_MAX_CLIENTS 4
#define AP_IP0 10
#define AP_IP1 45
#define AP_IP2 0
#define AP_IP3 1

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

#ifndef APP_OLED_SDA_GPIO
#define APP_OLED_SDA_GPIO 5
#endif

#ifndef APP_OLED_SCL_GPIO
#define APP_OLED_SCL_GPIO 4
#endif

#define OLED_SDA_GPIO APP_OLED_SDA_GPIO
#define OLED_SCL_GPIO APP_OLED_SCL_GPIO
#define OLED_RESET_GPIO 16
#define OLED_I2C_ADDRESS 0x3c

#ifndef APP_DISPLAY_IDEASPARK_ESP32_19_LCD
#define APP_DISPLAY_IDEASPARK_ESP32_19_LCD 0
#endif

#ifndef APP_SYNTHETIC_NO_WIFI_NO_OLED
#define APP_SYNTHETIC_NO_WIFI_NO_OLED 0
#endif

#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "This firmware targets the classic ESP32 hardware SHA device only"
#endif

#ifndef APP_ENABLE_SERIAL_LOGS
#define APP_ENABLE_SERIAL_LOGS 0
#endif

#if APP_ENABLE_SERIAL_LOGS
#define APP_SERIAL_LOGF(...) printf(__VA_ARGS__)
#else
#define APP_SERIAL_LOGF(...) ((void)0)
#endif

#ifndef APP_FACTORY_RESET_GPIO
#define APP_FACTORY_RESET_GPIO 0
#endif

#define FACTORY_RESET_GPIO APP_FACTORY_RESET_GPIO
#define FACTORY_RESET_HOLD_MS 5000
#define DISPLAY_SLEEP_MAX_MINUTES UINT16_MAX

#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_RECONNECT_IDLE_MS 30000
#define WIFI_RECONNECT_BASE_MS 1000
#define WIFI_RECONNECT_MAX_MS 30000
// Keep this roomy. Settings POSTs include wallet/pool fields and truncated
// form bodies can silently produce incomplete saves.
#define FORM_BODY_MAX 1536
#define STRATUM_LINE_MAX 4096
#define STRATUM_RECONNECT_MS 5000
#define STRATUM_IDLE_TIMEOUT_MS 120000
#define STRATUM_SOCKET_TIMEOUT_MS 5000
#define STRATUM_SOCKET_RX_BUFFER 512
#define APP_TASK_WDT_TIMEOUT_MS 30000
#ifndef APP_STRATUM_MINE_BATCH
// Keep batches below the hashrate sample window. Very large batches make
// hashes_total update in coarse jumps, which can under-report OLED/web hashrate.
#define APP_STRATUM_MINE_BATCH 262144
#endif
#ifndef APP_STRATUM_MINE_YIELD_BATCHES
#define APP_STRATUM_MINE_YIELD_BATCHES 8
#endif
#ifndef APP_STRATUM_MINE_CANDIDATE_GUARD_NONCES
// Extra nonce ownership for the unbounded hardware ASM candidate loop. This is
// not a scan limit and does not add instructions to the mining loop.
#define APP_STRATUM_MINE_CANDIDATE_GUARD_NONCES 1048576U
#endif
#ifndef APP_STRATUM_SOFTWARE_MINER_ENABLED
#define APP_STRATUM_SOFTWARE_MINER_ENABLED 1
#endif
#ifndef APP_STRATUM_SOFTWARE_MINE_BATCH
#define APP_STRATUM_SOFTWARE_MINE_BATCH 512
#endif
#ifndef APP_STRATUM_MIN_CANDIDATE_TARGET_WORD0
// Candidate gate for the hardware miner hot path. 0x0003ffff is a 14-bit
// leading-zero threshold, equivalent to minimum share difficulty 1/16384.
#define APP_STRATUM_MIN_CANDIDATE_TARGET_WORD0 0x0003ffffUL
#endif
#ifndef APP_STRATUM_ASM_CANDIDATE_LOOP
#define APP_STRATUM_ASM_CANDIDATE_LOOP 1
#endif
#define STRATUM_MINE_BATCH APP_STRATUM_MINE_BATCH
#define STRATUM_MINE_YIELD_BATCHES APP_STRATUM_MINE_YIELD_BATCHES
#define STRATUM_MINE_CANDIDATE_GUARD_NONCES APP_STRATUM_MINE_CANDIDATE_GUARD_NONCES
#define STRATUM_SOFTWARE_MINER_ENABLED APP_STRATUM_SOFTWARE_MINER_ENABLED
#define STRATUM_SOFTWARE_MINE_BATCH APP_STRATUM_SOFTWARE_MINE_BATCH
#define STRATUM_MIN_CANDIDATE_TARGET_WORD0 APP_STRATUM_MIN_CANDIDATE_TARGET_WORD0
#define STRATUM_ASM_CANDIDATE_LOOP APP_STRATUM_ASM_CANDIDATE_LOOP
#define STRATUM_MINER_TASK_COUNT 1
#define STRATUM_MINER_TASK_STACK_BYTES 4096
#define STRATUM_MINER_TASK_PRIORITY 1
#define STRATUM_SOFTWARE_MINER_TASK_STACK_BYTES 4096
#define STRATUM_SOFTWARE_MINER_TASK_PRIORITY 0
#define STRATUM_SESSION_POLL_MS 100
#define STRATUM_SESSION_IDLE_POLL_MS 1000
#define STRATUM_INITIAL_HASHRATE_SAMPLE_MS 5000
#define STRATUM_HASHRATE_SAMPLE_MS 15000
#ifndef APP_STRATUM_BOOT_BENCHMARK_SAMPLE_US
#define APP_STRATUM_BOOT_BENCHMARK_SAMPLE_US 1500000
#endif
#ifndef APP_STRATUM_BOOT_BENCHMARK_WARMUP_US
#define APP_STRATUM_BOOT_BENCHMARK_WARMUP_US 100000
#endif
#ifndef APP_STRATUM_BOOT_BENCHMARK_CHUNK
#define APP_STRATUM_BOOT_BENCHMARK_CHUNK 8192
#endif
#define STRATUM_BOOT_BENCHMARK_SAMPLE_US APP_STRATUM_BOOT_BENCHMARK_SAMPLE_US
#define STRATUM_BOOT_BENCHMARK_WARMUP_US APP_STRATUM_BOOT_BENCHMARK_WARMUP_US
#define STRATUM_BOOT_BENCHMARK_CHUNK APP_STRATUM_BOOT_BENCHMARK_CHUNK
#ifndef APP_SYNTHETIC_HASHES_PER_TASK
#define APP_SYNTHETIC_HASHES_PER_TASK 524288
#endif
#define STRATUM_SYNTHETIC_HASHES_PER_TASK APP_SYNTHETIC_HASHES_PER_TASK
#define STRATUM_MAX_BRANCHES 24
#define STRATUM_MAX_COINBASE_HEX 760
#define STRATUM_TARGET_SHARES_PER_MIN 14ULL
#define STRATUM_MIN_SUGGESTED_DIFFICULTY_SCALED 100ULL
#define STRATUM_SHARE_QUEUE_MAX 128

#define STRATUM_DEFAULT_PRIMARY "tinyminer.m45core.com"
#define STRATUM_DEFAULT_BACKUP "solobtc.nmminer.com"
#define STRATUM_DEFAULT_PORT 3333
#define STRATUM_DEFAULT_USER "0M45"
#define STRATUM_DEFAULT_WALLET "3B86bWqfjdQeLEr8nkeeWU6ygksc2K7MoL"
typedef struct {
  char ssid[33];
  char password[65];
} wifi_settings_t;

typedef struct {
  char primary_host[96];
  uint16_t primary_port;
  char backup_host[96];
  uint16_t backup_port;
  char username[64];
  char wallet[128];
  char suggested_difficulty[24];
  uint32_t benchmark_hashes_per_second;
} stratum_settings_t;

typedef enum {
  STRATUM_PAYOUT_STATUS_UNCHECKED = 0,
  STRATUM_PAYOUT_STATUS_OK,
  STRATUM_PAYOUT_STATUS_LOW,
  STRATUM_PAYOUT_STATUS_MISSING,
  STRATUM_PAYOUT_STATUS_UNSUPPORTED_WALLET,
  STRATUM_PAYOUT_STATUS_PARSE_ERROR,
} stratum_payout_status_t;

typedef struct {
  uint8_t flip_vertical;
  uint8_t screensaver;
  uint8_t light_mode;
  uint8_t brightness_pct;
  uint16_t sleep_timeout_minutes;
} display_settings_t;

typedef struct {
  uint8_t performance_mode;
} system_settings_t;

typedef struct {
  bool enabled;
  bool connected;
  bool using_backup;
  char current_pool[128];
  uint16_t current_port;
  uint32_t accepted;
  uint32_t rejected;
  uint32_t submitted;
  uint32_t found_shares;
  uint32_t hardware_hash_checks;
  uint32_t hardware_hash_errors;
  uint32_t best_share;
  uint32_t current_minute_best_share;
  uint32_t last_completed_minute_best_share;
  uint32_t hashes_per_second;
  uint64_t hashes_total;
  int64_t share_bucket_minute;
  int64_t last_completed_share_minute;
  char current_difficulty[24];
  char current_block[80];
  stratum_payout_status_t payout_status;
  uint16_t payout_percent_x100;
  uint64_t connected_at_us;
  uint64_t disconnected_at_us;
  uint64_t connected_total_us;
  uint64_t first_connected_at_us;
} stratum_runtime_t;

typedef struct {
  bool subscribed;
  bool ready;
  char extra_nonce1[64];
  uint8_t extra_nonce1_len;
  uint8_t extra_nonce2_size;
  uint8_t extra_nonce2[16];
  char job_id[96];
  char ntime[16];
  uint8_t header[80];
  uint32_t header_midstate[8];
  uint32_t header_tail_words[3];
  uint8_t share_target[32];
  uint32_t share_target_words[8];
  uint32_t next_nonce;
} mining_job_t;

extern wifi_settings_t g_wifi;
extern stratum_settings_t g_stratum;
extern display_settings_t g_display;
extern system_settings_t g_system;
extern char g_theme[24];
extern char g_ap_ssid[32];
extern char g_ap_ip_text[16];
extern uint8_t g_ap_ip[4];
extern char g_sta_ip[16];
extern bool g_sta_connected;
extern stratum_runtime_t g_stratum_runtime;
extern mining_job_t g_mining_job;
extern uint64_t g_boot_us;

void copy_str(char* dest, size_t dest_size, const char* src);
void strip_commas(char* value);
uint16_t parse_port(const char* text, uint16_t fallback);
bool save_wifi_settings(const char* ssid, const char* password);
bool save_stratum_settings(void);
bool save_system_settings(void);
void apply_performance_mode(void);
bool stratum_identity_text_valid(const char* text, bool allow_empty);
bool stratum_difficulty_text_valid(const char* text, bool allow_empty);

void uptime_text(char* out, size_t out_size);
const char* pool_status_text(void);
void pool_endpoint_text(char* out, size_t out_size);
void shares_per_min_text(char* out, size_t out_size);
const char* current_difficulty_text(void);
void device_name_text(char* out, size_t out_size);

void start_http_server(void);
void start_wifi(void);
void start_network_tasks(void);
bool wifi_setup_ap_active(void);
bool wifi_station_started(void);
bool wifi_test_station_credentials(const char* ssid, const char* password, char* error, size_t error_size);
void start_oled(void);
void oled_prepare_runtime_qr(void);
void oled_prepare_pool_endpoint_cache(void);
void oled_next_page(void);
void oled_update_status(void);
void display_sleep(void);
void display_wake(void);
bool display_is_sleeping(void);
void request_display_refresh(void);
void stratum_self_check(void);
uint32_t stratum_benchmark_hashrate_us(int64_t duration_us);
void stratum_synthetic_benchmark(void);
void stratum_task(void* arg);
