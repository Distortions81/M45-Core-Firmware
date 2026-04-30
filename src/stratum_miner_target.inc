/* Private implementation fragment included by stratum_miner.c. */

static bool parse_decimal_difficulty(const char* text, uint64_t* numerator, uint64_t* denominator) {
  uint64_t whole = 0;
  uint64_t frac = 0;
  uint64_t scale = 1;
  bool saw_digit = false;
  bool in_fraction = false;

  if (text == NULL) {
    return false;
  }
  while (*text == ' ' || *text == '\t') {
    ++text;
  }
  while (*text != '\0' && *text != ',' && *text != ']') {
    const char ch = *text++;
    if (ch == '.') {
      if (in_fraction) {
        return false;
      }
      in_fraction = true;
      continue;
    }
    if (ch < '0' || ch > '9') {
      return false;
    }
    saw_digit = true;
    if (in_fraction) {
      if (scale <= UINT64_MAX / 10ULL) {
        frac = frac * 10ULL + (uint64_t)(ch - '0');
        scale *= 10ULL;
      }
    } else {
      whole = whole * 10ULL + (uint64_t)(ch - '0');
    }
  }
  if (!saw_digit) {
    return false;
  }
  *numerator = whole * scale + frac;
  *denominator = scale;
  return *numerator > 0;
}

static bool target_mul_u64(uint8_t target[32], uint64_t factor) {
  uint64_t carry = 0;
  for (int i = 31; i >= 0; --i) {
    const uint64_t value = (uint64_t)target[i] * factor + carry;
    target[i] = (uint8_t)value;
    carry = value >> 8;
  }
  if (carry != 0) {
    memset(target, 0xff, 32);
    return false;
  }
  return true;
}

static void target_add(uint8_t target[32], const uint8_t addend[32]) {
  uint16_t carry = 0;
  for (int i = 31; i >= 0; --i) {
    const uint16_t value = (uint16_t)target[i] + addend[i] + carry;
    target[i] = (uint8_t)value;
    carry = value >> 8;
  }
  if (carry != 0) {
    memset(target, 0xff, 32);
  }
}

static void target_div_u64(uint8_t target[32], uint64_t divisor) {
  uint64_t remainder = 0;
  if (divisor == 0) {
    memset(target, 0xff, 32);
    return;
  }
  for (size_t i = 0; i < 32; ++i) {
    const uint64_t value = (remainder << 8) | target[i];
    target[i] = (uint8_t)(value / divisor);
    remainder = value % divisor;
  }
}

