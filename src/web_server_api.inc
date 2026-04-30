/* Private implementation fragment included by web_server.c. */

static esp_err_t stats_handler(httpd_req_t* req) {
  char line[384];
  char uptime[16];
  char endpoint[128];
  char shares_per_min[16];
  char hashrate_average[64];
  uptime_text(uptime, sizeof(uptime));
  pool_endpoint_text(endpoint, sizeof(endpoint));
  shares_per_min_text(shares_per_min, sizeof(shares_per_min));
  format_hashrate_average_text(hashrate_average, sizeof(hashrate_average));
  snprintf(line,
           sizeof(line),
           "%s,%s,%s,%s,%u,%s,%s,%u,%u\n",
           uptime,
           pool_status_text(),
           endpoint,
           shares_per_min,
           (unsigned)g_stratum_runtime.hashes_per_second,
           hashrate_average,
           current_difficulty_text(),
           (unsigned)g_stratum_runtime.submitted,
           (unsigned)g_stratum_runtime.rejected);
  httpd_resp_set_type(req, "text/csv");
  return httpd_resp_sendstr(req, line);
}

static esp_err_t memory_handler(httpd_req_t* req) {
  char line[320];
  char hash_error_pct[16];
  const int64_t now = esp_timer_get_time();
  const uint64_t app_uptime_us = app_uptime_us_now(now);
  const uint64_t pool_connected_us = pool_connected_us_now(now);
  const uint64_t pool_tracking_us = pool_tracking_us_now(now);
  double pool_uptime_pct = 0.0;
  if (pool_tracking_us > 0) {
    pool_uptime_pct = ((double)pool_connected_us * 100.0) / (double)pool_tracking_us;
    if (pool_uptime_pct > 100.0) {
      pool_uptime_pct = 100.0;
    }
  }
  format_share_error_pct_text(g_stratum_runtime.hardware_hash_errors,
                              g_stratum_runtime.hardware_hash_checks,
                              hash_error_pct,
                              sizeof(hash_error_pct));
  snprintf(line,
           sizeof(line),
           "%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%.1f,%s\n",
           (unsigned)esp_get_free_heap_size(),
           (unsigned)esp_get_minimum_free_heap_size(),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
           (unsigned long long)(app_uptime_us / 1000000ULL),
           (unsigned long long)(pool_connected_us / 1000000ULL),
           pool_uptime_pct,
           hash_error_pct);
  httpd_resp_set_type(req, "text/csv");
  return httpd_resp_sendstr(req, line);
}

static esp_err_t health_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_sendstr(req, "ok");
}

static esp_err_t networks_handler(httpd_req_t* req) {
  const int64_t now = esp_timer_get_time();
  if (g_networks_cache[0] != '\0' && now - g_networks_cache_us < NETWORK_SCAN_CACHE_TTL_US) {
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, g_networks_cache);
  }

  wifi_scan_config_t scan_config = {
      .ssid = NULL,
      .bssid = NULL,
      .channel = 0,
      .show_hidden = false,
      .scan_type = WIFI_SCAN_TYPE_ACTIVE,
  };
  esp_wifi_scan_start(&scan_config, true);
  uint16_t count = 0;
  esp_wifi_scan_get_ap_num(&count);
  if (count > 20) {
    count = 20;
  }
  wifi_ap_record_t records[20];
  memset(records, 0, sizeof(records));
  esp_wifi_scan_get_ap_records(&count, records);

  for (uint16_t i = 0; i < count; ++i) {
    for (uint16_t j = i + 1; j < count; ++j) {
      if (records[j].rssi > records[i].rssi) {
        const wifi_ap_record_t tmp = records[i];
        records[i] = records[j];
        records[j] = tmp;
      }
    }
  }

  size_t offset = 0;
  g_networks_cache[0] = '\0';
  g_networks_cache_us = now;
  for (uint16_t i = 0; i < count && offset + 1 < sizeof(g_networks_cache); ++i) {
    offset += snprintf(g_networks_cache + offset,
                       sizeof(g_networks_cache) - offset,
                       "%s\t%d\t%d\t%d\n",
                       (const char*)records[i].ssid,
                       records[i].rssi,
                       records[i].primary,
                       records[i].authmode == WIFI_AUTH_OPEN ? 1 : 0);
    if (offset >= sizeof(g_networks_cache)) {
      g_networks_cache[sizeof(g_networks_cache) - 1] = '\0';
      break;
    }
  }

  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_sendstr(req, g_networks_cache);
}

static esp_err_t configure_handler(httpd_req_t* req) {
  char body[FORM_BODY_MAX];
  char ssid[33];
  char password[65];
  read_body(req, body, sizeof(body));
  form_value(body, "ssid", ssid, sizeof(ssid));
  form_value(body, "password", password, sizeof(password));
  if (ssid[0] == '\0' || !save_wifi_settings(ssid, password)) {
    httpd_resp_set_status(req, "400 Bad Request");
    send_plain(req, "Wi-Fi name is required");
    return ESP_OK;
  }
  g_networks_cache[0] = '\0';
  g_networks_cache_us = 0;
  if (g_sta_connected) {
    send_plain(req, "Wi-Fi saved. Rebooting now...");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
  }
  send_plain(req, "Wi-Fi saved. Reboot to connect.");
  return ESP_OK;
}

