#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "app_state.h"

wifi_settings_t g_wifi;
stratum_settings_t g_stratum;
display_settings_t g_display;
system_settings_t g_system;
char g_theme[24] = "dark";
char g_ap_ssid[32] = AP_SSID;
char g_ap_ip_text[16] = "10.45.0.1";
uint8_t g_ap_ip[4] = {AP_IP0, AP_IP1, AP_IP2, AP_IP3};
char g_sta_ip[16] = "";
bool g_sta_connected = false;
stratum_runtime_t g_stratum_runtime;
mining_job_t g_mining_job;
uint64_t g_boot_us = 0;

static const char* TAG = "app";
static const int64_t SETUP_DISPLAY_REFRESH_US = 5000000LL;
static const int64_t RUNTIME_DISPLAY_CHECK_US = 1000000LL;
static const int64_t RUNTIME_CONNECTED_DISPLAY_CHECK_US = 1000000LL;
static const int64_t RUNTIME_PRE_POOL_REFRESH_US = 1000000LL;
static const int64_t RUNTIME_HASHRATE_REFRESH_US = 60000000LL;
static const int64_t DISPLAY_SLEEP_MINUTE_US = 60000000LL;

static int64_t g_last_display_check_us = 0;
static int64_t g_last_display_refresh_us = 0;
static int64_t g_last_hashrate_refresh_us = 0;
static int64_t g_last_display_input_us = 0;
static bool g_last_display_connected = false;
static bool g_last_display_enabled = false;
static bool g_last_display_using_backup = false;
static bool g_last_display_had_hashrate = false;
static volatile bool g_display_refresh_requested = false;
static char g_last_display_pool[sizeof(g_stratum_runtime.current_pool)];
static char g_last_display_ip[sizeof(g_sta_ip)];
static char g_last_display_block[sizeof(g_stratum_runtime.current_block)];
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t g_cpu_freq_lock = NULL;
static bool g_cpu_freq_lock_held = false;
#endif

void copy_str(char* dest, size_t dest_size, const char* src) {
  if (dest_size == 0) {
    return;
  }
  if (src == NULL) {
    src = "";
  }
  strlcpy(dest, src, dest_size);
}

void strip_commas(char* value) {
  char* write = value;
  for (char* read = value; *read != '\0'; ++read) {
    if (*read != ',') {
      *write++ = *read;
    }
  }
  *write = '\0';
}

uint16_t parse_port(const char* text, uint16_t fallback) {
  int value = 0;
  if (text == NULL || *text == '\0') {
    return fallback;
  }
  while (*text >= '0' && *text <= '9') {
    value = value * 10 + (*text - '0');
    if (value > 65535) {
      return fallback;
    }
    ++text;
  }
  return value > 0 ? (uint16_t)value : fallback;
}

static void load_string(nvs_handle_t nvs, const char* key, char* out, size_t out_size) {
  size_t size = out_size;
  (void)nvs_get_str(nvs, key, out, &size);
}

static void restore_empty_stratum_defaults(void) {
  if (g_stratum.primary_host[0] == '\0') {
    copy_str(g_stratum.primary_host, sizeof(g_stratum.primary_host), STRATUM_DEFAULT_PRIMARY);
  }
  if (g_stratum.backup_host[0] == '\0') {
    copy_str(g_stratum.backup_host, sizeof(g_stratum.backup_host), STRATUM_DEFAULT_BACKUP);
  }
  if (g_stratum.username[0] == '\0') {
    copy_str(g_stratum.username, sizeof(g_stratum.username), STRATUM_DEFAULT_USER);
  }
  if (g_stratum.wallet[0] == '\0') {
    copy_str(g_stratum.wallet, sizeof(g_stratum.wallet), STRATUM_DEFAULT_WALLET);
  }
}

bool stratum_identity_text_valid(const char* text, bool allow_empty) {
  if (text == NULL || *text == '\0') {
    return allow_empty;
  }
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    const char ch = *cursor;
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-') {
      continue;
    }
    return false;
  }
  return true;
}

