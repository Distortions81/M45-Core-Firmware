/* Private implementation fragment included by web_server.c. */

static void send_plain(httpd_req_t* req, const char* text);

static void url_decode(char* text) {
  char* write = text;
  for (char* read = text; *read != '\0'; ++read) {
    if (*read == '+') {
      *write++ = ' ';
    } else if (*read == '%') {
      if (read[1] == '\0' || read[2] == '\0') {
        *write++ = *read;
        continue;
      }
      const int hi = app_hex_value(read[1]);
      const int lo = app_hex_value(read[2]);
      if (hi < 0 || lo < 0) {
        *write++ = *read;
        continue;
      }
      *write++ = (char)((hi << 4) | lo);
      read += 2;
    } else {
      *write++ = *read;
    }
  }
  *write = '\0';
}

static void drain_body(httpd_req_t* req, int remaining) {
  char scratch[64];
  while (remaining > 0) {
    const int read_len = httpd_req_recv(req, scratch, MIN(remaining, (int)sizeof(scratch)));
    if (read_len <= 0) {
      break;
    }
    remaining -= read_len;
  }
}

static bool read_body(httpd_req_t* req, char* body, size_t body_size) {
  if (body_size == 0) {
    return false;
  }
  int remaining = req->content_len;
  if (remaining < 0 || (size_t)remaining >= body_size) {
    body[0] = '\0';
    drain_body(req, remaining);
    return false;
  }
  size_t offset = 0;
  while (remaining > 0 && offset + 1 < body_size) {
    const int chunk = MIN(remaining, (int)(body_size - 1 - offset));
    const int read_len = httpd_req_recv(req, body + offset, chunk);
    if (read_len <= 0) {
      body[offset] = '\0';
      drain_body(req, remaining);
      return false;
    }
    offset += read_len;
    remaining -= read_len;
  }
  body[offset] = '\0';
  return remaining == 0;
}

static bool header_ascii_equal(char a, char b) {
  if (a >= 'A' && a <= 'Z') {
    a = (char)(a - 'A' + 'a');
  }
  if (b >= 'A' && b <= 'Z') {
    b = (char)(b - 'A' + 'a');
  }
  return a == b;
}

static bool header_ascii_starts_with(const char* value, const char* prefix) {
  while (*prefix != '\0') {
    if (*value == '\0' || !header_ascii_equal(*value, *prefix)) {
      return false;
    }
    ++value;
    ++prefix;
  }
  return true;
}

static bool header_host_matches(const char* header, const char* host) {
  const char* candidate = header;
  const char* scheme = strstr(header, "://");
  if (scheme != NULL) {
    candidate = scheme + 3;
  }
  const char* end = strpbrk(candidate, "/?#");
  const size_t candidate_len = end == NULL ? strlen(candidate) : (size_t)(end - candidate);
  if (strlen(host) != candidate_len) {
    return false;
  }
  for (size_t i = 0; i < candidate_len; ++i) {
    if (!header_ascii_equal(candidate[i], host[i])) {
      return false;
    }
  }
  return true;
}

static bool request_same_origin(httpd_req_t* req) {
  char host[64];
  char origin[96];
  char referer[128];
  const size_t host_len = httpd_req_get_hdr_value_len(req, "Host");
  if (host_len == 0) {
    return true;
  }
  if (host_len >= sizeof(host) ||
      httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
    return false;
  }
  const size_t origin_len = httpd_req_get_hdr_value_len(req, "Origin");
  if (origin_len > 0) {
    if (origin_len >= sizeof(origin) ||
        httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK) {
      return false;
    }
    return header_host_matches(origin, host);
  }
  const size_t referer_len = httpd_req_get_hdr_value_len(req, "Referer");
  if (referer_len > 0) {
    if (referer_len >= sizeof(referer) ||
        httpd_req_get_hdr_value_str(req, "Referer", referer, sizeof(referer)) != ESP_OK) {
      return false;
    }
    return header_host_matches(referer, host);
  }
  return true;
}

static bool request_has_form_content_type(httpd_req_t* req) {
  char content_type[80];
  const size_t content_type_len = httpd_req_get_hdr_value_len(req, "Content-Type");
  if (content_type_len == 0 || content_type_len >= sizeof(content_type)) {
    return false;
  }
  if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK) {
    return false;
  }
  return header_ascii_starts_with(content_type, "application/x-www-form-urlencoded");
}

