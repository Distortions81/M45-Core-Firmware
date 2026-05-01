/* Private implementation fragment included by web_server.c. */

static void add_common_template_values(
    template_value_t* values,
    size_t* count,
    bool include_toolbar,
    const char* active_page) {
  const char* theme = normalized_theme(g_theme);
  values[(*count)++] = (template_value_t){"THEMES", WEB_THEMES_HTML};
  values[(*count)++] = (template_value_t){"GLOBAL_STYLES", WEB_GLOBAL_STYLES_HTML};
  values[(*count)++] = (template_value_t){"TOOLBAR", include_toolbar ? WEB_TOOLBAR_HTML : ""};
  values[(*count)++] = (template_value_t){"FOOTER", WEB_FOOTER_HTML};
  values[(*count)++] = (template_value_t){"THEME", theme};
  values[(*count)++] = (template_value_t){"ACTION_TOKEN", action_token()};
  values[(*count)++] =
      (template_value_t){"STATS_ACTIVE", strcmp(active_page != NULL ? active_page : "", "stats") == 0 ? "active" : ""};
  values[(*count)++] = (template_value_t){
      "SETTINGS_ACTIVE", strcmp(active_page != NULL ? active_page : "", "settings") == 0 ? "active" : ""};
}

static esp_err_t send_setup_page(httpd_req_t* req, bool include_toolbar) {
  char saved_wifi_ssid[224];
  html_escape(g_wifi.ssid, saved_wifi_ssid, sizeof(saved_wifi_ssid));
  template_value_t values[24];
  size_t count = 0;
  add_common_template_values(values, &count, include_toolbar, "settings");
  values[count++] = (template_value_t){"PAGE_TITLE", g_sta_connected ? "Wi-Fi" : "Device setup"};
  values[count++] = (template_value_t){"SSID", g_ap_ssid};
  values[count++] =
      (template_value_t){"DEVICE_IP", g_sta_connected && g_sta_ip[0] != '\0' ? g_sta_ip : g_ap_ip_text};
  values[count++] = (template_value_t){"PAGE_INTRO",
                                       g_sta_connected
                                           ? "Choose a different Wi-Fi network or enter one manually."
                                           : "Choose a Wi-Fi network to connect this device."};
  values[count++] = (template_value_t){"SAVED_WIFI_SSID", saved_wifi_ssid};
  values[count++] = (template_value_t){"SAVE_BUTTON_LABEL", g_sta_connected ? "Save and reconnect" : "Save & reboot"};
  values[count++] = (template_value_t){"SAVE_HINT",
                                       g_sta_connected
                                           ? "Save settings to reboot and reconnect to the selected Wi-Fi."
                                           : "The device will test the Wi-Fi connection before saving and rebooting."};
  return send_template(req, WEB_SETUP_PAGE_HTML, values, count);
}

static esp_err_t root_handler(httpd_req_t* req) {
  return g_sta_connected ? stats_page_handler(req) : send_setup_page(req, false);
}

static esp_err_t wifi_page_handler(httpd_req_t* req) {
  return send_setup_page(req, g_sta_connected);
}

