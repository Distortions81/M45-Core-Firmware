/* Private implementation fragment included by web_server.c. */

static esp_err_t captive_portal_redirect_handler(httpd_req_t* req) {
  char location[48];
  if (wifi_setup_ap_active()) {
    snprintf(location, sizeof(location), "http://%s/", g_ap_ip_text);
  } else {
    copy_str(location, sizeof(location), "/");
  }
  httpd_resp_set_status(req, "302 Temporary Redirect");
  httpd_resp_set_hdr(req, "Location", location);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_sendstr(req, "<html><body>Redirect to the captive portal</body></html>");
}

static esp_err_t redirect_handler(httpd_req_t* req) {
  return captive_portal_redirect_handler(req);
}

static void register_uri_or_log(httpd_handle_t server, const httpd_uri_t* uri) {
  const esp_err_t result = httpd_register_uri_handler(server, uri);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register %s: %s", uri->uri, esp_err_to_name(result));
  }
}

void start_http_server(void) {
  APP_SERIAL_LOGF("web: starting http server\n");
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  // Do not trim these casually. Mobile browsers and chunked settings pages
  // need the handler/socket headroom more than we need the small RAM savings.
  config.max_uri_handlers = 32;
  config.max_open_sockets = 6;
  config.stack_size = HTTP_SERVER_STACK_BYTES;

  httpd_handle_t server = NULL;
  const esp_err_t start_result = httpd_start(&server, &config);
  if (start_result != ESP_OK) {
    APP_SERIAL_LOGF("web: http server start failed %s\n", esp_err_to_name(start_result));
    ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(start_result));
    return;
  }
  APP_SERIAL_LOGF("web: http server started\n");

  const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
  const httpd_uri_t health = {.uri = "/health", .method = HTTP_GET, .handler = health_handler};
  const httpd_uri_t wifi = {.uri = "/wifi", .method = HTTP_GET, .handler = wifi_page_handler};
  const httpd_uri_t settings = {.uri = "/settings", .method = HTTP_GET, .handler = settings_page_handler};
  const httpd_uri_t licenses = {
      .uri = "/licenses", .method = HTTP_GET, .handler = licenses_handler};
  const httpd_uri_t mit_license = {
      .uri = "/licenses/mit.txt", .method = HTTP_GET, .handler = mit_license_handler};
  const httpd_uri_t apache_license = {
      .uri = "/licenses/apache-2.0.txt", .method = HTTP_GET, .handler = apache_license_handler};
  const httpd_uri_t qrcode_license = {
      .uri = "/licenses/qrcode.txt", .method = HTTP_GET, .handler = qrcode_license_handler};
  const httpd_uri_t stats_page = {.uri = "/stats", .method = HTTP_GET, .handler = stats_page_handler};
  const httpd_uri_t stats = {.uri = "/api/stats", .method = HTTP_GET, .handler = stats_handler};
  const httpd_uri_t memory = {.uri = "/api/memory", .method = HTTP_GET, .handler = memory_handler};
  const httpd_uri_t networks = {
      .uri = "/api/networks", .method = HTTP_GET, .handler = networks_handler};
  const httpd_uri_t configure = {
      .uri = "/api/configure", .method = HTTP_POST, .handler = configure_handler};
  const httpd_uri_t stratum = {
      .uri = "/api/stratum", .method = HTTP_POST, .handler = stratum_handler};
  const httpd_uri_t display = {
      .uri = "/api/display", .method = HTTP_POST, .handler = display_handler};
  const httpd_uri_t system = {
      .uri = "/api/system", .method = HTTP_POST, .handler = system_handler};
  const httpd_uri_t theme = {
      .uri = "/api/theme", .method = HTTP_POST, .handler = theme_handler};
  const httpd_uri_t reboot = {
      .uri = "/api/reboot", .method = HTTP_POST, .handler = reboot_handler};
  const httpd_uri_t factory_reset = {
      .uri = "/api/factory-reset", .method = HTTP_POST, .handler = factory_reset_handler};
  const httpd_uri_t android_generate_204 = {
      .uri = "/generate_204", .method = HTTP_GET, .handler = captive_portal_redirect_handler};
  const httpd_uri_t android_gen_204 = {
      .uri = "/gen_204", .method = HTTP_GET, .handler = captive_portal_redirect_handler};
  const httpd_uri_t apple_hotspot_detect = {
      .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_portal_redirect_handler};
  const httpd_uri_t apple_success = {
      .uri = "/library/test/success.html", .method = HTTP_GET, .handler = captive_portal_redirect_handler};
  const httpd_uri_t windows_connecttest = {
      .uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_portal_redirect_handler};
  const httpd_uri_t windows_redirect = {
      .uri = "/redirect", .method = HTTP_GET, .handler = captive_portal_redirect_handler};
  const httpd_uri_t windows_ncsi = {
      .uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_portal_redirect_handler};
  const httpd_uri_t firefox_canonical = {
      .uri = "/canonical.html", .method = HTTP_GET, .handler = captive_portal_redirect_handler};
  const httpd_uri_t catch_all = {.uri = "/*", .method = HTTP_GET, .handler = redirect_handler};

  register_uri_or_log(server, &root);
  register_uri_or_log(server, &health);
  register_uri_or_log(server, &wifi);
  register_uri_or_log(server, &settings);
  register_uri_or_log(server, &licenses);
  register_uri_or_log(server, &mit_license);
  register_uri_or_log(server, &apache_license);
  register_uri_or_log(server, &qrcode_license);
  register_uri_or_log(server, &stats_page);
  register_uri_or_log(server, &stats);
  register_uri_or_log(server, &memory);
  register_uri_or_log(server, &networks);
  register_uri_or_log(server, &configure);
  register_uri_or_log(server, &stratum);
  register_uri_or_log(server, &display);
  register_uri_or_log(server, &system);
  register_uri_or_log(server, &theme);
  register_uri_or_log(server, &reboot);
  register_uri_or_log(server, &factory_reset);
  register_uri_or_log(server, &android_generate_204);
  register_uri_or_log(server, &android_gen_204);
  register_uri_or_log(server, &apple_hotspot_detect);
  register_uri_or_log(server, &apple_success);
  register_uri_or_log(server, &windows_connecttest);
  register_uri_or_log(server, &windows_redirect);
  register_uri_or_log(server, &windows_ncsi);
  register_uri_or_log(server, &firefox_canonical);
  register_uri_or_log(server, &catch_all);
  ESP_LOGI(TAG, "HTTP server started");
  APP_SERIAL_LOGF("web: handlers registered\n");
}
