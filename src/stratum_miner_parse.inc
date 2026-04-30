/* Private implementation fragment included by stratum_miner.c. */

static void stratum_parse_set_difficulty(const char* line) {
  const char* params = strstr(line, "\"params\"");
  if (params == NULL) {
    return;
  }
  const char* bracket = strchr(params, '[');
  if (bracket == NULL) {
    return;
  }
  ++bracket;
  while (*bracket == ' ' || *bracket == '\t') {
    ++bracket;
  }
  size_t len = 0;
  while (bracket[len] != '\0' && bracket[len] != ',' && bracket[len] != ']' &&
         bracket[len] != ' ' && len + 1 < sizeof(g_stratum_runtime.current_difficulty)) {
    g_stratum_runtime.current_difficulty[len] = bracket[len];
    ++len;
  }
  g_stratum_runtime.current_difficulty[len] = '\0';
  set_job_share_target(&g_mining_job, g_stratum_runtime.current_difficulty);
  miner_publish_current_job(true);
}

static bool stratum_apply_extranonce(json_span_t extra_nonce1, json_span_t extra_nonce2_size) {
  json_span_t extra_nonce1_hex;
  uint8_t parsed_extra_nonce2_size = 0;
  if (!json_unquoted_span(extra_nonce1, &extra_nonce1_hex) ||
      (extra_nonce1_hex.len & 1U) != 0 ||
      !json_span_copy(extra_nonce1_hex, g_mining_job.extra_nonce1, sizeof(g_mining_job.extra_nonce1)) ||
      !json_span_to_u8(extra_nonce2_size, &parsed_extra_nonce2_size) ||
      parsed_extra_nonce2_size == 0 || parsed_extra_nonce2_size > 16) {
    return false;
  }
  g_mining_job.extra_nonce1_len = (uint8_t)extra_nonce1_hex.len;
  g_mining_job.extra_nonce2_size = parsed_extra_nonce2_size;
  extra_nonce2_clear(g_mining_job.extra_nonce2);
  g_mining_job.subscribed = true;
  return true;
}

static bool stratum_parse_subscribe_response(const char* line) {
  json_span_t result;
  json_span_t values[4];
  if (!json_find_array_for_key(line, "\"result\"", &result)) {
    return false;
  }
  const size_t count = json_split_array(result, values, 4);
  if (count < 3) {
    return false;
  }
  if (!stratum_apply_extranonce(values[1], values[2])) {
    return false;
  }
  return true;
}

static void stratum_parse_set_extranonce(const char* line) {
  json_span_t params;
  json_span_t values[2];
  if (!json_find_array_for_key(line, "\"params\"", &params) ||
      json_split_array(params, values, 2) < 2 ||
      !stratum_apply_extranonce(values[0], values[1])) {
    ESP_LOGW(TAG, "Ignored invalid mining.set_extranonce");
    return;
  }
  g_mining_job.ready = false;
  miner_clear_work();
}

static void json_trim_span(json_span_t* span) {
  while (span->len > 0 &&
         (span->start[0] == ' ' || span->start[0] == '\t' || span->start[0] == '\n')) {
    ++span->start;
    --span->len;
  }
  while (span->len > 0) {
    const char ch = span->start[span->len - 1];
    if (ch != ' ' && ch != '\t' && ch != '\n') {
      break;
    }
    --span->len;
  }
}

static bool stratum_notify_values(const char* line, json_span_t values[9]) {
  const char* params = strstr(line, "\"params\"");
  if (params == NULL) {
    return false;
  }
  const char* item = strchr(params, '[');
  if (item == NULL) {
    return false;
  }
  ++item;

  size_t count = 0;
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (const char* cursor = item; *cursor != '\0'; ++cursor) {
    const char ch = *cursor;
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
      continue;
    }
    if (ch == '[') {
      ++depth;
      continue;
    }
    if (ch == ']' && depth > 0) {
      --depth;
      continue;
    }
    if ((ch == ',' && depth == 0) || (ch == ']' && depth == 0)) {
      if (count >= 9) {
        return true;
      }
      values[count] = (json_span_t){
          .start = item,
          .len = (size_t)(cursor - item),
      };
      json_trim_span(&values[count]);
      ++count;
      if (ch == ']') {
        return count >= 9;
      }
      item = cursor + 1;
    }
  }
  return false;
}

