/* Private ESP32-S3 hardware SHA implementation fragment included by stratum_miner.c. */

#define S3_SHA_MEMW() asm volatile("memw" ::: "memory")
#define S3_SHA_STORE(base, byte_offset, value) \
  asm volatile("s32i %0, %1, " #byte_offset : : "r"(value), "r"(base))
#define S3_SHA_READ(base, byte_offset) \
  __extension__({ \
    uint32_t value; \
    asm volatile("l32i %0, %1, " #byte_offset : "=a"(value) : "r"(base) : "memory"); \
    value; \
  })

static inline __attribute__((always_inline)) void IRAM_ATTR s3_hw_sha_wait_idle(void) {
  uint32_t* cmd = (uint32_t*)SHA_START_REG;
  while (S3_SHA_READ(cmd, 8) != 0) {
  }
}

static inline __attribute__((always_inline)) void IRAM_ATTR s3_hw_sha_wait_first_block(void) {
  // A short CPU delay avoids repeatedly polling the slower peripheral bus.
  asm volatile(".rept 64\n"
               "nop\n"
               ".endr\n");
  s3_hw_sha_wait_idle();
}

static inline __attribute__((always_inline)) void IRAM_ATTR s3_hw_sha_write_midstate(
    const uint32_t midstate[8]) {
  uint32_t* h = (uint32_t*)SHA_H_BASE;
  S3_SHA_STORE(h, 0, byteswap32(midstate[0]));
  S3_SHA_STORE(h, 4, byteswap32(midstate[1]));
  S3_SHA_STORE(h, 8, byteswap32(midstate[2]));
  S3_SHA_STORE(h, 12, byteswap32(midstate[3]));
  S3_SHA_STORE(h, 16, byteswap32(midstate[4]));
  S3_SHA_STORE(h, 20, byteswap32(midstate[5]));
  S3_SHA_STORE(h, 24, byteswap32(midstate[6]));
  S3_SHA_STORE(h, 28, byteswap32(midstate[7]));
}

static inline __attribute__((always_inline)) void IRAM_ATTR s3_hw_sha_write_tail_block(
    const uint32_t tail_words[3],
    uint32_t nonce) {
  uint32_t* text = (uint32_t*)SHA_TEXT_BASE;
  S3_SHA_STORE(text, 0, byteswap32(tail_words[0]));
  S3_SHA_STORE(text, 4, byteswap32(tail_words[1]));
  S3_SHA_STORE(text, 8, byteswap32(tail_words[2]));
  S3_SHA_STORE(text, 12, nonce);
  S3_SHA_STORE(text, 16, 0x00000080UL);
  S3_SHA_STORE(text, 20, 0);
  S3_SHA_STORE(text, 24, 0);
  S3_SHA_STORE(text, 28, 0);
  S3_SHA_STORE(text, 32, 0);
  S3_SHA_STORE(text, 36, 0);
  S3_SHA_STORE(text, 40, 0);
  S3_SHA_STORE(text, 44, 0);
  S3_SHA_STORE(text, 48, 0);
  S3_SHA_STORE(text, 52, 0);
  S3_SHA_STORE(text, 56, 0);
  S3_SHA_STORE(text, 60, 0x80020000UL);
}

static inline __attribute__((always_inline)) void IRAM_ATTR s3_hw_sha_write_second_hash_block(void) {
  uint32_t* h = (uint32_t*)SHA_H_BASE;
  uint32_t* text = (uint32_t*)SHA_TEXT_BASE;
  S3_SHA_STORE(text, 0, S3_SHA_READ(h, 0));
  S3_SHA_STORE(text, 4, S3_SHA_READ(h, 4));
  S3_SHA_STORE(text, 8, S3_SHA_READ(h, 8));
  S3_SHA_STORE(text, 12, S3_SHA_READ(h, 12));
  S3_SHA_STORE(text, 16, S3_SHA_READ(h, 16));
  S3_SHA_STORE(text, 20, S3_SHA_READ(h, 20));
  S3_SHA_STORE(text, 24, S3_SHA_READ(h, 24));
  S3_SHA_STORE(text, 28, S3_SHA_READ(h, 28));
  S3_SHA_STORE(text, 32, 0x00000080UL);
  S3_SHA_STORE(text, 36, 0);
  S3_SHA_STORE(text, 40, 0);
  S3_SHA_STORE(text, 44, 0);
  S3_SHA_STORE(text, 48, 0);
  S3_SHA_STORE(text, 52, 0);
  S3_SHA_STORE(text, 56, 0);
  S3_SHA_STORE(text, 60, 0x00010000UL);
}

static inline __attribute__((always_inline)) uint32_t IRAM_ATTR s3_hw_sha256d_target_word0(
    const uint32_t midstate[8],
    const uint32_t tail_words[3],
    uint32_t nonce) {
  uint32_t* cmd = (uint32_t*)SHA_START_REG;
  uint32_t* h = (uint32_t*)SHA_H_BASE;
  s3_hw_sha_write_midstate(midstate);
  s3_hw_sha_write_tail_block(tail_words, nonce);
  S3_SHA_MEMW();
  S3_SHA_STORE(cmd, 4, 1);
  s3_hw_sha_wait_idle();

  s3_hw_sha_write_second_hash_block();
  S3_SHA_MEMW();
  S3_SHA_STORE(cmd, 0, 1);
  s3_hw_sha_wait_idle();
  return S3_SHA_READ(h, 28);
}

static void IRAM_ATTR s3_hw_sha256d_second_state(
    const uint32_t midstate[8],
    const uint32_t tail_words[3],
    uint32_t nonce,
    uint32_t second_state[8]) {
  uint32_t* h = (uint32_t*)SHA_H_BASE;
  (void)s3_hw_sha256d_target_word0(midstate, tail_words, nonce);
  second_state[0] = byteswap32(S3_SHA_READ(h, 0));
  second_state[1] = byteswap32(S3_SHA_READ(h, 4));
  second_state[2] = byteswap32(S3_SHA_READ(h, 8));
  second_state[3] = byteswap32(S3_SHA_READ(h, 12));
  second_state[4] = byteswap32(S3_SHA_READ(h, 16));
  second_state[5] = byteswap32(S3_SHA_READ(h, 20));
  second_state[6] = byteswap32(S3_SHA_READ(h, 24));
  second_state[7] = byteswap32(S3_SHA_READ(h, 28));
}

static inline __attribute__((always_inline)) bool IRAM_ATTR s3_hw_hash_meets_target(
    const miner_work_t* work,
    uint32_t target_word0) {
  if (target_word0 != work->target_word0) {
    return target_word0 < work->target_word0;
  }
  uint32_t* h = (uint32_t*)SHA_H_BASE;
  const uint32_t h1 = S3_SHA_READ(h, 24);
  if (h1 != work->target_words[1]) {
    return h1 < work->target_words[1];
  }
  const uint32_t h2 = S3_SHA_READ(h, 20);
  if (h2 != work->target_words[2]) {
    return h2 < work->target_words[2];
  }
  const uint32_t h3 = S3_SHA_READ(h, 16);
  if (h3 != work->target_words[3]) {
    return h3 < work->target_words[3];
  }
  const uint32_t h4 = S3_SHA_READ(h, 12);
  if (h4 != work->target_words[4]) {
    return h4 < work->target_words[4];
  }
  const uint32_t h5 = S3_SHA_READ(h, 8);
  if (h5 != work->target_words[5]) {
    return h5 < work->target_words[5];
  }
  const uint32_t h6 = S3_SHA_READ(h, 4);
  if (h6 != work->target_words[6]) {
    return h6 < work->target_words[6];
  }
  return S3_SHA_READ(h, 0) <= work->target_words[7];
}

static miner_candidate_result_t IRAM_ATTR __attribute__((noinline))
s3_miner_push_hardware_candidate(const miner_work_t* work, uint32_t nonce) {
  uint32_t hash_words[8];
  uint8_t hash_be[32];
  sha256d_midstate_second_state(work->midstate, work->tail_words, nonce, hash_words);
  const bool valid = miner_hash_words_meet_target(work, hash_words);

  portENTER_CRITICAL(&g_miner_stats_mux);
  ++g_stratum_runtime.hardware_hash_checks;
  if (!valid) {
    ++g_stratum_runtime.hardware_hash_errors;
  }
  portEXIT_CRITICAL(&g_miner_stats_mux);
  if (!valid) {
    return MINER_CANDIDATE_NONE;
  }

  miner_hash_words_to_be(hash_words, hash_be);
  const share_queue_push_result_t push_result =
      share_queue_push_with_hash(work, nonce, hash_be, difficulty_from_hash_be(hash_be));
  if (UNLIKELY(push_result == SHARE_QUEUE_PUSH_FULL)) {
    return MINER_CANDIDATE_QUEUE_FULL;
  }
  if (UNLIKELY(push_result == SHARE_QUEUE_PUSH_STALE)) {
    return MINER_CANDIDATE_STALE;
  }
  return MINER_CANDIDATE_PUSHED;
}

static miner_scan_result_t IRAM_ATTR s3_miner_scan_hw_range(
    const miner_work_t* work,
    uint32_t start_nonce,
    uint32_t nonce_count,
    bool enqueue_shares,
    uint32_t* checksum) {
  uint32_t* cmd = (uint32_t*)SHA_START_REG;
  uint32_t* h = (uint32_t*)SHA_H_BASE;
  uint32_t* text = (uint32_t*)SHA_TEXT_BASE;
  const uint32_t midstate0 = byteswap32(work->midstate[0]);
  const uint32_t midstate1 = byteswap32(work->midstate[1]);
  const uint32_t midstate2 = byteswap32(work->midstate[2]);
  const uint32_t midstate3 = byteswap32(work->midstate[3]);
  const uint32_t midstate4 = byteswap32(work->midstate[4]);
  const uint32_t midstate5 = byteswap32(work->midstate[5]);
  const uint32_t midstate6 = byteswap32(work->midstate[6]);
  const uint32_t midstate7 = byteswap32(work->midstate[7]);
  const uint32_t tail0 = byteswap32(work->tail_words[0]);
  const uint32_t tail1 = byteswap32(work->tail_words[1]);
  const uint32_t tail2 = byteswap32(work->tail_words[2]);
  miner_scan_result_t result = {0};
  const uint32_t candidate_t0 =
      enqueue_shares ? miner_candidate_target_word0(work->target_word0) : work->target_word0;
  uint32_t i = 0;
  uint32_t published_hashes = 0;

#define S3_SHA_WRITE_MIDSTATE() \
  do { \
    S3_SHA_STORE(h, 0, midstate0); \
    S3_SHA_STORE(h, 4, midstate1); \
    S3_SHA_STORE(h, 8, midstate2); \
    S3_SHA_STORE(h, 12, midstate3); \
    S3_SHA_STORE(h, 16, midstate4); \
    S3_SHA_STORE(h, 20, midstate5); \
    S3_SHA_STORE(h, 24, midstate6); \
    S3_SHA_STORE(h, 28, midstate7); \
  } while (false)

#define S3_SHA_WRITE_TAIL_BLOCK(nonce_value) \
  do { \
    S3_SHA_STORE(text, 0, tail0); \
    S3_SHA_STORE(text, 4, tail1); \
    S3_SHA_STORE(text, 8, tail2); \
    S3_SHA_STORE(text, 12, (nonce_value)); \
    S3_SHA_STORE(text, 16, 0x00000080UL); \
    S3_SHA_STORE(text, 20, 0); \
    S3_SHA_STORE(text, 24, 0); \
    S3_SHA_STORE(text, 28, 0); \
    S3_SHA_STORE(text, 32, 0); \
    S3_SHA_STORE(text, 60, 0x80020000UL); \
  } while (false)

#define S3_SHA_WRITE_SECOND_HASH_BLOCK() \
  do { \
    S3_SHA_STORE(text, 0, S3_SHA_READ(h, 0)); \
    S3_SHA_STORE(text, 4, S3_SHA_READ(h, 4)); \
    S3_SHA_STORE(text, 8, S3_SHA_READ(h, 8)); \
    S3_SHA_STORE(text, 12, S3_SHA_READ(h, 12)); \
    S3_SHA_STORE(text, 16, S3_SHA_READ(h, 16)); \
    S3_SHA_STORE(text, 20, S3_SHA_READ(h, 20)); \
    S3_SHA_STORE(text, 24, S3_SHA_READ(h, 24)); \
    S3_SHA_STORE(text, 28, S3_SHA_READ(h, 28)); \
  } while (false)

  while (i < nonce_count) {
    if (enqueue_shares && UNLIKELY(!miner_work_sequence_is_current(work))) {
      result.stale_work = true;
      break;
    }
    if (enqueue_shares && share_queue_room_fast() == 0) {
      result.queue_full = true;
      break;
    }

    uint32_t scan_end = nonce_count;
    const uint32_t stale_check_end = i + STRATUM_STALE_WORK_CHECK_NONCES;
    if (scan_end > stale_check_end) {
      scan_end = stale_check_end;
    }

    S3_SHA_WRITE_TAIL_BLOCK(start_nonce + i);
    for (; i < scan_end;) {
      const uint32_t nonce = start_nonce + i++;
      S3_SHA_WRITE_MIDSTATE();
      S3_SHA_MEMW();
      S3_SHA_STORE(cmd, 4, 1);

      // The first block is latched on CONTINUE. Fill the fixed portion of the
      // second block while its digest is being calculated.
      S3_SHA_STORE(text, 32, 0x00000080UL);
      S3_SHA_STORE(text, 60, 0x00010000UL);
      s3_hw_sha_wait_first_block();

      S3_SHA_WRITE_SECOND_HASH_BLOCK();
      S3_SHA_MEMW();
      S3_SHA_STORE(cmd, 0, 1);

      // The SHA engine latches text on START, so prepare the next nonce while
      // the second hash is running.
      S3_SHA_WRITE_TAIL_BLOCK(start_nonce + i);
      s3_hw_sha_wait_idle();
      const uint32_t target_word0 = S3_SHA_READ(h, 28);
      if (LIKELY(target_word0 > candidate_t0)) {
        continue;
      }
      if (!enqueue_shares) {
        ++result.candidates;
        if (checksum != NULL) {
          *checksum ^= nonce ^ target_word0;
        }
        continue;
      }
      if (!s3_hw_hash_meets_target(work, target_word0)) {
        continue;
      }

      const miner_candidate_result_t candidate_result =
          s3_miner_push_hardware_candidate(work, nonce);
      if (candidate_result != MINER_CANDIDATE_NONE) {
        ++result.candidates;
      }
      if (candidate_result == MINER_CANDIDATE_QUEUE_FULL) {
        result.queue_full = true;
      } else if (candidate_result == MINER_CANDIDATE_STALE) {
        result.stale_work = true;
      }
      if (result.queue_full || result.stale_work) {
        break;
      }
    }

    if (enqueue_shares && i > published_hashes) {
      miner_add_hashes(i - published_hashes);
      published_hashes = i;
      taskYIELD();
    }
    if (!enqueue_shares || result.queue_full || result.stale_work) {
      break;
    }
  }

#undef S3_SHA_WRITE_SECOND_HASH_BLOCK
#undef S3_SHA_WRITE_TAIL_BLOCK
#undef S3_SHA_WRITE_MIDSTATE

  if (enqueue_shares && i > published_hashes) {
    miner_add_hashes(i - published_hashes);
  }
  result.hashes = i;
  return result;
}

static miner_scan_result_t IRAM_ATTR miner_scan_batch(
    const miner_work_t* work, uint32_t start_nonce) {
  return s3_miner_scan_hw_range(work,
                                start_nonce,
                                STRATUM_HARDWARE_MINE_BATCH,
                                true,
                                NULL);
}

static miner_scan_result_t IRAM_ATTR miner_scan_hw_benchmark_range(
    const miner_work_t* work,
    uint32_t start_nonce,
    uint32_t nonce_count,
    uint32_t* checksum) {
  return s3_miner_scan_hw_range(work, start_nonce, nonce_count, false, checksum);
}

#undef S3_SHA_READ
#undef S3_SHA_STORE
#undef S3_SHA_MEMW