static esp_err_t settings_page_handler(httpd_req_t* req) {
  char primary_port[8];
  char backup_port[8];
  char display_brightness[8];
  char display_sleep_minutes[8];
  char display_sleep_max_minutes[8];
  char sta_ssid[224];
  char sta_ip[48];
  char primary_host[480];
  char backup_host[480];
  char username[384];
  char wallet[768];
  char device_name[48];
  device_name_text(device_name, sizeof(device_name));
  snprintf(primary_port, sizeof(primary_port), "%u", g_stratum.primary_port);
  snprintf(backup_port, sizeof(backup_port), "%u", g_stratum.backup_port);
  snprintf(display_brightness, sizeof(display_brightness), "%u", g_display.brightness_pct);
  snprintf(display_sleep_minutes,
           sizeof(display_sleep_minutes),
           "%u",
           (unsigned)g_display.sleep_timeout_minutes);
  snprintf(display_sleep_max_minutes,
           sizeof(display_sleep_max_minutes),
           "%u",
           (unsigned)DISPLAY_SLEEP_MAX_MINUTES);
  html_escape(g_wifi.ssid[0] != '\0' ? g_wifi.ssid : "Not configured", sta_ssid, sizeof(sta_ssid));
  html_escape(g_sta_ip[0] != '\0' ? g_sta_ip : "Not connected", sta_ip, sizeof(sta_ip));
  html_escape(g_stratum.primary_host, primary_host, sizeof(primary_host));
  html_escape(g_stratum.backup_host, backup_host, sizeof(backup_host));
  html_escape(g_stratum.username, username, sizeof(username));
  html_escape(g_stratum.wallet, wallet, sizeof(wallet));

  template_value_t values[32];
  size_t count = 0;
  add_common_template_values(values, &count, true, "settings");
  values[count++] = (template_value_t){"DEVICE_NAME", device_name};
  values[count++] = (template_value_t){"STA_SSID", sta_ssid};
  values[count++] = (template_value_t){"STA_IP", sta_ip};
  values[count++] =
      (template_value_t){"DISPLAY_FLIP_VERTICAL_CHECKED", g_display.flip_vertical ? "checked" : ""};
  values[count++] =
      (template_value_t){"DISPLAY_RUNTIME_SCREENSAVER_CHECKED", g_display.screensaver ? "checked" : ""};
  values[count++] = (template_value_t){
      "DISPLAY_LIGHT_MODE_CHECKED", strcmp(normalized_theme(g_theme), "light") == 0 ? "checked" : ""};
  values[count++] = (template_value_t){"DISPLAY_BRIGHTNESS", display_brightness};
  values[count++] = (template_value_t){"DISPLAY_SLEEP_MINUTES", display_sleep_minutes};
  values[count++] = (template_value_t){"DISPLAY_SLEEP_MAX_MINUTES", display_sleep_max_minutes};
  values[count++] =
      (template_value_t){"PERFORMANCE_MODE_CHECKED", g_system.performance_mode ? "checked" : ""};
  values[count++] = (template_value_t){"STRATUM_PRIMARY_SERVER", primary_host};
  values[count++] = (template_value_t){"STRATUM_PRIMARY_PORT", primary_port};
  values[count++] = (template_value_t){"STRATUM_BACKUP_SERVER", backup_host};
  values[count++] = (template_value_t){"STRATUM_BACKUP_PORT", backup_port};
  values[count++] = (template_value_t){"STRATUM_USERNAME", username};
  values[count++] = (template_value_t){"STRATUM_WALLET_ADDRESS", wallet};
  return send_template(req, WEB_SETTINGS_PAGE_HTML, values, count);
}

