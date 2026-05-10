/* Private implementation fragment included by web_server.c. */

static esp_err_t setup_page_handler(httpd_req_t* req) {
  return send_gzip_page(req, WEB_SETUP_PAGE_HTML_GZ, WEB_SETUP_PAGE_HTML_GZ_LEN);
}

static esp_err_t root_handler(httpd_req_t* req) {
  return g_sta_connected ? stats_page_handler(req) : setup_page_handler(req);
}

static esp_err_t wifi_page_handler(httpd_req_t* req) {
  return setup_page_handler(req);
}

static esp_err_t settings_page_handler(httpd_req_t* req) {
  return send_gzip_page(req, WEB_SETTINGS_PAGE_HTML_GZ, WEB_SETTINGS_PAGE_HTML_GZ_LEN);
}

static esp_err_t stats_page_handler(httpd_req_t* req) {
  return send_gzip_page(req, WEB_STATS_PAGE_HTML_GZ, WEB_STATS_PAGE_HTML_GZ_LEN);
}

static esp_err_t licenses_handler(httpd_req_t* req) {
  return send_gzip_page(req, WEB_LICENSES_PAGE_HTML_GZ, WEB_LICENSES_PAGE_HTML_GZ_LEN);
}

static esp_err_t styles_css_handler(httpd_req_t* req) {
  return send_gzip_asset(req, "text/css; charset=utf-8", WEB_STYLES_CSS_GZ, WEB_STYLES_CSS_GZ_LEN);
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
