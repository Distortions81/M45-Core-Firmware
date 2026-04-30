/* Private implementation fragment included by stratum_miner.c. */

static bool share_queue_ensure_mutex(void) {
  if (g_share_queue_mutex != NULL) {
    return true;
  }
  g_share_queue_mutex = xSemaphoreCreateMutex();
  if (g_share_queue_mutex == NULL) {
    ESP_LOGE(TAG, "Could not create share queue mutex");
    return false;
  }
  return true;
}

static void share_queue_clear(void) {
  if (!share_queue_ensure_mutex()) {
    g_share_queue.head = 0;
    g_share_queue.count = 0;
    return;
  }
  xSemaphoreTake(g_share_queue_mutex, portMAX_DELAY);
  g_share_queue.head = 0;
  g_share_queue.count = 0;
  xSemaphoreGive(g_share_queue_mutex);
}

static bool share_queue_prepare_storage(void) {
  if (!share_queue_ensure_mutex()) {
    return false;
  }
  return true;
}

static bool miner_work_sequence_is_current(const miner_work_t* work) {
  bool current = false;
  portENTER_CRITICAL(&g_miner_work_mux);
  current = g_miner_work.active && g_miner_work.sequence == work->sequence;
  portEXIT_CRITICAL(&g_miner_work_mux);
  return current;
}

static inline uint32_t share_queue_room_fast(void) {
  // The miner task is the only producer; the session task only drains.
  // This is only a fast "already full" check. Target hits are sparse, so free
  // share slots must not cap the number of nonces scanned in a batch.
  const size_t capacity = g_share_queue.capacity;
  const size_t count = g_share_queue.count;
  if (count >= capacity) {
    return 0;
  }
  const size_t room = capacity - count;
  return room > UINT32_MAX ? UINT32_MAX : (uint32_t)room;
}

static inline uint32_t IRAM_ATTR miner_candidate_target_word0(uint32_t target_word0) {
  return target_word0 > STRATUM_MIN_CANDIDATE_TARGET_WORD0 ? target_word0
                                                           : STRATUM_MIN_CANDIDATE_TARGET_WORD0;
}

static inline uint32_t IRAM_ATTR miner_candidate_raw_zero_mask(uint32_t target_word0) {
  const uint32_t leading_zero_bits =
      target_word0 == 0 ? 32U : (uint32_t)__builtin_clz(target_word0);
  if (leading_zero_bits == 0) {
    return 0;
  }
  const uint32_t target_order_mask =
      leading_zero_bits == 32U ? UINT32_MAX : UINT32_MAX << (32U - leading_zero_bits);
  return byteswap32(target_order_mask);
}

#if STRATUM_ASM_CANDIDATE_LOOP
extern uint32_t miner_scan_until_candidate_xtensa(
    const uint32_t* block0,
    const uint32_t* block1,
    uint32_t swapped_nonce,
    uint32_t raw_zero_mask);

static inline __attribute__((always_inline)) uint32_t IRAM_ATTR
miner_scan_until_candidate_asm(
    const miner_work_t* work,
    uint32_t start_nonce,
    uint32_t candidate_raw_zero_mask) {
  const uint32_t packed =
      miner_scan_until_candidate_xtensa(work->hw_block0,
                                        work->hw_block1,
                                        byteswap32(start_nonce),
                                        candidate_raw_zero_mask);
  return packed & 0x7fffffffUL;
}
#endif

