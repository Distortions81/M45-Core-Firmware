/* Private implementation fragment included by web_server.c. */

static void html_escape(const char* input, char* out, size_t out_size) {
  if (out_size == 0) {
    return;
  }
  if (input == NULL) {
    input = "";
  }

  size_t offset = 0;
  for (const char* cursor = input; *cursor != '\0' && offset + 1 < out_size; ++cursor) {
    const char* replacement = NULL;
    switch (*cursor) {
      case '&':
        replacement = "&amp;";
        break;
      case '<':
        replacement = "&lt;";
        break;
      case '>':
        replacement = "&gt;";
        break;
      case '"':
        replacement = "&quot;";
        break;
      case '\'':
        replacement = "&#39;";
        break;
      default:
        out[offset++] = *cursor;
        continue;
    }

    const size_t replacement_len = strlen(replacement);
    if (offset + replacement_len >= out_size) {
      break;
    }
    memcpy(out + offset, replacement, replacement_len);
    offset += replacement_len;
  }
  out[offset] = '\0';
}

static const char* normalized_theme(const char* theme) {
  if (strcmp(theme, "light") == 0) {
    return "light";
  }
  return "dark";
}

static void set_color_theme(const char* theme, bool persist) {
  const char* normalized = normalized_theme(theme);
  copy_str(g_theme, sizeof(g_theme), normalized);
  g_display.light_mode = strcmp(normalized, "light") == 0 ? 1 : 0;
  if (!persist) {
    return;
  }

  nvs_handle_t nvs;
  if (nvs_open("theme", NVS_READWRITE, &nvs) == ESP_OK) {
    nvs_set_str(nvs, "name", g_theme);
    nvs_commit(nvs);
    nvs_close(nvs);
  }
  if (nvs_open("display", NVS_READWRITE, &nvs) == ESP_OK) {
    nvs_set_u8(nvs, "light", g_display.light_mode);
    nvs_commit(nvs);
    nvs_close(nvs);
  }
}

static bool parse_required_port(const char* text, uint16_t* out) {
  if (text == NULL || *text == '\0') {
    return false;
  }

  uint32_t value = 0;
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    value = value * 10U + (uint32_t)(*cursor - '0');
    if (value > 65535U) {
      return false;
    }
  }

  if (value == 0) {
    return false;
  }
  *out = (uint16_t)value;
  return true;
}

static bool pool_host_text_valid(const char* text, bool allow_empty) {
  if (text == NULL || *text == '\0') {
    return allow_empty;
  }

  const size_t len = strlen(text);
  if (len > 95 || text[0] == '.' || text[0] == '-' || text[len - 1] == '.' ||
      text[len - 1] == '-') {
    return false;
  }

  bool previous_dot = false;
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    const char ch = *cursor;
    const bool valid_char = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                            (ch >= '0' && ch <= '9') || ch == '.' || ch == '-';
    if (!valid_char) {
      return false;
    }
    if (ch == '.') {
      if (previous_dot) {
        return false;
      }
      previous_dot = true;
    } else {
      previous_dot = false;
    }
  }
  return true;
}

static void format_bytes_text(size_t bytes, char* out, size_t out_size) {
  if (bytes >= 1024U * 1024U) {
    snprintf(out, out_size, "%.2f MiB", (double)bytes / (1024.0 * 1024.0));
  } else if (bytes >= 1024U) {
    snprintf(out, out_size, "%.2f KiB", (double)bytes / 1024.0);
  } else {
    snprintf(out, out_size, "%u B", (unsigned)bytes);
  }
}

static void format_hashrate_text(double hashes_per_second, char* out, size_t out_size) {
  if (!isfinite(hashes_per_second) || hashes_per_second <= 0.0) {
    copy_str(out, out_size, "0 H/s");
  } else if (hashes_per_second >= 1000000.0) {
    snprintf(out, out_size, "%.3f MH/s", hashes_per_second / 1000000.0);
  } else if (hashes_per_second >= 1000.0) {
    snprintf(out, out_size, "%.1f kH/s", hashes_per_second / 1000.0);
  } else {
    snprintf(out, out_size, "%.0f H/s", hashes_per_second);
  }
}

