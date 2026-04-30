/* Private implementation fragment included by stratum_miner.c. */

static void miner_add_hashes(uint32_t hashes) {
  portENTER_CRITICAL(&g_miner_stats_mux);
  g_stratum_runtime.hashes_total += hashes;
  portEXIT_CRITICAL(&g_miner_stats_mux);
}

static void miner_reset_hashrate_sample(bool zero_visible_hashrate) {
  portENTER_CRITICAL(&g_miner_stats_mux);
  g_hashrate_sample_us = 0;
  g_hashrate_sample_total = g_stratum_runtime.hashes_total;
  if (zero_visible_hashrate) {
    g_stratum_runtime.hashes_per_second = 0;
  }
  portEXIT_CRITICAL(&g_miner_stats_mux);
}

static void miner_clear_work(void) {
  portENTER_CRITICAL(&g_miner_work_mux);
  g_miner_work.active = false;
  ++g_miner_work.sequence;
  portEXIT_CRITICAL(&g_miner_work_mux);
  share_queue_clear();
}

static void miner_publish_current_job(bool preserve_nonce) {
  const bool ready = g_mining_job.ready;
  bool started_new_work = false;
  portENTER_CRITICAL(&g_miner_work_mux);
  const uint32_t next_nonce = preserve_nonce && g_miner_work.active ? g_miner_work.next_nonce : 0;
  const bool was_active = g_miner_work.active;
  ++g_miner_work.sequence;
  g_miner_work.active = ready;
  started_new_work = ready && !was_active;
  if (ready) {
    memcpy(g_miner_work.midstate, g_mining_job.header_midstate, sizeof(g_miner_work.midstate));
    memcpy(g_miner_work.tail_words, g_mining_job.header_tail_words, sizeof(g_miner_work.tail_words));
    synthetic_hw_prepare_blocks(g_miner_work.hw_block0, g_miner_work.hw_block1, g_mining_job.header);
    memcpy(g_miner_work.target_words, g_mining_job.share_target_words, sizeof(g_miner_work.target_words));
    g_miner_work.target_word0 = g_mining_job.share_target_words[0];
    g_miner_work.next_nonce = next_nonce;
    g_miner_work.extra_nonce2_size = g_mining_job.extra_nonce2_size;
    memcpy(g_miner_work.extra_nonce2,
           g_mining_job.extra_nonce2,
           sizeof(g_miner_work.extra_nonce2));
    copy_str(g_miner_work.block_hash, sizeof(g_miner_work.block_hash), g_stratum_runtime.current_block);
    copy_str(g_miner_work.job_id, sizeof(g_miner_work.job_id), g_mining_job.job_id);
    copy_str(g_miner_work.ntime, sizeof(g_miner_work.ntime), g_mining_job.ntime);
  }
  portEXIT_CRITICAL(&g_miner_work_mux);
  if (started_new_work) {
    miner_reset_hashrate_sample(false);
  }
  if (ready) {
    miner_wake_tasks();
  }
}

static bool miner_claim_range(miner_work_t* out, uint32_t* start_nonce, uint32_t nonce_count) {
  bool has_work = false;
  portENTER_CRITICAL(&g_miner_work_mux);
  if (g_miner_work.active) {
    *out = g_miner_work;
    *start_nonce = g_miner_work.next_nonce;
    g_miner_work.next_nonce += nonce_count;
    has_work = true;
  }
  portEXIT_CRITICAL(&g_miner_work_mux);
  return has_work;
}

static bool miner_claim_batch(
    miner_work_t* out,
    uint32_t* start_nonce,
    uint32_t* claimed_nonce_count) {
#if STRATUM_ASM_CANDIDATE_LOOP
  *claimed_nonce_count = STRATUM_MINE_BATCH + STRATUM_MINE_CANDIDATE_GUARD_NONCES;
#else
  *claimed_nonce_count = STRATUM_MINE_BATCH;
#endif
  return miner_claim_range(out, start_nonce, *claimed_nonce_count);
}

