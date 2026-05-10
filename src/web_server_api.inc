/* Private implementation fragment included by web_server.c. */

static bool urlencoded_is_unreserved(char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
         ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

static void append_response_text(char* out, size_t out_size, size_t* offset, const char* text) {
  if (out_size == 0 || *offset >= out_size) {
    return;
  }
  if (text == NULL) {
    text = "";
  }
  while (*text != '\0' && *offset + 1 < out_size) {
    out[(*offset)++] = *text++;
  }
  out[*offset] = '\0';
}

static void append_urlencoded_value(char* out, size_t out_size, size_t* offset, const char* value) {
  static const char hex[] = "0123456789ABCDEF";
  if (value == NULL) {
    value = "";
  }
  while (*value != '\0' && *offset + 1 < out_size) {
    const unsigned char ch = (unsigned char)*value++;
    if (urlencoded_is_unreserved((char)ch)) {
      out[(*offset)++] = (char)ch;
    } else if (ch == ' ') {
      out[(*offset)++] = '+';
    } else {
      if (*offset + 3 >= out_size) {
        break;
      }
      out[(*offset)++] = '%';
      out[(*offset)++] = hex[ch >> 4];
      out[(*offset)++] = hex[ch & 0x0f];
    }
  }
  out[*offset] = '\0';
}

static void append_urlencoded_field(
    char* out, size_t out_size, size_t* offset, const char* key, const char* value) {
  if (*offset > 0) {
    append_response_text(out, out_size, offset, "&");
  }
  append_response_text(out, out_size, offset, key);
  append_response_text(out, out_size, offset, "=");
  append_urlencoded_value(out, out_size, offset, value);
}

static void append_urlencoded_uint(
    char* out, size_t out_size, size_t* offset, const char* key, unsigned value) {
  char text[16];
  snprintf(text, sizeof(text), "%u", value);
  append_urlencoded_field(out, out_size, offset, key, text);
}

static esp_err_t stats_handler(httpd_req_t* req) {
  char line[768];
  char uptime[16];
  char endpoint[128];
  char shares_per_min[16];
  char hashrate_average[64];
  char payout_percent[16];
  char last_block_nonce[16];
  uptime_text(uptime, sizeof(uptime));
  pool_endpoint_text(endpoint, sizeof(endpoint));
  shares_per_min_text(shares_per_min, sizeof(shares_per_min));
  format_hashrate_average_text(hashrate_average, sizeof(hashrate_average));
  format_payout_percent_text(payout_percent, sizeof(payout_percent));
  snprintf(last_block_nonce,
           sizeof(last_block_nonce),
           "0x%08lx",
           (unsigned long)g_stratum_runtime.last_found_block_nonce);
  snprintf(line,
           sizeof(line),
           "%s,%s,%s,%s,%u,%s,%s,%u,%u,%s,%s,%u,%s,%u,%s,%u\n",
           uptime,
           pool_status_text(),
           endpoint,
           shares_per_min,
           (unsigned)g_stratum_runtime.hashes_per_second,
           hashrate_average,
           current_difficulty_text(),
           (unsigned)g_stratum_runtime.submitted,
           (unsigned)g_stratum_runtime.rejected,
           payout_status_id(),
           payout_percent,
           (unsigned)g_stratum_runtime.found_blocks,
           last_block_nonce,
           (unsigned)g_stratum_runtime.last_found_block_share_difficulty,
           g_stratum_runtime.last_found_block_hash,
           (unsigned)g_system.block_alerts);
  httpd_resp_set_type(req, "text/csv");
  set_text_response_headers(req);
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
  set_text_response_headers(req);
  return httpd_resp_sendstr(req, line);
}

static esp_err_t ui_handler(httpd_req_t* req) {
  char line[160];
  char device_name[48];
  device_name_text(device_name, sizeof(device_name));
  snprintf(line,
           sizeof(line),
           "%s,%s,%s,%s\n",
           normalized_theme(g_theme),
           device_name,
           APP_BUILD_ID,
           APP_BUILD_TIME_UTC);
  httpd_resp_set_type(req, "text/csv");
  set_text_response_headers(req);
  return httpd_resp_sendstr(req, line);
}

static esp_err_t settings_data_handler(httpd_req_t* req) {
  char line[2048];
  char device_name[48];
  size_t offset = 0;
  device_name_text(device_name, sizeof(device_name));
  line[0] = '\0';
  append_urlencoded_field(line, sizeof(line), &offset, "theme", normalized_theme(g_theme));
  append_urlencoded_field(line, sizeof(line), &offset, "device_name", device_name);
  append_urlencoded_field(line,
                          sizeof(line),
                          &offset,
                          "sta_ssid",
                          g_wifi.ssid[0] != '\0' ? g_wifi.ssid : "Not configured");
  append_urlencoded_field(line,
                          sizeof(line),
                          &offset,
                          "sta_ip",
                          g_sta_ip[0] != '\0' ? g_sta_ip : "Not connected");
  append_urlencoded_uint(line, sizeof(line), &offset, "display_flip_vertical", g_display.flip_vertical);
  append_urlencoded_uint(line, sizeof(line), &offset, "display_runtime_screensaver", g_display.screensaver);
  append_urlencoded_uint(line,
                         sizeof(line),
                         &offset,
                         "display_light_mode",
                         strcmp(normalized_theme(g_theme), "light") == 0 ? 1U : 0U);
  append_urlencoded_uint(line, sizeof(line), &offset, "display_text_outline", g_display.text_outline);
  append_urlencoded_uint(line, sizeof(line), &offset, "display_brightness", g_display.brightness_pct);
  append_urlencoded_uint(line,
                         sizeof(line),
                         &offset,
                         "display_sleep_minutes",
                         (unsigned)g_display.sleep_timeout_minutes);
  append_urlencoded_uint(line,
                         sizeof(line),
                         &offset,
                         "display_sleep_max_minutes",
                         (unsigned)DISPLAY_SLEEP_MAX_MINUTES);
  append_urlencoded_uint(line, sizeof(line), &offset, "performance_mode", g_system.performance_mode);
  append_urlencoded_uint(line, sizeof(line), &offset, "block_alerts", g_system.block_alerts);
  append_urlencoded_field(line, sizeof(line), &offset, "stratum_primary_server", g_stratum.primary_host);
  append_urlencoded_uint(line, sizeof(line), &offset, "stratum_primary_port", g_stratum.primary_port);
  append_urlencoded_field(line, sizeof(line), &offset, "stratum_backup_server", g_stratum.backup_host);
  append_urlencoded_uint(line, sizeof(line), &offset, "stratum_backup_port", g_stratum.backup_port);
  append_urlencoded_field(line, sizeof(line), &offset, "stratum_username", g_stratum.username);
  append_urlencoded_field(line, sizeof(line), &offset, "stratum_wallet_address", g_stratum.wallet);
  append_urlencoded_field(line, sizeof(line), &offset, "action_token", action_token());
  append_response_text(line, sizeof(line), &offset, "\n");
  httpd_resp_set_type(req, "application/x-www-form-urlencoded; charset=utf-8");
  set_text_response_headers(req);
  return httpd_resp_sendstr(req, line);
}

static esp_err_t setup_data_handler(httpd_req_t* req) {
  char line[1024];
  size_t offset = 0;
  line[0] = '\0';
  append_urlencoded_field(line, sizeof(line), &offset, "theme", normalized_theme(g_theme));
  append_urlencoded_field(line, sizeof(line), &offset, "page_title", g_sta_connected ? "Wi-Fi" : "Device setup");
  append_urlencoded_field(line, sizeof(line), &offset, "ssid", g_ap_ssid);
  append_urlencoded_field(line,
                          sizeof(line),
                          &offset,
                          "device_ip",
                          g_sta_connected && g_sta_ip[0] != '\0' ? g_sta_ip : g_ap_ip_text);
  append_urlencoded_field(line,
                          sizeof(line),
                          &offset,
                          "page_intro",
                          g_sta_connected
                              ? "Choose a different Wi-Fi network or enter one manually."
                              : "Choose a Wi-Fi network to connect this device.");
  append_urlencoded_field(line, sizeof(line), &offset, "saved_wifi_ssid", g_wifi.ssid);
  append_urlencoded_field(line,
                          sizeof(line),
                          &offset,
                          "save_button_label",
                          g_sta_connected ? "Save and reconnect" : "Save & reboot");
  append_urlencoded_field(line,
                          sizeof(line),
                          &offset,
                          "save_hint",
                          g_sta_connected
                              ? "Save settings to reboot and reconnect to the selected Wi-Fi."
                              : "The device will test the Wi-Fi connection before saving and rebooting.");
  append_urlencoded_field(line, sizeof(line), &offset, "action_token", action_token());
  append_response_text(line, sizeof(line), &offset, "\n");
  httpd_resp_set_type(req, "application/x-www-form-urlencoded; charset=utf-8");
  set_text_response_headers(req);
  return httpd_resp_sendstr(req, line);
}

static esp_err_t health_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/plain");
  set_text_response_headers(req);
  return httpd_resp_sendstr(req, "ok");
}