static share_queue_push_result_t share_queue_push(const miner_work_t* work, uint32_t nonce) {
  if (!share_queue_ensure_mutex()) {
    return SHARE_QUEUE_PUSH_FULL;
  }
  xSemaphoreTake(g_share_queue_mutex, portMAX_DELAY);
  if (!miner_work_sequence_is_current(work)) {
    xSemaphoreGive(g_share_queue_mutex);
    return SHARE_QUEUE_PUSH_STALE;
  }
  if (g_share_queue.count >= STRATUM_SHARE_QUEUE_MAX) {
    xSemaphoreGive(g_share_queue_mutex);
    return SHARE_QUEUE_PUSH_FULL;
  }
  if (g_share_queue.count == g_share_queue.capacity) {
    xSemaphoreGive(g_share_queue_mutex);
    return SHARE_QUEUE_PUSH_FULL;
  }
  bool best_share_updated = false;
  const size_t write_index = (g_share_queue.head + g_share_queue.count) % g_share_queue.capacity;
  g_share_queue.shares[write_index] = (found_share_t){
      .nonce = nonce,
      .extra_nonce2_size = work->extra_nonce2_size,
  };
  memcpy(g_share_queue.shares[write_index].extra_nonce2,
         work->extra_nonce2,
         sizeof(g_share_queue.shares[write_index].extra_nonce2));
  copy_str(g_share_queue.shares[write_index].job_id,
           sizeof(g_share_queue.shares[write_index].job_id),
           work->job_id);
  copy_str(g_share_queue.shares[write_index].ntime,
           sizeof(g_share_queue.shares[write_index].ntime),
           work->ntime);
  ++g_share_queue.count;
  ++g_stratum_runtime.found_shares;
  const uint32_t share_difficulty = miner_share_difficulty(work, nonce);
  record_share_difficulty(share_difficulty);
  if (share_difficulty > g_stratum_runtime.best_share) {
    g_stratum_runtime.best_share = share_difficulty;
    best_share_updated = true;
  }
  xSemaphoreGive(g_share_queue_mutex);
  if (best_share_updated) {
    save_best_share();
  }
  if (g_stratum_session_task_handle != NULL) {
    xTaskNotifyGive(g_stratum_session_task_handle);
  }
  return SHARE_QUEUE_PUSH_OK;
}

static bool miner_confirm_hardware_candidate(const miner_work_t* work, uint32_t nonce);

static miner_candidate_result_t IRAM_ATTR __attribute__((noinline)) miner_push_runtime_candidate(
    const miner_work_t* work,
    uint32_t nonce,
    uint32_t compare_word0,
    uint32_t* reg,
    uint32_t* apb) {
  if (compare_word0 > work->target_word0) {
    return MINER_CANDIDATE_NONE;
  }
  bool meets_target = compare_word0 < work->target_word0;
  if (!meets_target) {
    DPORT_INTERRUPT_DISABLE();
    const uint32_t h1 = byteswap32(SHA_TEXT_READ(reg, apb, 24));
    if (h1 != work->target_words[1]) {
      meets_target = h1 < work->target_words[1];
    } else {
      const uint32_t h2 = byteswap32(SHA_TEXT_READ(reg, apb, 20));
      if (h2 != work->target_words[2]) {
        meets_target = h2 < work->target_words[2];
      } else {
        const uint32_t h3 = byteswap32(SHA_TEXT_READ(reg, apb, 16));
        if (h3 != work->target_words[3]) {
          meets_target = h3 < work->target_words[3];
        } else {
          const uint32_t h4 = byteswap32(SHA_TEXT_READ(reg, apb, 12));
          if (h4 != work->target_words[4]) {
            meets_target = h4 < work->target_words[4];
          } else {
            const uint32_t h5 = byteswap32(SHA_TEXT_READ(reg, apb, 8));
            if (h5 != work->target_words[5]) {
              meets_target = h5 < work->target_words[5];
            } else {
              const uint32_t h6 = byteswap32(SHA_TEXT_READ(reg, apb, 4));
              if (h6 != work->target_words[6]) {
                meets_target = h6 < work->target_words[6];
              } else {
                const uint32_t h7 = byteswap32(SHA_TEXT_READ(reg, apb, 0));
                meets_target = h7 <= work->target_words[7];
              }
            }
          }
        }
      }
    }
    DPORT_INTERRUPT_RESTORE();
  }
  if (!meets_target) {
    return MINER_CANDIDATE_NONE;
  }
  if (!miner_confirm_hardware_candidate(work, nonce)) {
    return MINER_CANDIDATE_NONE;
  }

  const share_queue_push_result_t push_result = share_queue_push(work, nonce);
  if (UNLIKELY(push_result == SHARE_QUEUE_PUSH_FULL)) {
    return MINER_CANDIDATE_QUEUE_FULL;
  }
  if (UNLIKELY(push_result == SHARE_QUEUE_PUSH_STALE)) {
    return MINER_CANDIDATE_STALE;
  }
  return MINER_CANDIDATE_PUSHED;
}