static void miner_return_unscanned(
    const miner_work_t* work,
    uint32_t resume_nonce,
    uint32_t claimed_end_nonce) {
  portENTER_CRITICAL(&g_miner_work_mux);
  if (g_miner_work.active &&
      g_miner_work.sequence == work->sequence &&
      g_miner_work.next_nonce == claimed_end_nonce) {
    g_miner_work.next_nonce = resume_nonce;
  }
  portEXIT_CRITICAL(&g_miner_work_mux);
}

static void miner_advance_overrun_claim(
    const miner_work_t* work,
    uint32_t scanned_end_nonce,
    uint32_t claimed_end_nonce) {
  portENTER_CRITICAL(&g_miner_work_mux);
  if (g_miner_work.active &&
      g_miner_work.sequence == work->sequence &&
      g_miner_work.next_nonce == claimed_end_nonce) {
    g_miner_work.next_nonce = scanned_end_nonce;
  }
  portEXIT_CRITICAL(&g_miner_work_mux);
}

static inline __attribute__((always_inline)) miner_scan_result_t IRAM_ATTR miner_scan_hw_range_core(
    const miner_work_t* work,
    uint32_t start_nonce,
    uint32_t nonce_count,
    bool enqueue_shares,
    uint32_t* checksum) {
  uint32_t* reg = (uint32_t*)SHA_TEXT_BASE;
  uint32_t* cmd = (uint32_t*)SHA_256_START_REG;
  uint32_t* apb = (uint32_t*)0x3ff40078UL;
  const uint32_t cmd_one = 1;
  const uint32_t b00 = work->hw_block0[0];
  const uint32_t b01 = work->hw_block0[1];
  const uint32_t b02 = work->hw_block0[2];
  const uint32_t b03 = work->hw_block0[3];
  const uint32_t b04 = work->hw_block0[4];
  const uint32_t b05 = work->hw_block0[5];
  const uint32_t b06 = work->hw_block0[6];
  const uint32_t b07 = work->hw_block0[7];
  const uint32_t b08 = work->hw_block0[8];
  const uint32_t b09 = work->hw_block0[9];
  const uint32_t b0a = work->hw_block0[10];
  const uint32_t b0b = work->hw_block0[11];
  const uint32_t b0c = work->hw_block0[12];
  const uint32_t b0d = work->hw_block0[13];
  const uint32_t b0e = work->hw_block0[14];
  const uint32_t b0f = work->hw_block0[15];
  const uint32_t b10 = work->hw_block1[0];
  const uint32_t b11 = work->hw_block1[1];
  const uint32_t b12 = work->hw_block1[2];
  const uint32_t t0 = work->target_word0;
  const uint32_t t1 = work->target_words[1];
  const uint32_t t2 = work->target_words[2];
  const uint32_t t3 = work->target_words[3];
  const uint32_t t4 = work->target_words[4];
  const uint32_t t5 = work->target_words[5];
  const uint32_t t6 = work->target_words[6];
  const uint32_t t7 = work->target_words[7];
  const uint32_t candidate_t0 =
      enqueue_shares ? miner_candidate_target_word0(t0) : t0;
  const uint32_t candidate_raw_zero_mask = miner_candidate_raw_zero_mask(candidate_t0);
  miner_scan_result_t result = {0};
  uint32_t i = 0;
  uint32_t published_hashes = 0;

#define MINER_HW_WRITE_BLOCK0() \
  do { \
    SHA_TEXT_STORE(reg, 0, b00); \
    SHA_TEXT_STORE(reg, 4, b01); \
    SHA_TEXT_STORE(reg, 8, b02); \
    SHA_TEXT_STORE(reg, 12, b03); \
    SHA_TEXT_STORE(reg, 16, b04); \
    SHA_TEXT_STORE(reg, 20, b05); \
    SHA_TEXT_STORE(reg, 24, b06); \
    SHA_TEXT_STORE(reg, 28, b07); \
    SHA_TEXT_STORE(reg, 32, b08); \
    SHA_TEXT_STORE(reg, 36, b09); \
    SHA_TEXT_STORE(reg, 40, b0a); \
    SHA_TEXT_STORE(reg, 44, b0b); \
    SHA_TEXT_STORE(reg, 48, b0c); \
    SHA_TEXT_STORE(reg, 52, b0d); \
    SHA_TEXT_STORE(reg, 56, b0e); \
    SHA_TEXT_STORE(reg, 60, b0f); \
    SHA_TEXT_MEMW(); \
  } while (false)

#define MINER_HW_PREP_NEXT_SWAPPED(next_swapped_nonce, swapped_nonce_value) \
  do { \
    (next_swapped_nonce) = (swapped_nonce_value) + 0x01000000UL; \
    asm volatile("" : "+r"(next_swapped_nonce)); \
  } while (false)

#define MINER_HW_SCAN_NONCE_SWAPPED_PREP(nonce_value, swapped_nonce_value, prep_next_nonce) \
  do { \
    MINER_HW_WRITE_BLOCK0(); \
    SHA_CMD_STORE(cmd, 0, cmd_one); \
    SHA_TEXT_STORE(reg, 0, b10); \
    SHA_TEXT_STORE(reg, 4, b11); \
    SHA_TEXT_STORE(reg, 8, b12); \
    SHA_TEXT_STORE(reg, 12, (swapped_nonce_value)); \
    SHA_TEXT_STORE(reg, 16, 0x80000000UL); \
    SHA_TEXT_STORE(reg, 20, 0); \
    SHA_TEXT_STORE(reg, 24, 0); \
    SHA_TEXT_STORE(reg, 28, 0); \
    SHA_TEXT_STORE(reg, 32, 0); \
    SHA_TEXT_STORE(reg, 36, 0); \
    SHA_TEXT_STORE(reg, 40, 0); \
    SHA_TEXT_STORE(reg, 44, 0); \
    SHA_TEXT_STORE(reg, 48, 0); \
    SHA_TEXT_STORE(reg, 52, 0); \
    SHA_TEXT_STORE(reg, 56, 0); \
    SHA_TEXT_STORE(reg, 60, 80UL * 8UL); \
    SHA_TEXT_MEMW(); \
    synthetic_hw_sha_wait_idle(); \
    SHA_CMD_STORE(cmd, 4, cmd_one); \
    prep_next_nonce; \
    synthetic_hw_sha_wait_idle(); \
    SHA_CMD_STORE(cmd, 8, cmd_one); \
    synthetic_hw_sha_wait_idle(); \
    SHA_TEXT_STORE(reg, 32, 0x80000000UL); \
    SHA_TEXT_STORE(reg, 60, 32UL * 8UL); \
    SHA_TEXT_MEMW(); \
    SHA_CMD_STORE(cmd, 0, cmd_one); \
    synthetic_hw_sha_wait_idle(); \
    SHA_CMD_STORE(cmd, 8, cmd_one); \
    synthetic_hw_sha_wait_idle(); \
    const uint32_t raw_word0 = SHA_TEXT_READ(reg, apb, 28); \
    if (UNLIKELY((raw_word0 & candidate_raw_zero_mask) == 0)) { \
      const uint32_t compare_word0 = byteswap32(raw_word0); \
      if (LIKELY(compare_word0 > candidate_t0)) { \
        break; \
      } \
      bool meets_target = compare_word0 < t0; \
      if (!meets_target) { \
        DPORT_INTERRUPT_DISABLE(); \
        const uint32_t h1 = byteswap32(SHA_TEXT_READ(reg, apb, 24)); \
        if (h1 != t1) { \
          meets_target = h1 < t1; \
        } else { \
          const uint32_t h2 = byteswap32(SHA_TEXT_READ(reg, apb, 20)); \
          if (h2 != t2) { \
            meets_target = h2 < t2; \
          } else { \
            const uint32_t h3 = byteswap32(SHA_TEXT_READ(reg, apb, 16)); \
            if (h3 != t3) { \
              meets_target = h3 < t3; \
            } else { \
              const uint32_t h4 = byteswap32(SHA_TEXT_READ(reg, apb, 12)); \
              if (h4 != t4) { \
                meets_target = h4 < t4; \
              } else { \
                const uint32_t h5 = byteswap32(SHA_TEXT_READ(reg, apb, 8)); \
                if (h5 != t5) { \
                  meets_target = h5 < t5; \
                } else { \
                  const uint32_t h6 = byteswap32(SHA_TEXT_READ(reg, apb, 4)); \
                  if (h6 != t6) { \
                    meets_target = h6 < t6; \
                  } else { \
                    const uint32_t h7 = byteswap32(SHA_TEXT_READ(reg, apb, 0)); \
                    meets_target = h7 <= t7; \
                  } \
                } \
              } \
            } \
          } \
        } \
        DPORT_INTERRUPT_RESTORE(); \
      } \
      if (UNLIKELY(meets_target)) { \
        if (enqueue_shares) { \
          const miner_candidate_result_t candidate_result = \
              miner_push_runtime_candidate(work, (nonce_value), compare_word0, reg, apb); \
          if (UNLIKELY(candidate_result != MINER_CANDIDATE_NONE)) { \
            ++result.candidates; \
          } \
          if (UNLIKELY(candidate_result == MINER_CANDIDATE_QUEUE_FULL)) { \
            result.queue_full = true; \
          } else if (UNLIKELY(candidate_result == MINER_CANDIDATE_STALE)) { \
            result.stale_work = true; \
          } \
        } else { \
          ++result.candidates; \
          if (checksum != NULL) { \
            *checksum ^= (nonce_value); \
          } \
        } \
      } \
    } \
  } while (false)

#define MINER_HW_SCAN_NONCE_SWAPPED(nonce_value, swapped_nonce_value) \
  MINER_HW_SCAN_NONCE_SWAPPED_PREP((nonce_value), (swapped_nonce_value), (void)0)

#define MINER_HW_SCAN_NONCE(nonce_value) \
  MINER_HW_SCAN_NONCE_SWAPPED((nonce_value), byteswap32((nonce_value)))

  while (i < nonce_count) {
    if (enqueue_shares && UNLIKELY(!miner_work_sequence_is_current(work))) {
      result.stale_work = true;
      break;
    }
    uint32_t scan_end = nonce_count;
    const uint32_t stale_check_end = i + STRATUM_STALE_WORK_CHECK_NONCES;
    if (scan_end > stale_check_end) {
      scan_end = stale_check_end;
    }
    if (enqueue_shares) {
      const uint32_t room = share_queue_room_fast();
      if (room == 0) {
        result.queue_full = true;
        break;
      }
    }
    for (; i < scan_end && ((start_nonce + i) & 7U) != 0 &&
           !result.queue_full && !result.stale_work; ++i) {
      MINER_HW_SCAN_NONCE(start_nonce + i);
    }
    for (; i + 7 < scan_end && !result.queue_full && !result.stale_work;) {
      uint32_t nonce0 = start_nonce + i;
      uint32_t swapped_nonce0 = byteswap32(nonce0);
      uint32_t roll_end = scan_end;
      const uint32_t wrap_end = i + (256U - (nonce0 & 0xffU));
      if (roll_end > wrap_end) {
        roll_end = wrap_end;
      }
      for (; i + 7 < roll_end;) {
        uint32_t swapped_nonce1;
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0, swapped_nonce0,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce1, swapped_nonce0));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 1, swapped_nonce1,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce0, swapped_nonce1));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 2, swapped_nonce0,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce1, swapped_nonce0));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 3, swapped_nonce1,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce0, swapped_nonce1));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 4, swapped_nonce0,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce1, swapped_nonce0));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 5, swapped_nonce1,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce0, swapped_nonce1));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 6, swapped_nonce0,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce1, swapped_nonce0));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 7, swapped_nonce1,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce0, swapped_nonce1));
        i += 8;
        if (UNLIKELY(result.queue_full || result.stale_work)) {
          break;
        }
        nonce0 += 8;
      }
    }
    for (; i < scan_end && !result.queue_full && !result.stale_work; ++i) {
      MINER_HW_SCAN_NONCE(start_nonce + i);
    }
    if (enqueue_shares && i - published_hashes >= STRATUM_HASH_PROGRESS_NONCES) {
      miner_add_hashes(i - published_hashes);
      published_hashes = i;
    }
    if (!enqueue_shares || result.queue_full || result.stale_work) {
      break;
    }
  }

