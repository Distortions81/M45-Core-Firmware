/* Private implementation fragment included by stratum_miner.c. */

typedef struct {
  const char* start;
  size_t len;
} json_span_t;

typedef enum {
  STRATUM_METHOD_NONE = 0,
  STRATUM_METHOD_NOTIFY,
  STRATUM_METHOD_SET_DIFFICULTY,
  STRATUM_METHOD_SET_EXTRANONCE,
  STRATUM_METHOD_CLIENT_RECONNECT,
  STRATUM_METHOD_MINING_PING,
} stratum_method_t;

static stratum_method_t stratum_line_method(const char* line) {
  const char* method = strstr(line, "\"method\"");
  if (method == NULL) {
    return STRATUM_METHOD_NONE;
  }
  const char* colon = strchr(method, ':');
  if (colon == NULL) {
    return STRATUM_METHOD_NONE;
  }
  ++colon;
  while (*colon == ' ' || *colon == '\t') {
    ++colon;
  }
  if (*colon != '"') {
    return STRATUM_METHOD_NONE;
  }
  ++colon;
  if (strncmp(colon, "mining.notify\"", 14) == 0) {
    return STRATUM_METHOD_NOTIFY;
  }
  if (strncmp(colon, "mining.set_difficulty\"", 22) == 0) {
    return STRATUM_METHOD_SET_DIFFICULTY;
  }
  if (strncmp(colon, "mining.set_extranonce\"", 22) == 0) {
    return STRATUM_METHOD_SET_EXTRANONCE;
  }
  if (strncmp(colon, "client.reconnect\"", 17) == 0) {
    return STRATUM_METHOD_CLIENT_RECONNECT;
  }
  if (strncmp(colon, "mining.ping\"", 12) == 0) {
    return STRATUM_METHOD_MINING_PING;
  }
  return STRATUM_METHOD_NONE;
}

static bool json_find_array_for_key(const char* line, const char* key, json_span_t* out) {
  const char* cursor = strstr(line, key);
  if (cursor == NULL) {
    return false;
  }
  const char* bracket = strchr(cursor, '[');
  if (bracket == NULL) {
    return false;
  }
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (const char* p = bracket; *p != '\0'; ++p) {
    const char ch = *p;
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }
    if (ch == '"') {
      in_string = true;
    } else if (ch == '[') {
      ++depth;
    } else if (ch == ']') {
      --depth;
      if (depth == 0) {
        out->start = bracket + 1;
        out->len = (size_t)(p - bracket - 1);
        return true;
      }
    }
  }
  return false;
}

static size_t json_split_array(json_span_t array, json_span_t* values, size_t max_values) {
  size_t count = 0;
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  const char* item = array.start;
  const char* end = array.start + array.len;
  for (const char* p = array.start; p <= end; ++p) {
    const char ch = p < end ? *p : ',';
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }
    if (ch == '"') {
      in_string = true;
    } else if (ch == '[') {
      ++depth;
    } else if (ch == ']') {
      --depth;
    } else if ((ch == ',' && depth == 0) || p == end) {
      while (item < p && (*item == ' ' || *item == '\t' || *item == '\n')) {
        ++item;
      }
      const char* item_end = p;
      while (item_end > item && (item_end[-1] == ' ' || item_end[-1] == '\t' || item_end[-1] == '\n')) {
        --item_end;
      }
      if (count < max_values) {
        values[count].start = item;
        values[count].len = (size_t)(item_end - item);
        ++count;
      }
      item = p + 1;
    }
  }
  return count;
}

static bool json_unquoted_span(json_span_t span, json_span_t* out) {
  if (span.len < 2 || span.start[0] != '"' || span.start[span.len - 1] != '"') {
    return false;
  }
  for (size_t i = 1; i + 1 < span.len; ++i) {
    if (span.start[i] == '\\') {
      return false;
    }
  }
  out->start = span.start + 1;
  out->len = span.len - 2;
  return true;
}

static bool json_span_copy(json_span_t span, char* out, size_t out_size) {
  if (span.len >= out_size) {
    return false;
  }
  memcpy(out, span.start, span.len);
  out[span.len] = '\0';
  return true;
}

static bool json_span_equals_cstr(json_span_t span, const char* text) {
  const size_t len = strlen(text);
  return span.len == len && memcmp(span.start, text, len) == 0;
}

static bool json_span_to_u8(json_span_t span, uint8_t* out) {
  uint32_t value = 0;
  bool found = false;
  for (size_t i = 0; i < span.len; ++i) {
    const char ch = span.start[i];
    if (ch >= '0' && ch <= '9') {
      found = true;
      value = value * 10U + (uint32_t)(ch - '0');
      if (value > 32U) {
        return false;
      }
    }
  }
  if (!found) {
    return false;
  }
  *out = (uint8_t)value;
  return true;
}

