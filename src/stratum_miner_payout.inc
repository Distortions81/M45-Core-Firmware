/* Private implementation fragment included by stratum_miner.c. */

#define STRATUM_PAYOUT_MIN_PERCENT_X100 9700U
#define STRATUM_BASE58_DECODE_MAX 64
#define STRATUM_BECH32_DECODE_MAX 90

typedef struct {
  const uint8_t* bytes;
  size_t len;
  size_t offset;
} coinbase_reader_t;

static bool payout_read_bytes(coinbase_reader_t* reader, size_t len, const uint8_t** out) {
  if (reader->offset + len > reader->len) {
    return false;
  }
  *out = reader->bytes + reader->offset;
  reader->offset += len;
  return true;
}

static bool payout_skip_bytes(coinbase_reader_t* reader, size_t len) {
  const uint8_t* ignored;
  return payout_read_bytes(reader, len, &ignored);
}

static bool payout_read_u64_le(coinbase_reader_t* reader, uint64_t* out) {
  const uint8_t* bytes;
  if (!payout_read_bytes(reader, 8, &bytes)) {
    return false;
  }
  uint64_t value = 0;
  for (int i = 7; i >= 0; --i) {
    value = (value << 8) | bytes[i];
  }
  *out = value;
  return true;
}

static bool payout_read_varint(coinbase_reader_t* reader, uint64_t* out) {
  const uint8_t* first;
  if (!payout_read_bytes(reader, 1, &first)) {
    return false;
  }
  if (*first < 0xfd) {
    *out = *first;
    return true;
  }
  size_t byte_count = 0;
  if (*first == 0xfd) {
    byte_count = 2;
  } else if (*first == 0xfe) {
    byte_count = 4;
  } else {
    byte_count = 8;
  }
  const uint8_t* bytes;
  if (!payout_read_bytes(reader, byte_count, &bytes)) {
    return false;
  }
  uint64_t value = 0;
  for (size_t i = 0; i < byte_count; ++i) {
    value |= (uint64_t)bytes[i] << (8U * i);
  }
  *out = value;
  return true;
}

static int payout_base58_value(char ch) {
  static const char* alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
  const char* found = strchr(alphabet, ch);
  return found == NULL ? -1 : (int)(found - alphabet);
}

static bool payout_base58check_decode(const char* text, uint8_t* out, size_t* out_len) {
  uint8_t number[STRATUM_BASE58_DECODE_MAX] = {0};
  size_t number_len = 1;
  size_t leading_zeroes = 0;
  while (text[leading_zeroes] == '1') {
    ++leading_zeroes;
  }
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    const int digit = payout_base58_value(*cursor);
    if (digit < 0) {
      return false;
    }
    uint32_t carry = (uint32_t)digit;
    for (size_t i = 0; i < number_len; ++i) {
      const size_t index = number_len - 1U - i;
      carry += (uint32_t)number[index] * 58U;
      number[index] = (uint8_t)(carry & 0xffU);
      carry >>= 8;
    }
    while (carry > 0) {
      if (number_len >= sizeof(number)) {
        return false;
      }
      memmove(number + 1, number, number_len);
      number[0] = (uint8_t)(carry & 0xffU);
      ++number_len;
      carry >>= 8;
    }
  }
  size_t first_nonzero = 0;
  while (first_nonzero < number_len && number[first_nonzero] == 0) {
    ++first_nonzero;
  }
  const size_t decoded_len = leading_zeroes + number_len - first_nonzero;
  if (decoded_len < 5 || decoded_len > STRATUM_BASE58_DECODE_MAX) {
    return false;
  }
  memset(out, 0, leading_zeroes);
  memcpy(out + leading_zeroes, number + first_nonzero, number_len - first_nonzero);
  uint8_t checksum[32];
  sha256d(out, decoded_len - 4U, checksum);
  if (memcmp(checksum, out + decoded_len - 4U, 4) != 0) {
    return false;
  }
  *out_len = decoded_len - 4U;
  return true;
}

static int payout_bech32_value(char ch) {
  static const char* charset = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
  const char* found = strchr(charset, ch);
  return found == NULL ? -1 : (int)(found - charset);
}

static uint32_t payout_bech32_polymod_step(uint32_t chk, uint8_t value) {
  static const uint32_t generator[5] = {
      0x3b6a57b2UL, 0x26508e6dUL, 0x1ea119faUL, 0x3d4233ddUL, 0x2a1462b3UL};
  const uint8_t top = (uint8_t)(chk >> 25);
  chk = ((chk & 0x1ffffffUL) << 5) ^ value;
  for (uint8_t i = 0; i < 5; ++i) {
    if (((top >> i) & 1U) != 0) {
      chk ^= generator[i];
    }
  }
  return chk;
}