static void format_microdifficulty_text(const char* difficulty, char* out, size_t out_size) {
  if (difficulty == NULL || difficulty[0] == '\0') {
    copy_str(out, out_size, "0.0 \xC2\xB5" "diff");
    return;
  }
  char* end = NULL;
  const double value = strtod(difficulty, &end);
  if (end == difficulty || !isfinite(value) || value <= 0.0) {
    copy_str(out, out_size, "0.0 \xC2\xB5" "diff");
    return;
  }
  snprintf(out, out_size, "%.1f \xC2\xB5" "diff", value * 1000000.0);
}

static uint64_t app_uptime_us_now(int64_t now) {
  return now > (int64_t)g_boot_us ? (uint64_t)(now - (int64_t)g_boot_us) : 0;
}

static uint64_t pool_connected_us_now(int64_t now) {
  uint64_t connected_us = g_stratum_runtime.connected_total_us;
  if (g_stratum_runtime.connected && g_stratum_runtime.connected_at_us > 0 &&
      now > (int64_t)g_stratum_runtime.connected_at_us) {
    connected_us += (uint64_t)(now - (int64_t)g_stratum_runtime.connected_at_us);
  }
  return connected_us;
}

static uint64_t pool_tracking_us_now(int64_t now) {
  if (g_stratum_runtime.first_connected_at_us == 0 ||
      now <= (int64_t)g_stratum_runtime.first_connected_at_us) {
    return 0;
  }
  return (uint64_t)(now - (int64_t)g_stratum_runtime.first_connected_at_us);
}

static void format_duration_us_text(uint64_t duration_us, char* out, size_t out_size) {
  const uint64_t total_s = duration_us / 1000000ULL;
  const unsigned days = (unsigned)(total_s / 86400ULL);
  const unsigned hours = (unsigned)((total_s / 3600ULL) % 24ULL);
  const unsigned minutes = (unsigned)((total_s / 60ULL) % 60ULL);
  const unsigned seconds = (unsigned)(total_s % 60ULL);
  if (days > 0) {
    snprintf(out, out_size, "%ud %uh", days, hours);
  } else if (hours > 0) {
    snprintf(out, out_size, "%uh %um", hours, minutes);
  } else if (minutes > 0) {
    snprintf(out, out_size, "%um %us", minutes, seconds);
  } else {
    snprintf(out, out_size, "%us", seconds);
  }
}

static void format_pool_uptime_pct_text(uint64_t connected_us, uint64_t uptime_us, char* out, size_t out_size) {
  if (uptime_us == 0) {
    copy_str(out, out_size, "0.0%");
    return;
  }
  double percent = ((double)connected_us * 100.0) / (double)uptime_us;
  if (percent > 100.0) {
    percent = 100.0;
  }
  snprintf(out, out_size, "%.1f%%", percent);
}

static void format_share_error_pct_text(uint32_t rejected, uint32_t submitted, char* out, size_t out_size) {
  if (submitted == 0) {
    copy_str(out, out_size, "0.0%");
    return;
  }
  snprintf(out, out_size, "%.1f%%", ((double)rejected * 100.0) / (double)submitted);
}

static const char* payout_status_id(void) {
  switch (g_stratum_runtime.payout_status) {
    case STRATUM_PAYOUT_STATUS_OK:
      return "ok";
    case STRATUM_PAYOUT_STATUS_LOW:
      return "low";
    case STRATUM_PAYOUT_STATUS_MISSING:
      return "missing";
    case STRATUM_PAYOUT_STATUS_UNSUPPORTED_WALLET:
      return "unsupported";
    case STRATUM_PAYOUT_STATUS_PARSE_ERROR:
      return "parse_error";
    case STRATUM_PAYOUT_STATUS_UNCHECKED:
    default:
      return "unchecked";
  }
}

static const char* payout_status_text(void) {
  switch (g_stratum_runtime.payout_status) {
    case STRATUM_PAYOUT_STATUS_OK:
      return "Coinbase payout matches wallet.";
    case STRATUM_PAYOUT_STATUS_LOW:
      return "Warning: wallet receives less than 97% of coinbase outputs.";
    case STRATUM_PAYOUT_STATUS_MISSING:
      return "Warning: wallet was not found in the coinbase outputs.";
    case STRATUM_PAYOUT_STATUS_UNSUPPORTED_WALLET:
      return "Coinbase payout check unavailable for this wallet address.";
    case STRATUM_PAYOUT_STATUS_PARSE_ERROR:
      return "Coinbase payout check could not parse the coinbase transaction.";
    case STRATUM_PAYOUT_STATUS_UNCHECKED:
    default:
      return "Coinbase payout has not been checked yet.";
  }
}