#undef MINER_HW_SCAN_NONCE
#undef MINER_HW_SCAN_NONCE_SWAPPED
#undef MINER_HW_SCAN_NONCE_SWAPPED_PREP
#undef MINER_HW_PREP_NEXT_SWAPPED
#undef MINER_HW_WRITE_BLOCK0
  if (enqueue_shares && i > published_hashes) {
    miner_add_hashes(i - published_hashes);
  }
  result.hashes = i;
  return result;
}

static miner_scan_result_t IRAM_ATTR miner_scan_batch(const miner_work_t* work, uint32_t start_nonce) {
  uint32_t* reg = (uint32_t*)SHA_TEXT_BASE;
  uint32_t* apb = (uint32_t*)0x3ff40078UL;
  const uint32_t t0 = work->target_word0;
  const uint32_t candidate_t0 = miner_candidate_target_word0(t0);
  const uint32_t candidate_raw_zero_mask = miner_candidate_raw_zero_mask(candidate_t0);
#if STRATUM_ASM_CANDIDATE_LOOP
  if (UNLIKELY((candidate_raw_zero_mask & 0xffU) != 0xffU)) {
    return miner_scan_hw_range_core(work, start_nonce, STRATUM_MINE_BATCH, true, NULL);
  }
#endif
#if !STRATUM_ASM_CANDIDATE_LOOP
  uint32_t* cmd = (uint32_t*)SHA_256_START_REG;
  const uint32_t cmd_one = 1;
  const uint32_t b00 = work->hw_block0[0];
  const uint32_t b01 = work->hw_block0[1];
  const uint32_t b02 = work->hw_block0[2];
  const uint32_t b03 = work->hw_block0[3];
  const uint32_t b04 = work->hw_block0[4];
  const uint32_t b05 = work->hw_block0[5];
  const uint32_t b06 = work->hw_block0[6];
  const uint32_t b07 = work->hw_block0[7];
  const uint32_t b08 = work->hw_block0[8];
  const uint32_t b09 = work->hw_block0[9];
  const uint32_t b0a = work->hw_block0[10];
  const uint32_t b0b = work->hw_block0[11];
  const uint32_t b0c = work->hw_block0[12];
  const uint32_t b0d = work->hw_block0[13];
  const uint32_t b0e = work->hw_block0[14];
  const uint32_t b0f = work->hw_block0[15];
  const uint32_t b10 = work->hw_block1[0];
  const uint32_t b11 = work->hw_block1[1];
  const uint32_t b12 = work->hw_block1[2];
#endif
  const uint32_t nonce_count = STRATUM_MINE_BATCH;
  miner_scan_result_t result = {0};
  uint32_t i = 0;
  uint32_t published_hashes = 0;

#define MINER_HW_WRITE_BLOCK0() \
  do { \
    SHA_TEXT_STORE(reg, 0, b00); \
    SHA_TEXT_STORE(reg, 4, b01); \
    SHA_TEXT_STORE(reg, 8, b02); \
    SHA_TEXT_STORE(reg, 12, b03); \
    SHA_TEXT_STORE(reg, 16, b04); \
    SHA_TEXT_STORE(reg, 20, b05); \
    SHA_TEXT_STORE(reg, 24, b06); \
    SHA_TEXT_STORE(reg, 28, b07); \
    SHA_TEXT_STORE(reg, 32, b08); \
    SHA_TEXT_STORE(reg, 36, b09); \
    SHA_TEXT_STORE(reg, 40, b0a); \
    SHA_TEXT_STORE(reg, 44, b0b); \
    SHA_TEXT_STORE(reg, 48, b0c); \
    SHA_TEXT_STORE(reg, 52, b0d); \
    SHA_TEXT_STORE(reg, 56, b0e); \
    SHA_TEXT_STORE(reg, 60, b0f); \
    SHA_TEXT_MEMW(); \
  } while (false)

#define MINER_HW_PREP_NEXT_SWAPPED(next_swapped_nonce, swapped_nonce_value) \
  do { \
    (next_swapped_nonce) = (swapped_nonce_value) + 0x01000000UL; \
    asm volatile("" : "+r"(next_swapped_nonce)); \
  } while (false)

#define MINER_HW_SCAN_NONCE_SWAPPED_PREP(nonce_value, swapped_nonce_value, prep_next_nonce) \
  do { \
    MINER_HW_WRITE_BLOCK0(); \
    SHA_CMD_STORE(cmd, 0, cmd_one); \
    SHA_TEXT_STORE(reg, 0, b10); \
    SHA_TEXT_STORE(reg, 4, b11); \
    SHA_TEXT_STORE(reg, 8, b12); \
    SHA_TEXT_STORE(reg, 12, (swapped_nonce_value)); \
    SHA_TEXT_STORE(reg, 16, 0x80000000UL); \
    SHA_TEXT_STORE(reg, 20, 0); \
    SHA_TEXT_STORE(reg, 24, 0); \
    SHA_TEXT_STORE(reg, 28, 0); \
    SHA_TEXT_STORE(reg, 32, 0); \
    SHA_TEXT_STORE(reg, 36, 0); \
    SHA_TEXT_STORE(reg, 40, 0); \
    SHA_TEXT_STORE(reg, 44, 0); \
    SHA_TEXT_STORE(reg, 48, 0); \
    SHA_TEXT_STORE(reg, 52, 0); \
    SHA_TEXT_STORE(reg, 56, 0); \
    SHA_TEXT_STORE(reg, 60, 80UL * 8UL); \
    SHA_TEXT_MEMW(); \
    synthetic_hw_sha_wait_idle(); \
    SHA_CMD_STORE(cmd, 4, cmd_one); \
    prep_next_nonce; \
    synthetic_hw_sha_wait_idle(); \
    SHA_CMD_STORE(cmd, 8, cmd_one); \
    synthetic_hw_sha_wait_idle(); \
    SHA_TEXT_STORE(reg, 32, 0x80000000UL); \
    SHA_TEXT_STORE(reg, 60, 32UL * 8UL); \
    SHA_TEXT_MEMW(); \
    SHA_CMD_STORE(cmd, 0, cmd_one); \
    synthetic_hw_sha_wait_idle(); \
    SHA_CMD_STORE(cmd, 8, cmd_one); \
    synthetic_hw_sha_wait_idle(); \
    const uint32_t raw_word0 = SHA_TEXT_READ(reg, apb, 28); \
    if (UNLIKELY((raw_word0 & candidate_raw_zero_mask) == 0)) { \
      const uint32_t compare_word0 = byteswap32(raw_word0); \
      if (LIKELY(compare_word0 > candidate_t0)) { \
        break; \
      } \
      const miner_candidate_result_t candidate_result = \
          miner_push_runtime_candidate(work, (nonce_value), compare_word0, reg, apb); \
      if (UNLIKELY(candidate_result != MINER_CANDIDATE_NONE)) { \
        ++result.candidates; \
        if (UNLIKELY(candidate_result == MINER_CANDIDATE_QUEUE_FULL)) { \
          result.queue_full = true; \
        } else if (UNLIKELY(candidate_result == MINER_CANDIDATE_STALE)) { \
          result.stale_work = true; \
        } \
      } \
    } \
  } while (false)

#define MINER_HW_SCAN_NONCE_SWAPPED(nonce_value, swapped_nonce_value) \
  MINER_HW_SCAN_NONCE_SWAPPED_PREP((nonce_value), (swapped_nonce_value), (void)0)

#define MINER_HW_SCAN_NONCE(nonce_value) \
  MINER_HW_SCAN_NONCE_SWAPPED((nonce_value), byteswap32((nonce_value)))

  while (i < nonce_count) {
    if (UNLIKELY(!miner_work_sequence_is_current(work))) {
      result.stale_work = true;
      break;
    }
    uint32_t scan_end = nonce_count;
    const uint32_t stale_check_end = i + STRATUM_STALE_WORK_CHECK_NONCES;
    if (scan_end > stale_check_end) {
      scan_end = stale_check_end;
    }
    const uint32_t room = share_queue_room_fast();
    if (room == 0) {
      result.queue_full = true;
      break;
    }
#if STRATUM_ASM_CANDIDATE_LOOP
    while (i < scan_end && !result.queue_full && !result.stale_work) {
      const uint32_t nonce_base = start_nonce + i;
      const uint32_t scanned_hashes =
          miner_scan_until_candidate_asm(work, nonce_base, candidate_raw_zero_mask);
      i += scanned_hashes;
      const uint32_t candidate_nonce = start_nonce + i - 1U;
      const uint32_t compare_word0 = byteswap32(SHA_TEXT_READ(reg, apb, 28));
      if (compare_word0 > candidate_t0) {
        continue;
      }
      const miner_candidate_result_t candidate_result =
          miner_push_runtime_candidate(work, candidate_nonce, compare_word0, reg, apb);
      if (UNLIKELY(candidate_result != MINER_CANDIDATE_NONE)) {
        ++result.candidates;
        if (UNLIKELY(candidate_result == MINER_CANDIDATE_QUEUE_FULL)) {
          result.queue_full = true;
        } else if (UNLIKELY(candidate_result == MINER_CANDIDATE_STALE)) {
          result.stale_work = true;
        }
      }
    }
#else
    for (; i < scan_end && ((start_nonce + i) & 7U) != 0 &&
           !result.queue_full && !result.stale_work; ++i) {
      MINER_HW_SCAN_NONCE(start_nonce + i);
    }
    for (; i + 7 < scan_end && !result.queue_full && !result.stale_work;) {
      uint32_t nonce0 = start_nonce + i;
      uint32_t swapped_nonce0 = byteswap32(nonce0);
      uint32_t roll_end = scan_end;
      const uint32_t wrap_end = i + (256U - (nonce0 & 0xffU));
      if (roll_end > wrap_end) {
        roll_end = wrap_end;
      }
      for (; i + 7 < roll_end;) {
        uint32_t swapped_nonce1;
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0, swapped_nonce0,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce1, swapped_nonce0));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 1, swapped_nonce1,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce0, swapped_nonce1));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 2, swapped_nonce0,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce1, swapped_nonce0));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 3, swapped_nonce1,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce0, swapped_nonce1));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 4, swapped_nonce0,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce1, swapped_nonce0));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 5, swapped_nonce1,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce0, swapped_nonce1));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 6, swapped_nonce0,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce1, swapped_nonce0));
        MINER_HW_SCAN_NONCE_SWAPPED_PREP(
            nonce0 + 7, swapped_nonce1,
            MINER_HW_PREP_NEXT_SWAPPED(swapped_nonce0, swapped_nonce1));
        i += 8;
        if (UNLIKELY(result.queue_full || result.stale_work)) {
          break;
        }
        nonce0 += 8;
      }
    }
    for (; i < scan_end && !result.queue_full && !result.stale_work; ++i) {
      MINER_HW_SCAN_NONCE(start_nonce + i);
    }