static bool payout_bech32_convert_bits(
    const uint8_t* input, size_t input_len, uint8_t from_bits, uint8_t to_bits, bool pad, uint8_t* out, size_t* out_len) {
  uint32_t accumulator = 0;
  uint8_t bits = 0;
  size_t offset = 0;
  const uint32_t max_value = (1U << to_bits) - 1U;
  for (size_t i = 0; i < input_len; ++i) {
    if ((input[i] >> from_bits) != 0) {
      return false;
    }
    accumulator = (accumulator << from_bits) | input[i];
    bits += from_bits;
    while (bits >= to_bits) {
      bits -= to_bits;
      if (offset >= *out_len) {
        return false;
      }
      out[offset++] = (uint8_t)((accumulator >> bits) & max_value);
    }
  }
  if (pad) {
    if (bits > 0) {
      if (offset >= *out_len) {
        return false;
      }
      out[offset++] = (uint8_t)((accumulator << (to_bits - bits)) & max_value);
    }
  } else if (bits >= from_bits || ((accumulator << (to_bits - bits)) & max_value) != 0) {
    return false;
  }
  *out_len = offset;
  return true;
}

static bool payout_bech32_decode_witness(const char* address, uint8_t* version, uint8_t* program, size_t* program_len) {
  char lower[STRATUM_BECH32_DECODE_MAX + 1];
  size_t len = strlen(address);
  if (len > STRATUM_BECH32_DECODE_MAX || len < 8) {
    return false;
  }
  bool has_lower = false;
  bool has_upper = false;
  size_t separator = SIZE_MAX;
  for (size_t i = 0; i < len; ++i) {
    const char ch = address[i];
    if (ch < 33 || ch > 126) {
      return false;
    }
    if (ch >= 'a' && ch <= 'z') {
      has_lower = true;
      lower[i] = ch;
    } else if (ch >= 'A' && ch <= 'Z') {
      has_upper = true;
      lower[i] = (char)(ch + ('a' - 'A'));
    } else {
      lower[i] = ch;
    }
    if (lower[i] == '1') {
      separator = i;
    }
  }
  lower[len] = '\0';
  if ((has_lower && has_upper) || separator == SIZE_MAX || separator < 1 || separator + 7 > len) {
    return false;
  }
  if (!((separator == 2 && memcmp(lower, "bc", 2) == 0) ||
        (separator == 2 && memcmp(lower, "tb", 2) == 0))) {
    return false;
  }

  uint32_t polymod = 1;
  for (size_t i = 0; i < separator; ++i) {
    polymod = payout_bech32_polymod_step(polymod, (uint8_t)(lower[i] >> 5));
  }
  polymod = payout_bech32_polymod_step(polymod, 0);
  for (size_t i = 0; i < separator; ++i) {
    polymod = payout_bech32_polymod_step(polymod, (uint8_t)(lower[i] & 31));
  }

  uint8_t data[STRATUM_BECH32_DECODE_MAX];
  size_t data_len = 0;
  for (size_t i = separator + 1U; i < len; ++i) {
    const int value = payout_bech32_value(lower[i]);
    if (value < 0) {
      return false;
    }
    polymod = payout_bech32_polymod_step(polymod, (uint8_t)value);
    data[data_len++] = (uint8_t)value;
  }
  if (polymod != 1 && polymod != 0x2bc830a3UL) {
    return false;
  }
  if (data_len < 7 || data[0] > 16) {
    return false;
  }
  *version = data[0];
  *program_len = 40;
  if (!payout_bech32_convert_bits(data + 1, data_len - 7U, 5, 8, false, program, program_len)) {
    return false;
  }
  if (*program_len < 2 || *program_len > 40) {
    return false;
  }
  if (*version == 0 && polymod != 1) {
    return false;
  }
  if (*version > 0 && polymod != 0x2bc830a3UL) {
    return false;
  }
  return true;
}