static bool stratum_prepare_job(json_span_t* values, size_t count) {
  if (!g_mining_job.subscribed || count < 9) {
    return false;
  }

  json_span_t branches[STRATUM_MAX_BRANCHES + 1];
  json_span_t job_id;
  json_span_t prev_hash;
  json_span_t coinbase1;
  json_span_t coinbase2;
  json_span_t version;
  json_span_t nbits;
  json_span_t ntime;
  uint8_t coinbase[STRATUM_MAX_COINBASE_HEX / 2];
  uint8_t merkle[32];
  size_t offset = 0;
  const bool clean_jobs = values[8].len == 4 && strncmp(values[8].start, "true", 4) == 0;

  if (!json_unquoted_span(values[0], &job_id) ||
      !json_unquoted_span(values[1], &prev_hash) ||
      !json_unquoted_span(values[2], &coinbase1) ||
      !json_unquoted_span(values[3], &coinbase2) ||
      !json_unquoted_span(values[5], &version) ||
      !json_unquoted_span(values[6], &nbits) ||
      !json_unquoted_span(values[7], &ntime) ||
      !json_span_copy(job_id, g_mining_job.job_id, sizeof(g_mining_job.job_id)) ||
      !json_span_copy(ntime, g_mining_job.ntime, sizeof(g_mining_job.ntime))) {
    return false;
  }

  const bool block_hash_changed = !json_span_equals_cstr(prev_hash, g_stratum_runtime.current_block);
  if (block_hash_changed) {
    miner_clear_work();
  }

  if (clean_jobs) {
    extra_nonce2_increment(g_mining_job.extra_nonce2, g_mining_job.extra_nonce2_size);
  }
  const json_span_t extra_nonce1 = {
      .start = g_mining_job.extra_nonce1,
      .len = g_mining_job.extra_nonce1_len,
  };
  if (!hex_span_to_bytes(coinbase1, coinbase, &offset, sizeof(coinbase)) ||
      !hex_span_to_bytes(extra_nonce1, coinbase, &offset, sizeof(coinbase)) ||
      !append_extra_nonce2_le_bytes(g_mining_job.extra_nonce2,
                                    g_mining_job.extra_nonce2_size,
                                    coinbase,
                                    &offset,
                                    sizeof(coinbase)) ||
      !hex_span_to_bytes(coinbase2, coinbase, &offset, sizeof(coinbase))) {
    return false;
  }

  sha256d(coinbase, offset, merkle);

  if (values[4].len >= 2 && values[4].start[0] == '[') {
    json_span_t branch_array = {
        .start = values[4].start + 1,
        .len = values[4].len - 2,
    };
    const size_t branch_count = json_split_array(branch_array, branches, STRATUM_MAX_BRANCHES + 1);
    if (branch_count > STRATUM_MAX_BRANCHES) {
      return false;
    }
    for (size_t i = 0; i < branch_count; ++i) {
      uint8_t combined[64];
      json_span_t branch_hex;
      size_t branch_offset = 32;
      if (!json_unquoted_span(branches[i], &branch_hex) ||
          !hex_span_to_bytes(branch_hex, combined, &branch_offset, sizeof(combined)) ||
          branch_offset != sizeof(combined)) {
        return false;
      }
      memcpy(combined, merkle, 32);
      sha256d(combined, sizeof(combined), merkle);
    }
  }

  if (!hex_span_to_reversed(version, g_mining_job.header, 4) ||
      !hex_span_to_word_swapped32(prev_hash, g_mining_job.header + 4) ||
      !hex_span_to_reversed(ntime, g_mining_job.header + 68, 4) ||
      !hex_span_to_reversed(nbits, g_mining_job.header + 72, 4)) {
    return false;
  }
  memcpy(g_mining_job.header + 36, merkle, 32);
  memset(g_mining_job.header + 76, 0, 4);
  prepare_job_hashing(&g_mining_job);
  set_job_share_target(&g_mining_job, g_stratum_runtime.current_difficulty);
  g_mining_job.next_nonce = 0;
  g_mining_job.ready = true;
  if (!json_span_copy(prev_hash, g_stratum_runtime.current_block, sizeof(g_stratum_runtime.current_block))) {
    g_stratum_runtime.current_block[0] = '\0';
  }
  miner_publish_current_job(false);
  return true;
}

static void stratum_parse_notify(const char* line) {
  json_span_t values[9];
  if (!stratum_notify_values(line, values) || !stratum_prepare_job(values, 9)) {
    g_mining_job.ready = false;
    miner_clear_work();
  }
}

static int stratum_parse_id(const char* line) {
  const char* id = strstr(line, "\"id\"");
  if (id == NULL) {
    return -1;
  }
  const char* colon = strchr(id, ':');
  if (colon == NULL) {
    return -1;
  }
  ++colon;
  while (*colon == ' ' || *colon == '\t') {
    ++colon;
  }
  if (strncmp(colon, "null", 4) == 0) {
    return -1;
  }
  int value = 0;
  bool found = false;
  while (*colon >= '0' && *colon <= '9') {
    found = true;
    value = (value * 10) + (*colon - '0');
    ++colon;
  }
  return found ? value : -1;
}

static int stratum_parse_result_bool(const char* line) {
  const char* result = strstr(line, "\"result\"");
  if (result == NULL) {
    return -1;
  }
  const char* colon = strchr(result, ':');
  if (colon == NULL) {
    return -1;
  }
  ++colon;
  while (*colon == ' ' || *colon == '\t') {
    ++colon;
  }
  if (strncmp(colon, "true", 4) == 0) {
    return 1;
  }
  if (strncmp(colon, "false", 5) == 0 || strncmp(colon, "null", 4) == 0) {
    return 0;
  }
  return -1;
}

static bool stratum_response_has_error(const char* line) {
  const char* error = strstr(line, "\"error\"");
  if (error == NULL) {
    return false;
  }
  const char* colon = strchr(error, ':');
  if (colon == NULL) {
    return false;
  }
  ++colon;
  while (*colon == ' ' || *colon == '\t') {
    ++colon;
  }
  return strncmp(colon, "null", 4) != 0;
}