bool stratum_difficulty_text_valid(const char* text, bool allow_empty) {
  if (text == NULL || *text == '\0') {
    return allow_empty;
  }
  bool saw_digit = false;
  bool saw_dot = false;
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    const char ch = *cursor;
    if (ch >= '0' && ch <= '9') {
      saw_digit = true;
      continue;
    }
    if (ch == '.' && !saw_dot) {
      saw_dot = true;
      continue;
    }
    return false;
  }
  return saw_digit;
}

static void load_settings(void) {
  memset(&g_wifi, 0, sizeof(g_wifi));
  memset(&g_stratum, 0, sizeof(g_stratum));
  memset(&g_display, 0, sizeof(g_display));
  memset(&g_system, 0, sizeof(g_system));
  g_display.screensaver = 1;
#if APP_DISPLAY_IDEASPARK_ESP32_19_LCD
  g_display.brightness_pct = 50;
#else
  g_display.brightness_pct = 100;
#endif
  copy_str(g_theme, sizeof(g_theme), "dark");

  nvs_handle_t nvs;
  if (nvs_open("wifi", NVS_READONLY, &nvs) == ESP_OK) {
    load_string(nvs, "ssid", g_wifi.ssid, sizeof(g_wifi.ssid));
    load_string(nvs, "password", g_wifi.password, sizeof(g_wifi.password));
    nvs_close(nvs);
  }

  g_stratum.primary_port = STRATUM_DEFAULT_PORT;
  g_stratum.backup_port = STRATUM_DEFAULT_PORT;
  copy_str(g_stratum.primary_host, sizeof(g_stratum.primary_host), STRATUM_DEFAULT_PRIMARY);
  copy_str(g_stratum.backup_host, sizeof(g_stratum.backup_host), STRATUM_DEFAULT_BACKUP);
  copy_str(g_stratum.username, sizeof(g_stratum.username), STRATUM_DEFAULT_USER);
  copy_str(g_stratum.wallet, sizeof(g_stratum.wallet), STRATUM_DEFAULT_WALLET);
  if (nvs_open("stratum", NVS_READONLY, &nvs) == ESP_OK) {
    load_string(nvs, "primary_host", g_stratum.primary_host, sizeof(g_stratum.primary_host));
    load_string(nvs, "backup_host", g_stratum.backup_host, sizeof(g_stratum.backup_host));
    load_string(nvs, "username", g_stratum.username, sizeof(g_stratum.username));
    load_string(nvs, "wallet_addr", g_stratum.wallet, sizeof(g_stratum.wallet));
    load_string(nvs, "suggest_diff", g_stratum.suggested_difficulty, sizeof(g_stratum.suggested_difficulty));
    uint16_t port = 0;
    if (nvs_get_u16(nvs, "primary_port", &port) == ESP_OK && port > 0) {
      g_stratum.primary_port = port;
    }
    if (nvs_get_u16(nvs, "backup_port", &port) == ESP_OK && port > 0) {
      g_stratum.backup_port = port;
    }
    nvs_close(nvs);
  }
  restore_empty_stratum_defaults();
  if (!stratum_identity_text_valid(g_stratum.username, true)) {
    copy_str(g_stratum.username, sizeof(g_stratum.username), STRATUM_DEFAULT_USER);
  }
  if (!stratum_identity_text_valid(g_stratum.wallet, false)) {
    copy_str(g_stratum.wallet, sizeof(g_stratum.wallet), STRATUM_DEFAULT_WALLET);
  }
  if (!stratum_difficulty_text_valid(g_stratum.suggested_difficulty, true)) {
    g_stratum.suggested_difficulty[0] = '\0';
  }

  if (nvs_open("display", NVS_READONLY, &nvs) == ESP_OK) {
    nvs_get_u8(nvs, "flip", &g_display.flip_vertical);
    nvs_get_u8(nvs, "screensaver", &g_display.screensaver);
    nvs_get_u8(nvs, "light", &g_display.light_mode);
    nvs_get_u8(nvs, "brightness", &g_display.brightness_pct);
    nvs_get_u16(nvs, "sleep_min", &g_display.sleep_timeout_minutes);
    nvs_close(nvs);
  }
  if (g_display.brightness_pct > 100) {
    g_display.brightness_pct = 100;
  }

  if (nvs_open("system", NVS_READONLY, &nvs) == ESP_OK) {
    nvs_get_u8(nvs, "perf", &g_system.performance_mode);
    nvs_close(nvs);
  }
  g_system.performance_mode = g_system.performance_mode != 0 ? 1 : 0;

  bool loaded_theme = false;
  if (nvs_open("theme", NVS_READONLY, &nvs) == ESP_OK) {
    load_string(nvs, "name", g_theme, sizeof(g_theme));
    loaded_theme = true;
    nvs_close(nvs);
  }
  if (!loaded_theme) {
    copy_str(g_theme, sizeof(g_theme), g_display.light_mode != 0 ? "light" : "dark");
  } else if (strcmp(g_theme, "light") != 0) {
    copy_str(g_theme, sizeof(g_theme), "dark");
  }
  g_display.light_mode = strcmp(g_theme, "light") == 0 ? 1 : 0;
}