static void append_network_ssid_field(char* out, size_t out_size, size_t* offset, const uint8_t* ssid, size_t ssid_size) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t ssid_len = 0;
  while (ssid_len < ssid_size && ssid[ssid_len] != '\0') {
    ssid_len++;
  }
  for (size_t i = 0; i < ssid_len; i += 3) {
    if (*offset + 4 >= out_size) {
      break;
    }
    const uint32_t b0 = ssid[i];
    const uint32_t b1 = i + 1 < ssid_len ? ssid[i + 1] : 0;
    const uint32_t b2 = i + 2 < ssid_len ? ssid[i + 2] : 0;
    const uint32_t bits = (b0 << 16) | (b1 << 8) | b2;
    out[(*offset)++] = alphabet[(bits >> 18) & 0x3f];
    out[(*offset)++] = alphabet[(bits >> 12) & 0x3f];
    out[(*offset)++] = i + 1 < ssid_len ? alphabet[(bits >> 6) & 0x3f] : '=';
    out[(*offset)++] = i + 2 < ssid_len ? alphabet[bits & 0x3f] : '=';
  }
  out[*offset] = '\0';
}

static bool networks_cache_snapshot(char* out, size_t out_size, int64_t now, bool require_fresh) {
  bool available = false;
  if (out_size == 0) {
    return false;
  }
  out[0] = '\0';
  portENTER_CRITICAL(&g_networks_cache_mux);
  const bool fresh = g_networks_cache_valid && now - g_networks_cache_us < NETWORK_SCAN_CACHE_TTL_US;
  if (g_networks_cache_valid && (!require_fresh || fresh)) {
    const size_t copy_len = MIN(g_networks_cache_len, out_size - 1);
    memcpy(out, g_networks_cache, copy_len);
    out[copy_len] = '\0';
    available = true;
  }
  portEXIT_CRITICAL(&g_networks_cache_mux);
  return available;
}