static void miner_record_hardware_hash_check(bool valid) {
  portENTER_CRITICAL(&g_miner_stats_mux);
  ++g_stratum_runtime.hardware_hash_checks;
  if (!valid) {
    ++g_stratum_runtime.hardware_hash_errors;
  }
  portEXIT_CRITICAL(&g_miner_stats_mux);
}

static bool miner_confirm_hardware_candidate(const miner_work_t* work, uint32_t nonce) {
  uint32_t hash_words[8];
  sha256d_midstate_second_state(work->midstate, work->tail_words, nonce, hash_words);
  const bool valid = miner_hash_words_meet_target(work, hash_words);
  miner_record_hardware_hash_check(valid);
  return valid;
}

static miner_candidate_result_t miner_push_software_candidate(
    const miner_work_t* work,
    uint32_t nonce) {
  const share_queue_push_result_t push_result = share_queue_push(work, nonce);
  if (push_result == SHARE_QUEUE_PUSH_FULL) {
    return MINER_CANDIDATE_QUEUE_FULL;
  }
  if (push_result == SHARE_QUEUE_PUSH_STALE) {
    return MINER_CANDIDATE_STALE;
  }
  return MINER_CANDIDATE_PUSHED;
}

static bool share_queue_pop(found_share_t* out) {
  if (!share_queue_ensure_mutex()) {
    return false;
  }
  xSemaphoreTake(g_share_queue_mutex, portMAX_DELAY);
  if (g_share_queue.count == 0) {
    xSemaphoreGive(g_share_queue_mutex);
    return false;
  }
  *out = g_share_queue.shares[g_share_queue.head];
  g_share_queue.head = (g_share_queue.head + 1) % g_share_queue.capacity;
  --g_share_queue.count;
  if (g_share_queue.count == 0) {
    g_share_queue.head = 0;
  }
  xSemaphoreGive(g_share_queue_mutex);
  return true;
}

static void stratum_drain_share_queue(int sock, uint32_t* submit_id, const char* login) {
  found_share_t share;
  while (share_queue_pop(&share)) {
    const uint32_t id = (*submit_id)++;
    if (stratum_send_submit(sock, id, login, &share)) {
      ++g_stratum_runtime.submitted;
    }
  }
}

static void stratum_handle_line(int sock, const char* line) {
  switch (stratum_line_method(line)) {
    case STRATUM_METHOD_SET_DIFFICULTY:
      stratum_parse_set_difficulty(line);
      return;
    case STRATUM_METHOD_SET_EXTRANONCE:
      stratum_parse_set_extranonce(line);
      return;
    case STRATUM_METHOD_NOTIFY:
      stratum_parse_notify(line);
      return;
    case STRATUM_METHOD_CLIENT_RECONNECT:
      g_stratum_reconnect_requested = true;
      return;
    case STRATUM_METHOD_MINING_PING:
      stratum_send_pong(sock, stratum_parse_id(line));
      return;
    case STRATUM_METHOD_NONE:
      break;
  }

  const int id = stratum_parse_id(line);
  if (id == 1 && strstr(line, "\"result\"") != NULL) {
    stratum_parse_subscribe_response(line);
    return;
  }
  if (id >= 1000) {
    const int result = stratum_parse_result_bool(line);
    if (result > 0) {
      ++g_stratum_runtime.accepted;
    } else if (result == 0 || stratum_response_has_error(line)) {
      ++g_stratum_runtime.rejected;
      ESP_LOGW(TAG, "Rejected share id %u response %s", (unsigned)id, line);
    }
  }
}