bool save_wifi_settings(const char* ssid, const char* password) {
  nvs_handle_t nvs;
  if (nvs_open("wifi", NVS_READWRITE, &nvs) != ESP_OK) {
    return false;
  }
  const esp_err_t ssid_result = nvs_set_str(nvs, "ssid", ssid);
  const esp_err_t password_result = nvs_set_str(nvs, "password", password);
  const esp_err_t commit_result = nvs_commit(nvs);
  nvs_close(nvs);
  return ssid_result == ESP_OK && password_result == ESP_OK && commit_result == ESP_OK;
}

bool save_stratum_settings(void) {
  nvs_handle_t nvs;
  if (nvs_open("stratum", NVS_READWRITE, &nvs) != ESP_OK) {
    return false;
  }
  strip_commas(g_stratum.primary_host);
  strip_commas(g_stratum.backup_host);
  const esp_err_t a = nvs_set_str(nvs, "primary_host", g_stratum.primary_host);
  const esp_err_t b = nvs_set_u16(nvs, "primary_port", g_stratum.primary_port);
  const esp_err_t c = nvs_set_str(nvs, "backup_host", g_stratum.backup_host);
  const esp_err_t d = nvs_set_u16(nvs, "backup_port", g_stratum.backup_port);
  const esp_err_t e = nvs_set_str(nvs, "username", g_stratum.username);
  const esp_err_t f = nvs_set_str(nvs, "wallet_addr", g_stratum.wallet);
  const esp_err_t g = g_stratum.suggested_difficulty[0] == '\0'
                          ? ESP_OK
                          : nvs_set_str(nvs, "suggest_diff", g_stratum.suggested_difficulty);
  const esp_err_t h = nvs_commit(nvs);
  nvs_close(nvs);
  return a == ESP_OK && b == ESP_OK && c == ESP_OK && d == ESP_OK && e == ESP_OK &&
         f == ESP_OK && g == ESP_OK && h == ESP_OK;
}

bool save_system_settings(void) {
  nvs_handle_t nvs;
  if (nvs_open("system", NVS_READWRITE, &nvs) != ESP_OK) {
    return false;
  }
  const esp_err_t mode_result = nvs_set_u8(nvs, "perf", g_system.performance_mode ? 1 : 0);
  const esp_err_t commit_result = nvs_commit(nvs);
  nvs_close(nvs);
  return mode_result == ESP_OK && commit_result == ESP_OK;
}

static void choose_suggested_difficulty(uint32_t hashes_per_second, char* out, size_t out_size) {
  const uint64_t scaled =
      ((uint64_t)hashes_per_second * 60ULL * 10000000000ULL) /
      (STRATUM_TARGET_SHARES_PER_MIN * 4294967296ULL);
  const uint64_t clamped = scaled < STRATUM_MIN_SUGGESTED_DIFFICULTY_SCALED
                               ? STRATUM_MIN_SUGGESTED_DIFFICULTY_SCALED
                               : scaled;
  snprintf(out, out_size, "0.%010llu", (unsigned long long)clamped);
}

static void run_first_boot_hash_benchmark(void) {
  g_stratum.benchmark_hashes_per_second =
      stratum_benchmark_hashrate_us(STRATUM_BOOT_BENCHMARK_SAMPLE_US);
  if (g_stratum.suggested_difficulty[0] == '\0') {
    choose_suggested_difficulty(g_stratum.benchmark_hashes_per_second,
                                g_stratum.suggested_difficulty,
                                sizeof(g_stratum.suggested_difficulty));
    save_stratum_settings();
  }
}