static void format_payout_percent_text(char* out, size_t out_size) {
  if ((g_stratum_runtime.payout_percent_x100 % 100U) == 0) {
    snprintf(out, out_size, "%u%%", (unsigned)(g_stratum_runtime.payout_percent_x100 / 100U));
    return;
  }
  snprintf(out,
           out_size,
           "%u.%02u%%",
           (unsigned)(g_stratum_runtime.payout_percent_x100 / 100U),
           (unsigned)(g_stratum_runtime.payout_percent_x100 % 100U));
}

static void format_average_period_text(uint64_t elapsed_us, char* out, size_t out_size) {
  uint64_t minutes = (elapsed_us + 30000000ULL) / 60000000ULL;
  if (minutes == 0) {
    minutes = 1;
  }
  if (minutes < 60) {
    snprintf(out, out_size, "%u min", (unsigned)minutes);
    return;
  }
  const unsigned hours = (unsigned)(minutes / 60ULL);
  const unsigned remaining_minutes = (unsigned)(minutes % 60ULL);
  if (remaining_minutes > 0) {
    snprintf(out, out_size, "%uh %um", hours, remaining_minutes);
  } else {
    snprintf(out, out_size, "%uh", hours);
  }
}

static void hashrate_average_drop_oldest_locked(void) {
  if (g_hashrate_average_count == 0) {
    return;
  }
  memmove(g_hashrate_average_samples,
          g_hashrate_average_samples + 1,
          (g_hashrate_average_count - 1U) * sizeof(g_hashrate_average_samples[0]));
  --g_hashrate_average_count;
}

static void format_hashrate_average_text(char* out, size_t out_size) {
  const int64_t now = esp_timer_get_time();
  const uint64_t hashes_total = g_stratum_runtime.hashes_total;
  uint64_t elapsed_us = 0;
  uint64_t hash_delta = 0;

  portENTER_CRITICAL(&g_hashrate_average_mux);
  if (g_hashrate_average_count > 0 &&
      hashes_total < g_hashrate_average_samples[g_hashrate_average_count - 1U].hashes_total) {
    g_hashrate_average_count = 0;
  }
  if (g_hashrate_average_count == 0) {
    g_hashrate_average_samples[0] = (hashrate_average_sample_t){
        .time_us = now,
        .hashes_total = hashes_total,
    };
    g_hashrate_average_count = 1;
  } else {
    hashrate_average_sample_t* last =
        &g_hashrate_average_samples[g_hashrate_average_count - 1U];
    if (now - last->time_us >= HASHRATE_AVERAGE_SAMPLE_US) {
      if (g_hashrate_average_count >= HASHRATE_AVERAGE_MAX_SAMPLES) {
        hashrate_average_drop_oldest_locked();
      }
      g_hashrate_average_samples[g_hashrate_average_count++] = (hashrate_average_sample_t){
          .time_us = now,
          .hashes_total = hashes_total,
      };
    }
  }
  const int64_t cutoff = now - HASHRATE_AVERAGE_WINDOW_US;
  while (g_hashrate_average_count > 2 && g_hashrate_average_samples[1].time_us < cutoff) {
    hashrate_average_drop_oldest_locked();
  }
  if (g_hashrate_average_count >= 2) {
    const hashrate_average_sample_t oldest = g_hashrate_average_samples[0];
    const hashrate_average_sample_t newest =
        g_hashrate_average_samples[g_hashrate_average_count - 1U];
    if (newest.time_us > oldest.time_us && newest.hashes_total >= oldest.hashes_total) {
      elapsed_us = (uint64_t)(newest.time_us - oldest.time_us);
      hash_delta = newest.hashes_total - oldest.hashes_total;
    }
  }
  portEXIT_CRITICAL(&g_hashrate_average_mux);

  if (elapsed_us < HASHRATE_AVERAGE_MIN_SPAN_US) {
    copy_str(out, out_size, "Average: gathering");
    return;
  }

  char period[16];
  char hashrate[20];
  const double average = ((double)hash_delta * 1000000.0) / (double)elapsed_us;
  format_average_period_text(elapsed_us, period, sizeof(period));
  format_hashrate_text(average, hashrate, sizeof(hashrate));
  snprintf(out, out_size, "%s avg: %s", period, hashrate);
}

static bool parse_optional_port(const char* text, uint16_t fallback, uint16_t* out) {
  if (text == NULL || *text == '\0') {
    *out = fallback;
    return true;
  }
  return parse_required_port(text, out);
}