static bool read_mutating_form_body(httpd_req_t* req, char* body, size_t body_size) {
  if (!request_same_origin(req)) {
    httpd_resp_set_status(req, "403 Forbidden");
    send_plain(req, "Cross-origin request rejected");
    drain_body(req, req->content_len);
    return false;
  }
  if (!request_has_form_content_type(req)) {
    httpd_resp_set_status(req, "415 Unsupported Media Type");
    send_plain(req, "Content-Type must be application/x-www-form-urlencoded");
    drain_body(req, req->content_len);
    return false;
  }
  if (!read_body(req, body, body_size)) {
    httpd_resp_set_status(req, "413 Payload Too Large");
    send_plain(req, "Request body is too large");
    return false;
  }
  return true;
}

static bool form_value(const char* body, const char* key, char* out, size_t out_size) {
  if (out_size == 0) {
    return false;
  }
  const size_t key_len = strlen(key);
  const char* cursor = body;
  out[0] = '\0';
  while (*cursor != '\0') {
    const char* next = strchr(cursor, '&');
    const size_t field_len = next == NULL ? strlen(cursor) : (size_t)(next - cursor);
    const char* equals = memchr(cursor, '=', field_len);
    if (equals != NULL && (size_t)(equals - cursor) == key_len && memcmp(cursor, key, key_len) == 0) {
      const size_t value_len = field_len - key_len - 1;
      const size_t copy_len = MIN(value_len, out_size - 1);
      memcpy(out, equals + 1, copy_len);
      out[copy_len] = '\0';
      url_decode(out);
      return true;
    }
    if (next == NULL) {
      break;
    }
    cursor = next + 1;
  }
  return false;
}

static void strip_html_markup(char* text) {
  char* write = text;
  bool in_tag = false;
  for (char* read = text; *read != '\0'; ++read) {
    const unsigned char ch = (unsigned char)*read;
    if (ch == '<') {
      in_tag = true;
      continue;
    }
    if (ch == '>') {
      in_tag = false;
      continue;
    }
    if (in_tag || ch < 0x20 || ch == 0x7f) {
      continue;
    }
    *write++ = *read;
  }
  *write = '\0';
}

static bool form_text_value(const char* body, const char* key, char* out, size_t out_size) {
  if (!form_value(body, key, out, out_size)) {
    return false;
  }
  strip_html_markup(out);
  return true;
}

static bool form_value_matches_token(const char* body, const char* key, const char* expected) {
  char actual[40];
  if (!form_value(body, key, actual, sizeof(actual)) || expected == NULL) {
    return false;
  }
  const size_t actual_len = strlen(actual);
  const size_t expected_len = strlen(expected);
  unsigned diff = (unsigned)(actual_len ^ expected_len);
  const size_t compare_len = actual_len > expected_len ? actual_len : expected_len;
  for (size_t i = 0; i < compare_len; ++i) {
    const unsigned actual_ch = i < actual_len ? (unsigned)(uint8_t)actual[i] : 0U;
    const unsigned expected_ch = i < expected_len ? (unsigned)(uint8_t)expected[i] : 0U;
    diff |= actual_ch ^ expected_ch;
  }
  return diff == 0U;
}

static bool require_action_token(httpd_req_t* req, const char* body) {
  if (form_value_matches_token(body, "action_token", action_token())) {
    return true;
  }
  httpd_resp_set_status(req, "403 Forbidden");
  send_plain(req, "Action confirmation token is invalid");
  return false;
}

static bool form_checked(const char* body, const char* key) {
  char value[8];
  return form_value(body, key, value, sizeof(value)) && strcmp(value, "1") == 0;
}

static uint8_t parse_percent_or_default(const char* text, uint8_t fallback) {
  if (text == NULL || text[0] == '\0') {
    return fallback;
  }
  char* end = NULL;
  const long value = strtol(text, &end, 10);
  if (end == text || *end != '\0') {
    return fallback;
  }
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return (uint8_t)value;
}

static uint16_t parse_display_sleep_minutes_or_default(const char* text, uint16_t fallback) {
  if (text == NULL || text[0] == '\0') {
    return fallback;
  }
  char* end = NULL;
  const long value = strtol(text, &end, 10);
  if (end != text && *end != '\0') {
    char* decimal_end = NULL;
    const double decimal_value = strtod(text, &decimal_end);
    if (decimal_end != text && *decimal_end == '\0' && decimal_value < 1.0) {
      return 0;
    }
  }
  if (end == text || *end != '\0') {
    return fallback;
  }
  if (value < 1) {
    return 0;
  }
  if (value > DISPLAY_SLEEP_MAX_MINUTES) {
    return DISPLAY_SLEEP_MAX_MINUTES;
  }
  return (uint16_t)value;
}

static void send_plain(httpd_req_t* req, const char* text) {
  httpd_resp_set_type(req, "text/plain");
  set_common_response_headers(req);
  httpd_resp_sendstr(req, text);
}