static void networks_cache_store(const char* cache) {
  portENTER_CRITICAL(&g_networks_cache_mux);
  g_networks_cache_len = strnlen(cache, sizeof(g_networks_cache) - 1);
  memcpy(g_networks_cache, cache, g_networks_cache_len);
  g_networks_cache[g_networks_cache_len] = '\0';
  g_networks_cache_us = esp_timer_get_time();
  g_networks_cache_valid = true;
  portEXIT_CRITICAL(&g_networks_cache_mux);
}

static void networks_cache_clear(void) {
  portENTER_CRITICAL(&g_networks_cache_mux);
  g_networks_cache[0] = '\0';
  g_networks_cache_len = 0;
  g_networks_cache_us = 0;
  g_networks_cache_valid = false;
  portEXIT_CRITICAL(&g_networks_cache_mux);
}

static void network_scan_task_finish(void) {
  portENTER_CRITICAL(&g_network_scan_mux);
  g_network_scan_in_progress = false;
  portEXIT_CRITICAL(&g_network_scan_mux);
}

static void network_scan_task(void* arg) {
  (void)arg;
  wifi_scan_config_t scan_config = {
      .ssid = NULL,
      .bssid = NULL,
      .channel = 0,
      .show_hidden = false,
      .scan_type = WIFI_SCAN_TYPE_ACTIVE,
  };
  esp_err_t err = esp_wifi_scan_start(&scan_config, true);
  uint16_t count = 0;
  wifi_ap_record_t records[NETWORK_SCAN_MAX_APS];
  memset(records, 0, sizeof(records));
  if (err == ESP_OK) {
    err = esp_wifi_scan_get_ap_num(&count);
  }
  if (err == ESP_OK) {
    if (count > NETWORK_SCAN_MAX_APS) {
      count = NETWORK_SCAN_MAX_APS;
    }
    if (count > 0) {
      err = esp_wifi_scan_get_ap_records(&count, records);
    }
  }

  if (err == ESP_OK) {
    for (uint16_t i = 0; i < count; ++i) {
      for (uint16_t j = i + 1; j < count; ++j) {
        if (records[j].rssi > records[i].rssi) {
          const wifi_ap_record_t tmp = records[i];
          records[i] = records[j];
          records[j] = tmp;
        }
      }
    }

    char cache[sizeof(g_networks_cache)];
    size_t offset = 0;
    cache[0] = '\0';
    for (uint16_t i = 0; i < count && offset + 1 < sizeof(cache); ++i) {
      append_network_ssid_field(cache,
                                sizeof(cache),
                                &offset,
                                records[i].ssid,
                                sizeof(records[i].ssid));
      offset += snprintf(cache + offset,
                         sizeof(cache) - offset,
                         "\t%d\t%d\t%d\n",
                         records[i].rssi,
                         records[i].primary,
                         records[i].authmode == WIFI_AUTH_OPEN ? 1 : 0);
      if (offset >= sizeof(cache)) {
        cache[sizeof(cache) - 1] = '\0';
        break;
      }
    }
    networks_cache_store(cache);
  } else {
    ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
  }

  network_scan_task_finish();
  vTaskDelete(NULL);
}

