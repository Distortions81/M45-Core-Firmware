#include "app_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <stdint.h>
#include <math.h>

#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "build_info.h"
#include "hex_utils.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "web_assets.h"

typedef struct {
  const char* key;
  const char* value;
} template_value_t;

#define NETWORK_SCAN_CACHE_TTL_US (15LL * 1000000LL)
#define HTTP_SERVER_STACK_BYTES 8192
#define HASHRATE_AVERAGE_WINDOW_US (15LL * 60LL * 1000000LL)
#define HASHRATE_AVERAGE_MIN_SPAN_US (60LL * 1000000LL)
#define HASHRATE_AVERAGE_SAMPLE_US (15LL * 1000000LL)
#define HASHRATE_AVERAGE_MAX_SAMPLES 64

static const char* TAG = "web";
static char g_networks_cache[20 * 128];
static int64_t g_networks_cache_us = 0;
static char g_action_token[33];
typedef struct {
  int64_t time_us;
  uint64_t hashes_total;
} hashrate_average_sample_t;
static hashrate_average_sample_t g_hashrate_average_samples[HASHRATE_AVERAGE_MAX_SAMPLES];
static size_t g_hashrate_average_count = 0;
static portMUX_TYPE g_hashrate_average_mux = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t stats_page_handler(httpd_req_t* req);

/* Private implementation fragments share the static state above. */
#include "web_server_render.inc"
#include "web_server_form.inc"
#include "web_server_format.inc"
#include "web_server_pages.inc"
#include "web_server_api.inc"
#include "web_server_routes.inc"