static const DRAM_ATTR uint8_t STRATUM_HEX_VALUES[256] = {
    ['0'] = 1,
    ['1'] = 2,
    ['2'] = 3,
    ['3'] = 4,
    ['4'] = 5,
    ['5'] = 6,
    ['6'] = 7,
    ['7'] = 8,
    ['8'] = 9,
    ['9'] = 10,
    ['a'] = 11,
    ['b'] = 12,
    ['c'] = 13,
    ['d'] = 14,
    ['e'] = 15,
    ['f'] = 16,
    ['A'] = 11,
    ['B'] = 12,
    ['C'] = 13,
    ['D'] = 14,
    ['E'] = 15,
    ['F'] = 16,
};

static const DRAM_ATTR char STRATUM_HEX_DIGITS[] = "0123456789abcdef";

static inline __attribute__((always_inline)) char hex_digit_fast(uint8_t value) {
  return STRATUM_HEX_DIGITS[value & 0x0f];
}

static inline __attribute__((always_inline)) bool hex_pair_fast(const char* hex, uint8_t* out) {
  const uint8_t hi = STRATUM_HEX_VALUES[(uint8_t)hex[0]];
  const uint8_t lo = STRATUM_HEX_VALUES[(uint8_t)hex[1]];
  if (UNLIKELY(hi == 0 || lo == 0)) {
    return false;
  }
  *out = (uint8_t)(((hi - 1U) << 4) | (lo - 1U));
  return true;
}

static bool hex_span_to_bytes(json_span_t hex, uint8_t* out, size_t* offset, size_t out_size) {
  if ((hex.len & 1U) != 0 || *offset + hex.len / 2U > out_size) {
    return false;
  }
  for (size_t i = 0; i < hex.len; i += 2) {
    if (UNLIKELY(!hex_pair_fast(hex.start + i, &out[*offset]))) {
      return false;
    }
    ++(*offset);
  }
  return true;
}

static bool append_extra_nonce2_le_bytes(
    const uint8_t extra_nonce2[16], uint8_t byte_count, uint8_t* out, size_t* offset, size_t out_size) {
  if (*offset + byte_count > out_size) {
    return false;
  }
  memcpy(out + *offset, extra_nonce2, byte_count);
  *offset += byte_count;
  return true;
}

static bool hex_span_to_reversed(json_span_t hex, uint8_t* out, size_t out_len) {
  if (hex.len != out_len * 2U) {
    return false;
  }
  for (size_t i = 0; i < out_len; ++i) {
    const size_t hex_offset = (out_len - 1U - i) * 2U;
    if (UNLIKELY(!hex_pair_fast(hex.start + hex_offset, out + i))) {
      return false;
    }
  }
  return true;
}

static bool hex_span_to_word_swapped32(json_span_t hex, uint8_t* out) {
  if (hex.len != 64) {
    return false;
  }
  for (size_t word = 0; word < 8; ++word) {
    const size_t in = word * 8U;
    const size_t offset = word * 4U;
    for (size_t byte = 0; byte < 4; ++byte) {
      const size_t hex_offset = in + (3U - byte) * 2U;
      if (UNLIKELY(!hex_pair_fast(hex.start + hex_offset, out + offset + byte))) {
        return false;
      }
    }
  }
  return true;
}

static void uint32_to_hex_be(uint32_t value, char* out, size_t out_size) {
  if (out_size < 9) {
    return;
  }
  for (int shift = 28, i = 0; shift >= 0; shift -= 4, ++i) {
    out[i] = hex_digit_fast((uint8_t)(value >> shift));
  }
  out[8] = '\0';
}

static void extra_nonce2_to_hex_le(
    const uint8_t extra_nonce2[16], uint8_t byte_count, char* out, size_t out_size) {
  if (out_size < (size_t)byte_count * 2U + 1U) {
    return;
  }
  size_t offset = 0;
  for (uint8_t i = 0; i < byte_count; ++i) {
    const uint8_t byte = extra_nonce2[i];
    out[offset++] = hex_digit_fast(byte >> 4);
    out[offset++] = hex_digit_fast(byte);
  }
  out[offset] = '\0';
}

static void extra_nonce2_clear(uint8_t extra_nonce2[16]) {
  memset(extra_nonce2, 0, 16);
}

static void extra_nonce2_increment(uint8_t extra_nonce2[16], uint8_t byte_count) {
  for (uint8_t i = 0; i < byte_count && i < 16; ++i) {
    ++extra_nonce2[i];
    if (extra_nonce2[i] != 0) {
      break;
    }
  }
}

static void sha256d(const uint8_t* data, size_t len, uint8_t out[32]) {
  uint8_t first[32];
  mbedtls_sha256(data, len, first, 0);
  mbedtls_sha256(first, sizeof(first), out, 0);
}