static esp_err_t stats_page_handler(httpd_req_t* req) {
  char uptime[16];
  char endpoint[128];
  char hashrate[16];
  char hashrate_average[64];
  char shares_per_min[16];
  char submitted[16];
  char rejected[16];
  char share_error_pct[16];
  char payout_percent[16];
  char payout_status[160];
  char pool_status[64];
  char endpoint_escaped[768];
  char difficulty_escaped[160];
  char difficulty_micro[32];
  char device_name[48];
  char free_heap[16];
  char min_free_heap[16];
  char largest_free_block[16];
  char internal_free_heap[16];
  char internal_min_free_heap[16];
  char internal_largest_free_block[16];
  char dma_free_heap[16];
  char hardware_uptime[16];
  char pool_uptime[16];
  char pool_uptime_pct[16];
  char hash_error_pct[16];
  const int64_t now = esp_timer_get_time();
  const uint64_t app_uptime_us = app_uptime_us_now(now);
  const uint64_t pool_connected_us = pool_connected_us_now(now);
  const uint64_t pool_tracking_us = pool_tracking_us_now(now);
  device_name_text(device_name, sizeof(device_name));
  uptime_text(uptime, sizeof(uptime));
  pool_endpoint_text(endpoint, sizeof(endpoint));
  shares_per_min_text(shares_per_min, sizeof(shares_per_min));
  html_escape(pool_status_text(), pool_status, sizeof(pool_status));
  html_escape(endpoint, endpoint_escaped, sizeof(endpoint_escaped));
  html_escape(current_difficulty_text(), difficulty_escaped, sizeof(difficulty_escaped));
  format_microdifficulty_text(current_difficulty_text(), difficulty_micro, sizeof(difficulty_micro));
  format_bytes_text(esp_get_free_heap_size(), free_heap, sizeof(free_heap));
  format_bytes_text(esp_get_minimum_free_heap_size(), min_free_heap, sizeof(min_free_heap));
  format_bytes_text(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                    largest_free_block,
                    sizeof(largest_free_block));
  format_bytes_text(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                    internal_free_heap,
                    sizeof(internal_free_heap));
  format_bytes_text(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                    internal_min_free_heap,
                    sizeof(internal_min_free_heap));
  format_bytes_text(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                    internal_largest_free_block,
                    sizeof(internal_largest_free_block));
  format_bytes_text(heap_caps_get_free_size(MALLOC_CAP_DMA), dma_free_heap, sizeof(dma_free_heap));
  format_duration_us_text(app_uptime_us, hardware_uptime, sizeof(hardware_uptime));
  format_duration_us_text(pool_connected_us, pool_uptime, sizeof(pool_uptime));
  format_pool_uptime_pct_text(pool_connected_us, pool_tracking_us, pool_uptime_pct, sizeof(pool_uptime_pct));
  snprintf(hashrate, sizeof(hashrate), "%u", (unsigned)g_stratum_runtime.hashes_per_second);
  format_hashrate_average_text(hashrate_average, sizeof(hashrate_average));
  snprintf(submitted, sizeof(submitted), "%u", (unsigned)g_stratum_runtime.submitted);
  snprintf(rejected, sizeof(rejected), "%u", (unsigned)g_stratum_runtime.rejected);
  format_share_error_pct_text(g_stratum_runtime.rejected,
                              g_stratum_runtime.submitted,
                              share_error_pct,
                              sizeof(share_error_pct));
  format_payout_percent_text(payout_percent, sizeof(payout_percent));
  html_escape(payout_status_text(), payout_status, sizeof(payout_status));
  format_share_error_pct_text(g_stratum_runtime.hardware_hash_errors,
                              g_stratum_runtime.hardware_hash_checks,
                              hash_error_pct,
                              sizeof(hash_error_pct));

  template_value_t values[48];
  size_t count = 0;
  add_common_template_values(values, &count, true, "stats");
  values[count++] = (template_value_t){"DEVICE_NAME", device_name};
  values[count++] = (template_value_t){"HASHES_PER_SEC", hashrate};
  values[count++] = (template_value_t){"HASHRATE_AVERAGE", hashrate_average};
  values[count++] = (template_value_t){"UPTIME", uptime};
  values[count++] = (template_value_t){"POOL_STATUS", pool_status};
  values[count++] = (template_value_t){"POOL_ENDPOINT", endpoint_escaped};
  values[count++] = (template_value_t){"SHARES_PER_MIN", shares_per_min};
  values[count++] = (template_value_t){"CURRENT_DIFFICULTY", difficulty_escaped};
  values[count++] = (template_value_t){"CURRENT_DIFFICULTY_MICRO", difficulty_micro};
  values[count++] = (template_value_t){"SUBMITTED_SHARES", submitted};
  values[count++] = (template_value_t){"REJECTED_SHARES", rejected};
  values[count++] = (template_value_t){"SHARE_ERROR_PCT", share_error_pct};
  values[count++] = (template_value_t){"PAYOUT_STATUS_ID", payout_status_id()};
  values[count++] = (template_value_t){"PAYOUT_STATUS_TEXT", payout_status};
  values[count++] = (template_value_t){"PAYOUT_PERCENT", payout_percent};
  values[count++] = (template_value_t){
      "PAYOUT_PERCENT_HIDDEN", g_stratum_runtime.payout_status == STRATUM_PAYOUT_STATUS_UNCHECKED ? "hidden" : ""};
  values[count++] = (template_value_t){"FREE_HEAP", free_heap};
  values[count++] = (template_value_t){"MIN_FREE_HEAP", min_free_heap};
  values[count++] = (template_value_t){"LARGEST_FREE_BLOCK", largest_free_block};
  values[count++] = (template_value_t){"INTERNAL_FREE_HEAP", internal_free_heap};
  values[count++] = (template_value_t){"INTERNAL_MIN_FREE_HEAP", internal_min_free_heap};
  values[count++] = (template_value_t){"INTERNAL_LARGEST_FREE_BLOCK", internal_largest_free_block};
  values[count++] = (template_value_t){"DMA_FREE_HEAP", dma_free_heap};
  values[count++] = (template_value_t){"HARDWARE_UPTIME", hardware_uptime};
  values[count++] = (template_value_t){"POOL_UPTIME", pool_uptime};
  values[count++] = (template_value_t){"POOL_UPTIME_PCT", pool_uptime_pct};
  values[count++] = (template_value_t){"HASH_ERROR_PCT", hash_error_pct};
  values[count++] = (template_value_t){"BUILD_ID", APP_BUILD_ID};
  values[count++] = (template_value_t){"BUILD_TIME", APP_BUILD_TIME_UTC};
  return send_template(req, WEB_STATS_PAGE_HTML, values, count);
}

static esp_err_t licenses_handler(httpd_req_t* req) {
  template_value_t values[12];
  size_t count = 0;
  add_common_template_values(values, &count, true, "");
  return send_template(req, WEB_LICENSES_PAGE_HTML, values, count);
}

static esp_err_t mit_license_handler(httpd_req_t* req) {
  return send_gzip_text(req, WEB_MIT_TXT_GZ, WEB_MIT_TXT_GZ_LEN);
}

static esp_err_t apache_license_handler(httpd_req_t* req) {
  return send_gzip_text(req, WEB_APACHE_2_0_TXT_GZ, WEB_APACHE_2_0_TXT_GZ_LEN);
}

static esp_err_t qrcode_license_handler(httpd_req_t* req) {
  return send_gzip_text(req, WEB_QRCODE_TXT_GZ, WEB_QRCODE_TXT_GZ_LEN);
}