static bool payout_wallet_script(const char* wallet, uint8_t* script, size_t* script_len) {
  uint8_t decoded[STRATUM_BASE58_DECODE_MAX];
  size_t decoded_len = 0;
  if (payout_base58check_decode(wallet, decoded, &decoded_len) && decoded_len == 21) {
    if (decoded[0] == 0x00) {
      script[0] = 0x76;
      script[1] = 0xa9;
      script[2] = 0x14;
      memcpy(script + 3, decoded + 1, 20);
      script[23] = 0x88;
      script[24] = 0xac;
      *script_len = 25;
      return true;
    }
    if (decoded[0] == 0x05) {
      script[0] = 0xa9;
      script[1] = 0x14;
      memcpy(script + 2, decoded + 1, 20);
      script[22] = 0x87;
      *script_len = 23;
      return true;
    }
  }

  uint8_t witness_version = 0;
  uint8_t program[40];
  size_t program_len = sizeof(program);
  if (payout_bech32_decode_witness(wallet, &witness_version, program, &program_len)) {
    script[0] = witness_version == 0 ? 0x00 : (uint8_t)(0x50U + witness_version);
    script[1] = (uint8_t)program_len;
    memcpy(script + 2, program, program_len);
    *script_len = program_len + 2U;
    return true;
  }
  return false;
}

static bool payout_scan_coinbase_outputs(
    const uint8_t* coinbase, size_t coinbase_len, const uint8_t* wallet_script, size_t wallet_script_len, uint64_t* total_value, uint64_t* wallet_value) {
  coinbase_reader_t reader = {
      .bytes = coinbase,
      .len = coinbase_len,
      .offset = 0,
  };
  *total_value = 0;
  *wallet_value = 0;
  if (!payout_skip_bytes(&reader, 4)) {
    return false;
  }

  uint64_t input_count = 0;
  if (!payout_read_varint(&reader, &input_count)) {
    return false;
  }
  if (input_count == 0 && reader.offset + 1 <= reader.len && coinbase[reader.offset] == 0x01) {
    ++reader.offset;
    if (!payout_read_varint(&reader, &input_count)) {
      return false;
    }
  }
  if (input_count == 0 || input_count > 8) {
    return false;
  }
  for (uint64_t input = 0; input < input_count; ++input) {
    uint64_t script_len = 0;
    if (!payout_skip_bytes(&reader, 36) ||
        !payout_read_varint(&reader, &script_len) ||
        script_len > coinbase_len ||
        !payout_skip_bytes(&reader, (size_t)script_len) ||
        !payout_skip_bytes(&reader, 4)) {
      return false;
    }
  }

  uint64_t output_count = 0;
  if (!payout_read_varint(&reader, &output_count) || output_count == 0 || output_count > 64) {
    return false;
  }
  for (uint64_t output = 0; output < output_count; ++output) {
    uint64_t value = 0;
    uint64_t script_len = 0;
    const uint8_t* script = NULL;
    if (!payout_read_u64_le(&reader, &value) ||
        !payout_read_varint(&reader, &script_len) ||
        script_len > coinbase_len ||
        !payout_read_bytes(&reader, (size_t)script_len, &script)) {
      return false;
    }
    *total_value += value;
    if (script_len == wallet_script_len && memcmp(script, wallet_script, wallet_script_len) == 0) {
      *wallet_value += value;
    }
  }
  return *total_value > 0;
}

static void stratum_update_payout_from_coinbase(const uint8_t* coinbase, size_t coinbase_len) {
  uint8_t wallet_script[42];
  size_t wallet_script_len = 0;
  g_stratum_runtime.payout_percent_x100 = 0;
  if (!payout_wallet_script(g_stratum.wallet, wallet_script, &wallet_script_len)) {
    g_stratum_runtime.payout_status = STRATUM_PAYOUT_STATUS_UNSUPPORTED_WALLET;
    return;
  }

  uint64_t total_value = 0;
  uint64_t wallet_value = 0;
  if (!payout_scan_coinbase_outputs(
          coinbase, coinbase_len, wallet_script, wallet_script_len, &total_value, &wallet_value)) {
    g_stratum_runtime.payout_status = STRATUM_PAYOUT_STATUS_PARSE_ERROR;
    return;
  }

  if (wallet_value == 0) {
    g_stratum_runtime.payout_status = STRATUM_PAYOUT_STATUS_MISSING;
    return;
  }
  const uint64_t percent_x100 = ((wallet_value * 10000ULL) + (total_value / 2ULL)) / total_value;
  g_stratum_runtime.payout_percent_x100 =
      percent_x100 > UINT16_MAX ? UINT16_MAX : (uint16_t)percent_x100;
  g_stratum_runtime.payout_status = g_stratum_runtime.payout_percent_x100 < STRATUM_PAYOUT_MIN_PERCENT_X100
                                        ? STRATUM_PAYOUT_STATUS_LOW
                                        : STRATUM_PAYOUT_STATUS_OK;
}