static void load_best_share(void) {
  nvs_handle_t nvs;
  if (nvs_open("stats", NVS_READONLY, &nvs) == ESP_OK) {
    nvs_get_u32(nvs, "best_share", &g_stratum_runtime.best_share);
    nvs_close(nvs);
  }
}

static void record_display_refresh(int64_t now) {
  g_last_display_refresh_us = now;
  g_display_refresh_requested = false;
  if (g_stratum_runtime.connected) {
    g_last_hashrate_refresh_us = now;
  }
  g_last_display_connected = g_stratum_runtime.connected;
  g_last_display_enabled = g_stratum_runtime.enabled;
  g_last_display_using_backup = g_stratum_runtime.using_backup;
  g_last_display_had_hashrate = g_stratum_runtime.hashes_per_second > 0;
  copy_str(g_last_display_pool, sizeof(g_last_display_pool), g_stratum_runtime.current_pool);
  copy_str(g_last_display_ip, sizeof(g_last_display_ip), g_sta_ip);
  copy_str(g_last_display_block, sizeof(g_last_display_block), g_stratum_runtime.current_block);
}

void request_display_refresh(void) {
  g_display_refresh_requested = true;
}

static int64_t display_sleep_timeout_us(void) {
  if (g_display.sleep_timeout_minutes == 0) {
    return 0;
  }
  return (int64_t)g_display.sleep_timeout_minutes * DISPLAY_SLEEP_MINUTE_US;
}

static bool record_display_input(int64_t now) {
  const bool was_sleeping = display_is_sleeping();
  g_last_display_input_us = now;
  if (was_sleeping) {
    display_wake();
    request_display_refresh();
  }
  return was_sleeping;
}

static void update_display_sleep_state(int64_t now) {
  const int64_t timeout_us = display_sleep_timeout_us();
  if (timeout_us == 0) {
    if (display_is_sleeping()) {
      display_wake();
      request_display_refresh();
    }
    g_last_display_input_us = now;
    return;
  }

  if (g_last_display_input_us == 0) {
    g_last_display_input_us = now;
  }
  if (!display_is_sleeping() && now - g_last_display_input_us >= timeout_us) {
    display_sleep();
  }
}

static bool should_refresh_display(int64_t now) {
  if (g_display_refresh_requested) {
    return true;
  }

  if (!g_sta_connected) {
    const int64_t refresh_interval =
        wifi_setup_ap_active() ? SETUP_DISPLAY_REFRESH_US : RUNTIME_PRE_POOL_REFRESH_US;
    return g_last_display_refresh_us == 0 ||
           now - g_last_display_refresh_us >= refresh_interval;
  }

  const int64_t check_interval =
      g_stratum_runtime.connected ? RUNTIME_CONNECTED_DISPLAY_CHECK_US : RUNTIME_DISPLAY_CHECK_US;
  if (g_stratum_runtime.connected == g_last_display_connected &&
      now - g_last_display_check_us < check_interval) {
    return false;
  }
  g_last_display_check_us = now;

  if (strcmp(g_sta_ip, g_last_display_ip) != 0 ||
      g_stratum_runtime.connected != g_last_display_connected ||
      g_stratum_runtime.enabled != g_last_display_enabled ||
      g_stratum_runtime.using_backup != g_last_display_using_backup ||
      strcmp(g_stratum_runtime.current_pool, g_last_display_pool) != 0 ||
      strcmp(g_stratum_runtime.current_block, g_last_display_block) != 0) {
    return true;
  }

  if (!g_stratum_runtime.connected) {
    return now - g_last_display_refresh_us >= RUNTIME_PRE_POOL_REFRESH_US;
  }

  if (!g_last_display_had_hashrate && g_stratum_runtime.hashes_per_second > 0) {
    return true;
  }

  return now - g_last_hashrate_refresh_us >= RUNTIME_HASHRATE_REFRESH_US;
}

static bool factory_reset_button_pressed(void) {
  return gpio_get_level((gpio_num_t)FACTORY_RESET_GPIO) == 0;
}