static bool start_network_scan_task(void) {
  portENTER_CRITICAL(&g_network_scan_mux);
  if (g_network_scan_in_progress) {
    portEXIT_CRITICAL(&g_network_scan_mux);
    return true;
  }
  g_network_scan_in_progress = true;
  portEXIT_CRITICAL(&g_network_scan_mux);

  const BaseType_t created = xTaskCreatePinnedToCore(network_scan_task,
                                                     "wifi_scan",
                                                     NETWORK_SCAN_TASK_STACK_BYTES,
                                                     NULL,
                                                     NETWORK_SCAN_TASK_PRIORITY,
                                                     NULL,
                                                     NETWORK_SCAN_TASK_CORE_ID);
  if (created != pdPASS) {
    network_scan_task_finish();
    ESP_LOGW(TAG, "Failed to start Wi-Fi scan task");
    return false;
  }
  return true;
}

static esp_err_t networks_handler(httpd_req_t* req) {
  const int64_t now = esp_timer_get_time();
  char cached[sizeof(g_networks_cache)];
  if (networks_cache_snapshot(cached, sizeof(cached), now, true)) {
    httpd_resp_set_type(req, "text/plain");
    set_text_response_headers(req);
    return httpd_resp_sendstr(req, cached);
  }

  if (!start_network_scan_task()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    send_plain(req, "Wi-Fi scan unavailable");
    return ESP_OK;
  }

  if (networks_cache_snapshot(cached, sizeof(cached), now, false)) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "X-Scan-Status", "refreshing");
    set_text_response_headers(req);
    return httpd_resp_sendstr(req, cached);
  }

  httpd_resp_set_status(req, "202 Accepted");
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Retry-After", "1");
  set_text_response_headers(req);
  return httpd_resp_sendstr(req, "scan\n");
}