static void build_share_target(uint8_t target[32], const char* difficulty) {
  static const uint8_t diff1_target[32] = {
      0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint64_t numerator = 0;
  uint64_t denominator = 1;
  memcpy(target, diff1_target, 32);
  if (!parse_decimal_difficulty(difficulty, &numerator, &denominator)) {
    return;
  }
  if (denominator > 1) {
    if (denominator >= numerator) {
      const uint64_t factor = denominator / numerator;
      const uint64_t remainder = denominator % numerator;
      uint8_t remainder_target[32];
      memcpy(remainder_target, target, sizeof(remainder_target));
      if (!target_mul_u64(target, factor)) {
        return;
      }
      if (remainder != 0) {
        if (target_mul_u64(remainder_target, remainder)) {
          target_div_u64(remainder_target, numerator);
          target_add(target, remainder_target);
        }
      }
      return;
    }
    target_div_u64(target, numerator);
    target_mul_u64(target, denominator);
    return;
  }
  target_div_u64(target, numerator);
}

static void set_job_share_target(mining_job_t* job, const char* difficulty) {
  build_share_target(job->share_target, difficulty);
  for (size_t i = 0; i < 8; ++i) {
    job->share_target_words[i] = read_u32_be(job->share_target + i * 4);
  }
}

static bool hash_meets_target(const uint8_t hash_le[32], const uint8_t target_be[32]) {
  for (size_t i = 0; i < 32; ++i) {
    const uint8_t hash_be = hash_le[31 - i];
    if (hash_be < target_be[i]) {
      return true;
    }
    if (hash_be > target_be[i]) {
      return false;
    }
  }
  return true;
}

static inline __attribute__((always_inline)) uint32_t IRAM_ATTR miner_hash_target_word(
    const uint32_t hash_words[8],
    size_t target_index) {
  return byteswap32(hash_words[7U - target_index]);
}

static inline bool miner_hash_words_meet_target(
    const miner_work_t* work,
    const uint32_t hash_words[8]) {
  for (size_t i = 0; i < 8; ++i) {
    const uint32_t hash_word = miner_hash_target_word(hash_words, i);
    if (hash_word < work->target_words[i]) {
      return true;
    }
    if (hash_word > work->target_words[i]) {
      return false;
    }
  }
  return true;
}

static int target_compare_be(const uint8_t a[32], const uint8_t b[32]) {
  for (size_t i = 0; i < 32; ++i) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  return 0;
}

static uint16_t target_bit_length(const uint8_t value[32]) {
  for (size_t i = 0; i < 32; ++i) {
    if (value[i] == 0) {
      continue;
    }
    uint8_t byte = value[i];
    uint8_t bits = 8;
    while ((byte & 0x80U) == 0) {
      byte <<= 1;
      --bits;
    }
    return (uint16_t)((32U - i - 1U) * 8U + bits);
  }
  return 0;
}

static bool target_get_bit(const uint8_t value[32], uint16_t bit) {
  return (value[31U - (bit / 8U)] & (uint8_t)(1U << (bit % 8U))) != 0;
}

static void target_set_bit(uint8_t value[32], uint16_t bit) {
  value[31U - (bit / 8U)] |= (uint8_t)(1U << (bit % 8U));
}

static void target_shift_left(const uint8_t input[32], uint16_t shift, uint8_t output[32]) {
  memset(output, 0, 32);
  if (shift >= 256U) {
    return;
  }
  for (uint16_t bit = 0; bit < 256U - shift; ++bit) {
    if (target_get_bit(input, bit)) {
      target_set_bit(output, (uint16_t)(bit + shift));
    }
  }
}

static void target_subtract(uint8_t value[32], const uint8_t subtrahend[32]) {
  int borrow = 0;
  for (int i = 31; i >= 0; --i) {
    int diff = (int)value[i] - (int)subtrahend[i] - borrow;
    if (diff < 0) {
      diff += 256;
      borrow = 1;
    } else {
      borrow = 0;
    }
    value[i] = (uint8_t)diff;
  }
}

static uint32_t difficulty_from_hash_be(const uint8_t hash_be[32]) {
  static const uint8_t diff1_target[32] = {
      0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  if (target_bit_length(hash_be) == 0 || target_compare_be(hash_be, diff1_target) > 0) {
    return 0;
  }

  uint8_t remainder[32];
  uint8_t shifted[32];
  memcpy(remainder, diff1_target, sizeof(remainder));
  uint32_t quotient = 0;
  const uint16_t denominator_bits = target_bit_length(hash_be);
  while (target_compare_be(remainder, hash_be) >= 0) {
    const uint16_t remainder_bits = target_bit_length(remainder);
    uint16_t shift = remainder_bits > denominator_bits ? (uint16_t)(remainder_bits - denominator_bits) : 0;
    target_shift_left(hash_be, shift, shifted);
    if (target_compare_be(shifted, remainder) > 0) {
      if (shift == 0) {
        break;
      }
      --shift;
      target_shift_left(hash_be, shift, shifted);
    }
    if (shift >= 32U) {
      return UINT32_MAX;
    }
    quotient |= 1UL << shift;
    target_subtract(remainder, shifted);
  }
  return quotient;
}

static uint32_t miner_share_difficulty(const miner_work_t* work, uint32_t nonce) {
  uint32_t second_state[8];
  uint8_t hash_be[32];
  sha256d_midstate_second_state(work->midstate, work->tail_words, nonce, second_state);
  for (size_t i = 0; i < 8; ++i) {
    write_u32_be(hash_be + i * 4, miner_hash_target_word(second_state, i));
  }
  return difficulty_from_hash_be(hash_be);
}

static void save_best_share(void) {
  nvs_handle_t nvs;
  if (nvs_open("stats", NVS_READWRITE, &nvs) == ESP_OK) {
    if (nvs_set_u32(nvs, "best_share", g_stratum_runtime.best_share) == ESP_OK) {
      nvs_commit(nvs);
    }
    nvs_close(nvs);
  }
}

static void record_share_difficulty(uint32_t share_difficulty) {
  if (share_difficulty <= 1U) {
    return;
  }
  const int64_t minute = esp_timer_get_time() / 60000000LL;
  if (g_stratum_runtime.share_bucket_minute != minute) {
    g_stratum_runtime.last_completed_share_minute = g_stratum_runtime.share_bucket_minute;
    g_stratum_runtime.last_completed_minute_best_share = g_stratum_runtime.current_minute_best_share;
    g_stratum_runtime.share_bucket_minute = minute;
    g_stratum_runtime.current_minute_best_share = 0;
  }
  if (share_difficulty > g_stratum_runtime.current_minute_best_share) {
    g_stratum_runtime.current_minute_best_share = share_difficulty;
  }
}
