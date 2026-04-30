/* Private implementation fragment included by web_server.c. */

static void url_decode(char* text) {
  char* write = text;
  for (char* read = text; *read != '\0'; ++read) {
    if (*read == '+') {
      *write++ = ' ';
    } else if (*read == '%') {
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

static bool read_body(httpd_req_t* req, char* body, size_t body_size) {
  if (body_size == 0) {
    return false;
  }
  int remaining = req->content_len;
  size_t offset = 0;
  while (remaining > 0 && offset + 1 < body_size) {
    const int chunk = MIN(remaining, (int)(body_size - 1 - offset));
    const int read_len = httpd_req_recv(req, body + offset, chunk);
    if (read_len <= 0) {
      break;
    }
    offset += read_len;
    remaining -= read_len;
  }
  body[offset] = '\0';
  char scratch[64];
  while (remaining > 0) {
    const int read_len = httpd_req_recv(req, scratch, MIN(remaining, (int)sizeof(scratch)));
    if (read_len <= 0) {
      break;
    }
    remaining -= read_len;
  }
  return offset > 0 || req->content_len == 0;
}

static bool form_value(const char* body, const char* key, char* out, size_t out_size) {
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

static void send_plain(httpd_req_t* req, const char* text) {
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, text);
}