static esp_err_t configure_handler(httpd_req_t* req) {
  char body[FORM_BODY_MAX];
  char ssid[33];
  char password[65];
  char connect_error[160];
  if (!read_mutating_form_body(req, body, sizeof(body))) {
    return ESP_OK;
  }
  form_text_value(body, "ssid", ssid, sizeof(ssid));
  form_value(body, "password", password, sizeof(password));
  if (ssid[0] == '\0') {
    httpd_resp_set_status(req, "400 Bad Request");
    send_plain(req, "Wi-Fi name is required");
    return ESP_OK;
  }
  const bool setup_mode = wifi_setup_ap_active() && !g_sta_connected;
  if (setup_mode && !wifi_test_station_credentials(ssid, password, connect_error, sizeof(connect_error))) {
    httpd_resp_set_status(req, "400 Bad Request");
    send_plain(req, connect_error[0] != '\0' ? connect_error : "Wi-Fi connection failed");
    return ESP_OK;
  }
  if (!save_wifi_settings(ssid, password)) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    send_plain(req, "Wi-Fi save failed");
    return ESP_OK;
  }
  networks_cache_clear();
  if (g_sta_connected || setup_mode) {
    send_plain(req, setup_mode ? "Wi-Fi verified. Rebooting now..." : "Wi-Fi saved. Rebooting now...");
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
  if (!read_mutating_form_body(req, body, sizeof(body))) {
    return ESP_OK;
  }
  if (form_text_value(body, "primary_server", value, sizeof(value))) {
    if (!pool_host_text_valid(value, false)) {
      httpd_resp_set_status(req, "400 Bad Request");
      send_plain(req, "Main pool host can only use letters, numbers, dot, and dash");
      return ESP_OK;
    }
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
  if (form_text_value(body, "backup_server", value, sizeof(value))) {
    if (!pool_host_text_valid(value, true)) {
      httpd_resp_set_status(req, "400 Bad Request");
      send_plain(req, "Backup pool host can only use letters, numbers, dot, and dash");
      return ESP_OK;
    }
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
  if (form_text_value(body, "username", value, sizeof(value))) {
    if (!stratum_identity_text_valid(value, true)) {
      httpd_resp_set_status(req, "400 Bad Request");
      send_plain(req, "Worker name can only use letters, numbers, dot, dash, and underscore");
      return ESP_OK;
    }
    copy_str(g_stratum.username, sizeof(g_stratum.username), value);
  }
  if (form_text_value(body, "wallet_address", value, sizeof(value))) {
    if (!stratum_identity_text_valid(value, false)) {
      httpd_resp_set_status(req, "400 Bad Request");
      send_plain(req, "Wallet can only use letters, numbers, dot, dash, and underscore");
      return ESP_OK;
    }
    copy_str(g_stratum.wallet, sizeof(g_stratum.wallet), value);
  }
  if (form_text_value(body, "suggested_difficulty", value, sizeof(value))) {
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
  if (!read_mutating_form_body(req, body, sizeof(body))) {
    return ESP_OK;
  }
  const display_settings_t previous = g_display;
  g_display.flip_vertical = form_checked(body, "flip_vertical") ? 1 : 0;
  g_display.screensaver = form_checked(body, "runtime_screensaver") ? 1 : 0;
  g_display.text_outline = form_checked(body, "display_text_outline") ? 1 : 0;
  set_color_theme(form_checked(body, "display_light_mode") ? "light" : "dark", true);
  if (form_value(body, "display_brightness", value, sizeof(value))) {
    g_display.brightness_pct = parse_percent_or_default(value, previous.brightness_pct);
  }
  if (form_value(body, "display_sleep_minutes", value, sizeof(value))) {
    g_display.sleep_timeout_minutes =
        parse_display_sleep_minutes_or_default(value, previous.sleep_timeout_minutes);
  }
  nvs_handle_t nvs;
  if (nvs_open("display", NVS_READWRITE, &nvs) == ESP_OK) {
    nvs_set_u8(nvs, "flip", g_display.flip_vertical);
    nvs_set_u8(nvs, "screensaver", g_display.screensaver);
    nvs_set_u8(nvs, "light", g_display.light_mode);
    nvs_set_u8(nvs, "brightness", g_display.brightness_pct);
    nvs_set_u8(nvs, "text_outline", g_display.text_outline);
    nvs_set_u16(nvs, "sleep_min", g_display.sleep_timeout_minutes);
    nvs_commit(nvs);
    nvs_close(nvs);
  }
  if (previous.flip_vertical != g_display.flip_vertical ||
      previous.screensaver != g_display.screensaver ||
      previous.light_mode != g_display.light_mode ||
      previous.brightness_pct != g_display.brightness_pct ||
      previous.text_outline != g_display.text_outline ||
      previous.sleep_timeout_minutes != g_display.sleep_timeout_minutes) {
    request_display_refresh();
  }
  send_plain(req, "Display settings saved");
  return ESP_OK;
}

static esp_err_t system_handler(httpd_req_t* req) {
  char body[FORM_BODY_MAX];
  if (!read_mutating_form_body(req, body, sizeof(body))) {
    return ESP_OK;
  }
  const system_settings_t previous = g_system;
  g_system.performance_mode = form_checked(body, "performance_mode") ? 1 : 0;
  g_system.block_alerts = form_checked(body, "block_alerts") ? 1 : 0;
  if (!save_system_settings()) {
    g_system = previous;
    httpd_resp_set_status(req, "500 Internal Server Error");
    send_plain(req, "Device settings save failed");
    return ESP_OK;
  }
  apply_performance_mode();
  if (previous.block_alerts != g_system.block_alerts) {
    request_display_refresh();
  }
  send_plain(req, "Device settings saved");
  return ESP_OK;
}

static esp_err_t theme_handler(httpd_req_t* req) {
  char body[FORM_BODY_MAX];
  char value[24];
  if (!read_mutating_form_body(req, body, sizeof(body))) {
    return ESP_OK;
  }
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
  if (!read_mutating_form_body(req, body, sizeof(body))) {
    return ESP_OK;
  }
  if (!require_action_token(req, body)) {
    return ESP_OK;
  }
  send_plain(req, "Rebooting");
  vTaskDelay(pdMS_TO_TICKS(300));
  esp_restart();
  return ESP_OK;
}

static esp_err_t factory_reset_handler(httpd_req_t* req) {
  char body[64];
  if (!read_mutating_form_body(req, body, sizeof(body))) {
    return ESP_OK;
  }
  if (!require_action_token(req, body)) {
    return ESP_OK;
  }
  send_plain(req, "Reset complete. Rebooting...");
  vTaskDelay(pdMS_TO_TICKS(300));
  nvs_flash_erase();
  esp_restart();
  return ESP_OK;
}
