/* Private implementation fragment included by web_server.c. */

static const char* template_value(const template_value_t* values, size_t count, const char* key, size_t key_len) {
  for (size_t i = 0; i < count; ++i) {
    if (strlen(values[i].key) == key_len && memcmp(values[i].key, key, key_len) == 0) {
      return values[i].value != NULL ? values[i].value : "";
    }
  }
  return "";
}

static size_t rendered_template_len(
    const char* html, const template_value_t* values, size_t count, uint8_t depth) {
  size_t length = 0;
  const char* cursor = html;
  while (*cursor != '\0') {
    const char* open = strstr(cursor, "{{");
    if (open == NULL) {
      return length + strlen(cursor);
    }
    length += (size_t)(open - cursor);
    const char* close = strstr(open + 2, "}}");
    if (close == NULL) {
      return length + strlen(open);
    }
    const char* value = template_value(values, count, open + 2, (size_t)(close - open - 2));
    if (value[0] != '\0') {
      if (depth < 4 && strstr(value, "{{") != NULL) {
        length += rendered_template_len(value, values, count, depth + 1);
      } else {
        length += strlen(value);
      }
    }
    cursor = close + 2;
  }
  return length;
}

static char* render_template_to(
    char* out, const char* html, const template_value_t* values, size_t count, uint8_t depth) {
  const char* cursor = html;
  while (*cursor != '\0') {
    const char* open = strstr(cursor, "{{");
    if (open == NULL) {
      const size_t len = strlen(cursor);
      memcpy(out, cursor, len);
      return out + len;
    }
    const size_t literal_len = (size_t)(open - cursor);
    if (literal_len > 0) {
      memcpy(out, cursor, literal_len);
      out += literal_len;
    }
    const char* close = strstr(open + 2, "}}");
    if (close == NULL) {
      const size_t len = strlen(open);
      memcpy(out, open, len);
      return out + len;
    }
    const char* value = template_value(values, count, open + 2, (size_t)(close - open - 2));
    if (value[0] != '\0') {
      if (depth < 4 && strstr(value, "{{") != NULL) {
        out = render_template_to(out, value, values, count, depth + 1);
      } else {
        const size_t value_len = strlen(value);
        memcpy(out, value, value_len);
        out += value_len;
      }
    }
    cursor = close + 2;
  }
  return out;
}

static esp_err_t send_template_chunked(
    httpd_req_t* req, const char* html, const template_value_t* values, size_t count, uint8_t depth) {
  const char* cursor = html;
  while (*cursor != '\0') {
    const char* open = strstr(cursor, "{{");
    if (open == NULL) {
      return httpd_resp_sendstr_chunk(req, cursor);
    }
    if (open > cursor &&
        httpd_resp_send_chunk(req, cursor, (ssize_t)(open - cursor)) != ESP_OK) {
      return ESP_FAIL;
    }
    const char* close = strstr(open + 2, "}}");
    if (close == NULL) {
      return httpd_resp_sendstr_chunk(req, open);
    }
    const char* value = template_value(values, count, open + 2, (size_t)(close - open - 2));
    esp_err_t result = ESP_OK;
    if (value[0] == '\0') {
      result = ESP_OK;
    } else if (depth < 4 && strstr(value, "{{") != NULL) {
      result = send_template_chunked(req, value, values, count, depth + 1);
    } else {
      result = httpd_resp_sendstr_chunk(req, value);
    }
    if (result != ESP_OK) {
      return result;
    }
    cursor = close + 2;
  }
  return ESP_OK;
}

static esp_err_t send_template(
    httpd_req_t* req, const char* html, const template_value_t* values, size_t count) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  const size_t rendered_len = rendered_template_len(html, values, count, 0);
  char* rendered = (char*)malloc(rendered_len + 1);
  if (rendered != NULL) {
    char* end = render_template_to(rendered, html, values, count, 0);
    *end = '\0';
    const esp_err_t result = httpd_resp_send(req, rendered, (ssize_t)(end - rendered));
    free(rendered);
    return result;
  }

  esp_err_t result = send_template_chunked(req, html, values, count, 0);
  if (result != ESP_OK) {
    return result;
  }
  return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t send_gzip_text(httpd_req_t* req, const unsigned char* data, unsigned int len) {
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
  return httpd_resp_send(req, (const char*)data, (ssize_t)len);
}