static esp_err_t stratum_handler(httpd_req_t* req) {
  char body[FORM_BODY_MAX];
  char value[128];
  uint16_t port = 0;
  read_body(req, body, sizeof(body));
  if (form_value(body, "primary_server", value, sizeof(value))) {
    copy_str(g_stratum.primary_host, sizeof(g_stratum.primary_host), value);
  }
  if (form_value(body, "primary_port", value, sizeof(value))) {
    if (!parse_required_port(value, &port)) {
      httpd_resp_set_status(req, "400 Bad Request");
      send_plain(req, "Main pool port must be 1-65535");
      return ESP_OK;
    }
    g_stratum.primary_port = port;
  }
  if (form_value(body, "backup_server", value, sizeof(value))) {
    copy_str(g_stratum.backup_host, sizeof(g_stratum.backup_host), value);
  }
  if (form_value(body, "backup_port", value, sizeof(value))) {
    if (!parse_optional_port(value, STRATUM_DEFAULT_PORT, &port)) {
      httpd_resp_set_status(req, "400 Bad Request");
      send_plain(req, "Backup pool port must be 1-65535");
      return ESP_OK;
    }
    g_stratum.backup_port = port;
  }
  if (form_value(body, "username", value, sizeof(value))) {
    if (!stratum_identity_text_valid(value, true)) {
      httpd_resp_set_status(req, "400 Bad Request");
      send_plain(req, "Worker name can only use letters, numbers, dot, dash, and underscore");
      return ESP_OK;
    }
    copy_str(g_stratum.username, sizeof(g_stratum.username), value);
  }
  if (form_value(body, "wallet_address", value, sizeof(value))) {
    if (!stratum_identity_text_valid(value, false)) {
      httpd_resp_set_status(req, "400 Bad Request");
      send_plain(req, "Wallet can only use letters, numbers, dot, dash, and underscore");
      return ESP_OK;
    }
    copy_str(g_stratum.wallet, sizeof(g_stratum.wallet), value);
  }
  if (form_value(body, "suggested_difficulty", value, sizeof(value))) {
    if (!stratum_difficulty_text_valid(value, true)) {
      httpd_resp_set_status(req, "400 Bad Request");
      send_plain(req, "suggested difficulty must be a positive decimal number");
      return ESP_OK;
    }
    copy_str(g_stratum.suggested_difficulty, sizeof(g_stratum.suggested_difficulty), value);
  }
  if (!save_stratum_settings()) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    send_plain(req, "Save failed");
    return ESP_OK;
  }
  send_plain(req, "Mining settings saved");
  return ESP_OK;
}

static esp_err_t display_handler(httpd_req_t* req) {
  char body[FORM_BODY_MAX];
  char value[8];
  read_body(req, body, sizeof(body));
  const display_settings_t previous = g_display;
  g_display.flip_vertical = form_checked(body, "flip_vertical") ? 1 : 0;
  g_display.screensaver = form_checked(body, "runtime_screensaver") ? 1 : 0;
  set_color_theme(form_checked(body, "display_light_mode") ? "light" : "dark", true);
  if (form_value(body, "display_brightness", value, sizeof(value))) {
    g_display.brightness_pct = parse_percent_or_default(value, previous.brightness_pct);
  }
  nvs_handle_t nvs;
  if (nvs_open("display", NVS_READWRITE, &nvs) == ESP_OK) {
    nvs_set_u8(nvs, "flip", g_display.flip_vertical);
    nvs_set_u8(nvs, "screensaver", g_display.screensaver);
    nvs_set_u8(nvs, "light", g_display.light_mode);
    nvs_set_u8(nvs, "brightness", g_display.brightness_pct);
    nvs_commit(nvs);
    nvs_close(nvs);
  }
  if (previous.flip_vertical != g_display.flip_vertical ||
      previous.screensaver != g_display.screensaver ||
      previous.light_mode != g_display.light_mode ||
      previous.brightness_pct != g_display.brightness_pct) {
    request_display_refresh();
  }
  send_plain(req, "Display settings saved");
  return ESP_OK;
}

static esp_err_t system_handler(httpd_req_t* req) {
  char body[FORM_BODY_MAX];
  read_body(req, body, sizeof(body));
  const system_settings_t previous = g_system;
  g_system.performance_mode = form_checked(body, "performance_mode") ? 1 : 0;
  if (!save_system_settings()) {
    g_system = previous;
    httpd_resp_set_status(req, "500 Internal Server Error");
    send_plain(req, "Device settings save failed");
    return ESP_OK;
  }
  apply_performance_mode();
  send_plain(req, "Device settings saved");
  return ESP_OK;
}

static esp_err_t theme_handler(httpd_req_t* req) {
  char body[FORM_BODY_MAX];
  char value[24];
  read_body(req, body, sizeof(body));
  if (form_value(body, "theme", value, sizeof(value))) {
    const display_settings_t previous = g_display;
    set_color_theme(value, true);
    if (previous.light_mode != g_display.light_mode) {
      request_display_refresh();
    }
  }
  send_plain(req, normalized_theme(g_theme));
  return ESP_OK;
}

static esp_err_t reboot_handler(httpd_req_t* req) {
  char body[64];
  read_body(req, body, sizeof(body));
  send_plain(req, "Rebooting");
  vTaskDelay(pdMS_TO_TICKS(300));
  esp_restart();
  return ESP_OK;
}

static esp_err_t factory_reset_handler(httpd_req_t* req) {
  char body[64];
  read_body(req, body, sizeof(body));
  send_plain(req, "Reset complete. Rebooting...");
  vTaskDelay(pdMS_TO_TICKS(300));
  nvs_flash_erase();
  esp_restart();
  return ESP_OK;
}
