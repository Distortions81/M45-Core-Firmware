/* Private implementation fragment included by web_server.c. */

static void set_common_response_headers(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(req,
                     "Content-Security-Policy",
                     "default-src 'self'; base-uri 'none'; object-src 'none'; frame-ancestors 'none'; "
                     "form-action 'self'; connect-src 'self'; img-src 'self' data:; "
                     "script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'");
}

static void set_text_response_headers(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

static void close_http_session_after_response(httpd_req_t* req) {
  const int sockfd = httpd_req_to_sockfd(req);
  if (sockfd >= 0) {
    (void)httpd_sess_update_lru_counter(req->handle, sockfd);
    (void)httpd_sess_trigger_close(req->handle, sockfd);
  }
}

static esp_err_t send_response_and_close(httpd_req_t* req, const char* data, ssize_t len) {
  httpd_resp_set_hdr(req, "Connection", "close");
  const esp_err_t result = httpd_resp_send(req, data, len);
  close_http_session_after_response(req);
  return result;
}

static esp_err_t send_response_str_and_close(httpd_req_t* req, const char* text) {
  httpd_resp_set_hdr(req, "Connection", "close");
  const esp_err_t result = httpd_resp_sendstr(req, text);
  close_http_session_after_response(req);
  return result;
}

static const char* action_token(void) {
  static const char hex[] = "0123456789abcdef";
  if (g_action_token[0] == '\0') {
    for (size_t i = 0; i < 16; ++i) {
      const uint8_t byte = (uint8_t)esp_random();
      g_action_token[i * 2] = hex[byte >> 4];
      g_action_token[i * 2 + 1] = hex[byte & 0x0f];
    }
    g_action_token[32] = '\0';
  }
  return g_action_token;
}

static esp_err_t send_gzip_asset(
    httpd_req_t* req, const char* content_type, const unsigned char* data, unsigned int len) {
  httpd_resp_set_type(req, content_type);
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
  return send_response_and_close(req, (const char*)data, (ssize_t)len);
}

static esp_err_t send_gzip_page(httpd_req_t* req, const unsigned char* data, unsigned int len) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  set_common_response_headers(req);
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return send_response_and_close(req, (const char*)data, (ssize_t)len);
}

static esp_err_t send_gzip_text(httpd_req_t* req, const unsigned char* data, unsigned int len) {
  return send_gzip_asset(req, "text/plain; charset=utf-8", data, len);
}