static void configure_factory_reset_button(void) {
  gpio_config_t config = {
      .pin_bit_mask = 1ULL << FACTORY_RESET_GPIO,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&config));
}

static void erase_nvs_and_restart(const char* reason) {
  ESP_LOGW(TAG, "Factory reset requested: %s", reason);
  ESP_ERROR_CHECK(nvs_flash_erase());
  vTaskDelay(pdMS_TO_TICKS(150));
  esp_restart();
}

static void factory_reset_boot_check(void) {
  if (!factory_reset_button_pressed()) {
    return;
  }
  ESP_LOGW(TAG, "Hold BOOT for %u ms to factory reset", (unsigned)FACTORY_RESET_HOLD_MS);
  const int64_t started = esp_timer_get_time();
  while (factory_reset_button_pressed()) {
    if (esp_timer_get_time() - started >= (int64_t)FACTORY_RESET_HOLD_MS * 1000LL) {
      erase_nvs_and_restart("boot button held at startup");
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

static void factory_reset_button_task(void* arg) {
  (void)arg;
  int64_t pressed_since = 0;
  bool long_press_handled = false;
  bool consume_current_press = false;
  while (true) {
    const int64_t now = esp_timer_get_time();
    if (factory_reset_button_pressed()) {
      if (pressed_since == 0) {
        pressed_since = now;
        long_press_handled = false;
        consume_current_press = record_display_input(now);
      } else if (!consume_current_press && !long_press_handled &&
                 now - pressed_since >= (int64_t)FACTORY_RESET_HOLD_MS * 1000LL) {
        long_press_handled = true;
        erase_nvs_and_restart("boot button held while running");
      }
    } else {
      if (pressed_since != 0 && !long_press_handled && !consume_current_press) {
        record_display_input(now);
        oled_next_page();
        request_display_refresh();
      }
      pressed_since = 0;
      long_press_handled = false;
      consume_current_press = false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

static void configure_power_management(void) {
#if CONFIG_PM_ENABLE
  const esp_pm_config_t config = {
      .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
      .min_freq_mhz = 80,
      .light_sleep_enable = false,
  };
  const esp_err_t result = esp_pm_configure(&config);
  if (result != ESP_OK) {
    ESP_LOGW(TAG, "power management configure failed: %s", esp_err_to_name(result));
    return;
  }
  ESP_LOGI(TAG,
           "power management configured: max %u MHz, idle %u MHz",
           (unsigned)config.max_freq_mhz,
           (unsigned)config.min_freq_mhz);
#else
  ESP_LOGI(TAG, "power management disabled; CPU frequency fixed by sdkconfig");
#endif
}

#if CONFIG_PM_ENABLE
static bool set_cpu_frequency_lock(bool enabled) {
  if (enabled && g_cpu_freq_lock == NULL) {
    const esp_err_t create_result =
        esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "perf", &g_cpu_freq_lock);
    if (create_result != ESP_OK) {
      ESP_LOGW(TAG, "CPU frequency lock create failed: %s", esp_err_to_name(create_result));
      return false;
    }
  }

  if (enabled && !g_cpu_freq_lock_held) {
    const esp_err_t acquire_result = esp_pm_lock_acquire(g_cpu_freq_lock);
    if (acquire_result != ESP_OK) {
      ESP_LOGW(TAG, "CPU frequency lock acquire failed: %s", esp_err_to_name(acquire_result));
      return false;
    }
    g_cpu_freq_lock_held = true;
    return true;
  }

  if (!enabled && g_cpu_freq_lock_held) {
    const esp_err_t release_result = esp_pm_lock_release(g_cpu_freq_lock);
    if (release_result != ESP_OK) {
      ESP_LOGW(TAG, "CPU frequency lock release failed: %s", esp_err_to_name(release_result));
      return false;
    }
    g_cpu_freq_lock_held = false;
  }

  return true;
}

#endif

void apply_performance_mode(void) {
#if CONFIG_PM_ENABLE
  if (!set_cpu_frequency_lock(g_system.performance_mode)) {
    return;
  }
  if (g_system.performance_mode) {
    ESP_LOGI(TAG, "performance mode enabled");
  } else {
    ESP_LOGI(TAG, "performance mode disabled");
  }
#else
  if (g_system.performance_mode) {
    ESP_LOGW(TAG, "performance mode requested but power management is disabled");
  }
#endif
}

static void configure_task_watchdog(void) {
#if CONFIG_ESP_TASK_WDT_EN
#ifdef CONFIG_ESP_TASK_WDT_PANIC
  const bool trigger_panic = true;
#else
  const bool trigger_panic = false;
#endif
  const esp_task_wdt_config_t config = {
      .timeout_ms = APP_TASK_WDT_TIMEOUT_MS,
      .idle_core_mask = (1U << 0) | (1U << 1),
      .trigger_panic = trigger_panic,
  };
  esp_err_t result = esp_task_wdt_reconfigure(&config);
  if (result == ESP_ERR_INVALID_STATE) {
    result = esp_task_wdt_init(&config);
  }
  APP_SERIAL_LOGF("app: task watchdog timeout=%ums result=%s\n",
                  (unsigned)APP_TASK_WDT_TIMEOUT_MS,
                  esp_err_to_name(result));
#endif
}

void app_main(void) {
  esp_log_level_set("*", ESP_LOG_NONE);
  APP_SERIAL_LOGF("app: boot\n");
#if APP_SYNTHETIC_NO_WIFI_NO_OLED
  APP_SERIAL_LOGF("app: synthetic no-wifi no-oled benchmark mode\n");
  const esp_err_t twdt_result = esp_task_wdt_deinit();
  APP_SERIAL_LOGF("app: task watchdog disable result=%s\n", esp_err_to_name(twdt_result));
  APP_SERIAL_LOGF("app: self check\n");
  stratum_self_check();
  stratum_synthetic_benchmark();
  return;
#endif

  esp_err_t nvs_result = nvs_flash_init();
  if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    nvs_result = nvs_flash_init();
  }
  ESP_ERROR_CHECK(nvs_result);
  APP_SERIAL_LOGF("app: nvs ready\n");
  configure_task_watchdog();
  configure_factory_reset_button();
  factory_reset_boot_check();
  configure_power_management();

  g_boot_us = esp_timer_get_time();
  APP_SERIAL_LOGF("app: self check\n");
  stratum_self_check();
  APP_SERIAL_LOGF("app: load settings\n");
  load_settings();
  apply_performance_mode();
  memset(&g_stratum_runtime, 0, sizeof(g_stratum_runtime));
  g_stratum_runtime.share_bucket_minute = -1;
  g_stratum_runtime.last_completed_share_minute = -1;
  load_best_share();
  copy_str(g_stratum_runtime.current_difficulty,
           sizeof(g_stratum_runtime.current_difficulty),
           g_stratum.suggested_difficulty[0] != '\0' ? g_stratum.suggested_difficulty : "0.0000000000");
  run_first_boot_hash_benchmark();
  g_stratum_runtime.hashes_per_second = g_stratum.benchmark_hashes_per_second;
  APP_SERIAL_LOGF("app: benchmark/settings ready\n");
  if (strcmp(g_stratum_runtime.current_difficulty, "0.0000000000") == 0 &&
      g_stratum.suggested_difficulty[0] != '\0') {
    copy_str(g_stratum_runtime.current_difficulty,
             sizeof(g_stratum_runtime.current_difficulty),
             g_stratum.suggested_difficulty);
  }
  start_oled();
  g_last_display_input_us = esp_timer_get_time();
  oled_update_status();
  APP_SERIAL_LOGF("app: oled started\n");
  start_wifi();
  APP_SERIAL_LOGF("app: wifi started ap=%s ip=%s\n", g_ap_ssid, g_ap_ip_text);
  start_http_server();
  APP_SERIAL_LOGF("app: http start requested\n");
  start_network_tasks();
  APP_SERIAL_LOGF("app: network tasks started\n");
  xTaskCreate(factory_reset_button_task, "factory_reset", 2048, NULL, 5, NULL);
  xTaskCreatePinnedToCore(stratum_task, "stratum", 16384, NULL, 3, NULL, 0);

  while (true) {
    const int64_t now = esp_timer_get_time();
    update_display_sleep_state(now);
    if (!display_is_sleeping() && should_refresh_display(now)) {
      oled_update_status();
      record_display_refresh(now);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