#endif
    if (i - published_hashes >= STRATUM_HASH_PROGRESS_NONCES) {
      miner_add_hashes(i - published_hashes);
      published_hashes = i;
    }
    if (result.queue_full || result.stale_work) {
      break;
    }
  }

#undef MINER_HW_SCAN_NONCE
#undef MINER_HW_SCAN_NONCE_SWAPPED
#undef MINER_HW_SCAN_NONCE_SWAPPED_PREP
#undef MINER_HW_PREP_NEXT_SWAPPED
#undef MINER_HW_WRITE_BLOCK0
  if (i > published_hashes) {
    miner_add_hashes(i - published_hashes);
  }
  result.hashes = i;
  return result;
}

static miner_scan_result_t IRAM_ATTR miner_scan_hw_benchmark_range(
    const miner_work_t* work,
    uint32_t start_nonce,
    uint32_t nonce_count,
    uint32_t* checksum) {
  return miner_scan_hw_range_core(work, start_nonce, nonce_count, false, checksum);
}

static miner_scan_result_t IRAM_ATTR miner_scan_software_range(
    const miner_work_t* work,
    uint32_t start_nonce,
    uint32_t nonce_count) {
  miner_scan_result_t result = {0};
  uint32_t i = 0;
  uint32_t candidates = 0;
  uint32_t published_hashes = 0;
  bool queue_full = false;
  bool stale_work = false;
  const uint32_t* midstate = work->midstate;
  const uint32_t* tail_words = work->tail_words;
  const uint32_t tail0 = tail_words[0];
  const uint32_t tail1 = tail_words[1];
  const uint32_t tail2 = tail_words[2];
  const uint32_t t0 = work->target_word0;
  const uint32_t raw_word0_zero_mask = miner_candidate_raw_zero_mask(t0);

  while (i < nonce_count) {
    if (!miner_work_sequence_is_current(work)) {
      stale_work = true;
      break;
    }
    if (share_queue_room_fast() == 0) {
      queue_full = true;
      break;
    }

    uint32_t scan_end = nonce_count;
    const uint32_t stale_check_end =
        i + STRATUM_SOFTWARE_STALE_WORK_CHECK_NONCES;
    if (scan_end > stale_check_end) {
      scan_end = stale_check_end;
    }

    while (i < scan_end && !queue_full && !stale_work) {
      const uint32_t nonce_base = start_nonce + i;
      uint32_t swapped_nonce = byteswap32(nonce_base);
      uint32_t roll_end = scan_end;
      const uint32_t wrap_end = i + (256U - (nonce_base & 0xffU));
      if (roll_end > wrap_end) {
        roll_end = wrap_end;
      }

      for (; i < roll_end;) {
        const uint32_t raw_word0 =
            sha256d_midstate_second_target_word0_raw_swapped(midstate,
                                                             tail0,
                                                             tail1,
                                                             tail2,
                                                             swapped_nonce);
        if (LIKELY((raw_word0 & raw_word0_zero_mask) != 0)) {
          ++i;
          swapped_nonce += 0x01000000UL;
          continue;
        }
        const uint32_t hash_word0 = byteswap32(raw_word0);
        if (LIKELY(hash_word0 > t0)) {
          ++i;
          swapped_nonce += 0x01000000UL;
          continue;
        }

        bool meets_target = hash_word0 < t0;
        if (!meets_target) {
          uint32_t hash_words[8];
          sha256d_midstate_second_state_swapped(midstate,
                                                tail_words,
                                                swapped_nonce,
                                                hash_words);
          meets_target = miner_hash_words_meet_target(work, hash_words);
        }

        if (UNLIKELY(meets_target)) {
          ++candidates;
          const uint32_t nonce = start_nonce + i;
          const miner_candidate_result_t candidate_result =
              miner_push_software_candidate(work, nonce);
          if (candidate_result == MINER_CANDIDATE_QUEUE_FULL) {
            queue_full = true;
            break;
          }
          if (candidate_result == MINER_CANDIDATE_STALE) {
            stale_work = true;
            break;
          }
        }
        ++i;
        swapped_nonce += 0x01000000UL;
      }
    }

    if (i - published_hashes >= STRATUM_SOFTWARE_STALE_WORK_CHECK_NONCES) {
      miner_add_hashes(i - published_hashes);
      published_hashes = i;
      taskYIELD();
    }
    if (queue_full || stale_work) {
      break;
    }
  }

  if (i > published_hashes) {
    miner_add_hashes(i - published_hashes);
  }
  result.hashes = i;
  result.candidates = candidates;
  result.queue_full = queue_full;
  result.stale_work = stale_work;
  return result;
}
