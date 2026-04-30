/* Private implementation fragment included by stratum_miner.c. */

static bool append_text(char* out, size_t out_size, size_t* offset, const char* text) {
  const size_t len = strlen(text);
  if (*offset + len >= out_size) {
    return false;
  }
  memcpy(out + *offset, text, len);
  *offset += len;
  out[*offset] = '\0';
  return true;
}

static bool append_json_token_value(char* out, size_t out_size, size_t* offset, const char* text) {
  if (!append_text(out, out_size, offset, "\"")) {
    return false;
  }
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    const char ch = *cursor;
    if ((unsigned char)ch < 0x20U) {
      return false;
    }
    if (ch == '"' || ch == '\\') {
      char escaped[3] = {'\\', ch, '\0'};
      if (!append_text(out, out_size, offset, escaped)) {
        return false;
      }
      continue;
    }
    char one[2] = {ch, '\0'};
    if (!append_text(out, out_size, offset, one)) {
      return false;
    }
  }
  return append_text(out, out_size, offset, "\"");
}

static bool append_u32_dec(char* out, size_t out_size, size_t* offset, uint32_t value) {
  char tmp[10];
  size_t len = 0;
  do {
    tmp[len++] = (char)('0' + (value % 10U));
    value /= 10U;
  } while (value > 0 && len < sizeof(tmp));
  if (*offset + len >= out_size) {
    return false;
  }
  while (len > 0) {
    out[(*offset)++] = tmp[--len];
  }
  out[*offset] = '\0';
  return true;
}

static void stratum_build_miner_info(char* out, size_t out_size) {
  snprintf(out,
           out_size,
           "%s/%.*s%s",
           STRATUM_MINER_MODEL,
           STRATUM_MINER_VERSION_CHARS,
           APP_BUILD_GIT_SHA,
           APP_BUILD_DIRTY ? "-dirty" : "");
}

static bool send_all(int sock, const char* text) {
  size_t remaining = strlen(text);
  const char* cursor = text;
  const int64_t deadline =
      esp_timer_get_time() + (int64_t)STRATUM_SOCKET_TIMEOUT_MS * 1000LL;
  while (remaining > 0) {
    const int sent = send(sock, cursor, remaining, 0);
    if (sent > 0) {
      cursor += sent;
      remaining -= sent;
      continue;
    }
    if (sent < 0 && errno == EINTR) {
      continue;
    }
    if (sent == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
      if (esp_timer_get_time() >= deadline) {
        return false;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    if (sent < 0) {
      return false;
    }
  }
  return true;
}

static bool stratum_set_socket_nonblocking(int sock) {
  const int flags = fcntl(sock, F_GETFL, 0);
  if (flags < 0) {
    ESP_LOGE(TAG, "fcntl get flags failed: errno %d", errno);
    return false;
  }
  if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) != 0) {
    ESP_LOGE(TAG, "fcntl set non-blocking failed: errno %d", errno);
    return false;
  }
  return true;
}

