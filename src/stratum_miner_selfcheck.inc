/* Private implementation fragment included by stratum_miner.c. */

typedef struct {
  const char* difficulty;
  uint8_t target[32];
} target_check_t;

typedef struct {
  uint8_t hash_be[32];
  uint32_t difficulty;
} difficulty_check_t;

static void stratum_check_target_case(const target_check_t* check) {
  uint8_t target[32];
  build_share_target(target, check->difficulty);
  if (memcmp(target, check->target, sizeof(target)) != 0) {
    ESP_LOGE(TAG, "share target self-check failed for difficulty %s", check->difficulty);
    abort();
  }

  uint8_t exact_hash_le[32];
  for (size_t i = 0; i < sizeof(exact_hash_le); ++i) {
    exact_hash_le[i] = check->target[31 - i];
  }
  if (!hash_meets_target(exact_hash_le, check->target)) {
    ESP_LOGE(TAG, "share target equality check failed for difficulty %s", check->difficulty);
    abort();
  }

  uint8_t above_hash_le[32];
  memcpy(above_hash_le, exact_hash_le, sizeof(above_hash_le));
  for (size_t i = 0; i < sizeof(above_hash_le); ++i) {
    if (above_hash_le[i] != 0xff) {
      ++above_hash_le[i];
      break;
    }
  }
  if (hash_meets_target(above_hash_le, check->target)) {
    ESP_LOGE(TAG, "share target upper-bound check failed for difficulty %s", check->difficulty);
    abort();
  }
}

static void stratum_check_difficulty_case(const difficulty_check_t* check) {
  const uint32_t difficulty = difficulty_from_hash_be(check->hash_be);
  if (difficulty != check->difficulty) {
    ESP_LOGE(TAG,
             "share difficulty self-check failed: got %lu expected %lu",
             (unsigned long)difficulty,
             (unsigned long)check->difficulty);
    abort();
  }
}

void stratum_self_check(void) {
  static const target_check_t checks[] = {
      {
          .difficulty = "1",
          .target = {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00},
      },
      {
          .difficulty = "0.0001220703125",
          .target = {0x00, 0x00, 0x1f, 0xff, 0xe0, 0x00, 0x00, 0x00},
      },
      {
          .difficulty = "0.000244140625",
          .target = {0x00, 0x00, 0x0f, 0xff, 0xf0, 0x00, 0x00, 0x00},
      },
      {
          .difficulty = "0.00048828125",
          .target = {0x00, 0x00, 0x07, 0xff, 0xf8, 0x00, 0x00, 0x00},
      },
      {
          .difficulty = "0.0009765625",
          .target = {0x00, 0x00, 0x03, 0xff, 0xfc, 0x00, 0x00, 0x00},
      },
  };

  for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i) {
    stratum_check_target_case(&checks[i]);
  }

  static const difficulty_check_t difficulty_checks[] = {
      {
          .hash_be = {0x00, 0x00, 0x00, 0x00, 0xff, 0xff},
          .difficulty = 1,
      },
      {
          .hash_be = {0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0x80},
          .difficulty = 2,
      },
      {
          .hash_be = {0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xc0},
          .difficulty = 4,
      },
  };
  for (size_t i = 0; i < sizeof(difficulty_checks) / sizeof(difficulty_checks[0]); ++i) {
    stratum_check_difficulty_case(&difficulty_checks[i]);
  }

  mining_job_t job = {0};
  uint8_t reference_hash[32];
  uint8_t prepared_hash[32];
  for (size_t i = 0; i < sizeof(job.header); ++i) {
    job.header[i] = (uint8_t)i;
  }
  prepare_job_hashing(&job);
  const uint32_t nonce = 0x7c2bac1dUL;
  job.header[76] = (uint8_t)nonce;
  job.header[77] = (uint8_t)(nonce >> 8);
  job.header[78] = (uint8_t)(nonce >> 16);
  job.header[79] = (uint8_t)(nonce >> 24);
  sha256d(job.header, sizeof(job.header), reference_hash);
  sha256d_miner_header(&job, nonce, prepared_hash);
  if (memcmp(reference_hash, prepared_hash, sizeof(reference_hash)) != 0) {
    ESP_LOGE(TAG, "prepared header hashing self-check failed");
    abort();
  }
  uint32_t software_state[8];
  sha256d_midstate_second_state(job.header_midstate, job.header_tail_words, nonce, software_state);
  const uint32_t software_word0 =
      sha256d_midstate_second_word0_swapped(job.header_midstate,
                                            job.header_tail_words,
                                            byteswap32(nonce));
  if (software_word0 != software_state[0]) {
    ESP_LOGE(TAG, "software sha256d word0 self-check failed");
    abort();
  }
  const uint32_t software_target_word0_raw =
      sha256d_midstate_second_target_word0_raw_swapped(job.header_midstate,
                                                       job.header_tail_words[0],
                                                       job.header_tail_words[1],
                                                       job.header_tail_words[2],
                                                       byteswap32(nonce));
  if (software_target_word0_raw != software_state[7]) {
    ESP_LOGE(TAG, "software sha256d target word0 self-check failed");
    abort();
  }
  uint8_t target_order_hash[32];
  miner_hash_words_to_be(software_state, target_order_hash);
  for (size_t i = 0; i < sizeof(target_order_hash); ++i) {
    if (target_order_hash[i] != reference_hash[sizeof(reference_hash) - 1U - i]) {
      ESP_LOGE(TAG, "share target hash order self-check failed");
      abort();
    }
  }
#if APP_CLASSIC_HARDWARE_SHA_MINER
  uint32_t hw_block0[16];
  uint32_t hw_block1[16];
  uint32_t hardware_state[8];
  synthetic_hw_prepare_blocks(hw_block0, hw_block1, job.header);
  miner_hardware_acquire();
  synthetic_hw_sha256d_second_state(hw_block0, hw_block1, nonce, hardware_state);
  miner_hardware_release();
  if (memcmp(software_state, hardware_state, sizeof(software_state)) != 0) {
    ESP_LOGE(TAG, "hardware sha256d self-check failed");
    abort();
  }
#elif APP_S3_HARDWARE_SHA_MINER
  uint32_t hardware_state[8];
  miner_work_t pipeline_work = {0};
  uint32_t pipeline_checksum = 0;
  uint32_t expected_pipeline_checksum = 0;
  memcpy(pipeline_work.midstate, job.header_midstate, sizeof(pipeline_work.midstate));
  memcpy(pipeline_work.tail_words, job.header_tail_words, sizeof(pipeline_work.tail_words));
  pipeline_work.target_word0 = UINT32_MAX;
  miner_hardware_acquire();
  const uint32_t hardware_target_word0 =
      s3_hw_sha256d_target_word0(job.header_midstate, job.header_tail_words, nonce);
  s3_hw_sha256d_second_state(job.header_midstate,
                             job.header_tail_words,
                             nonce,
                             hardware_state);
  const miner_scan_result_t pipeline_result =
      s3_miner_scan_hw_range(&pipeline_work, nonce, 8, false, &pipeline_checksum);
  miner_hardware_release();
  if (hardware_target_word0 != miner_hash_target_word(software_state, 0)) {
    ESP_LOGE(TAG, "hardware sha256d target word self-check failed");
    abort();
  }
  if (memcmp(software_state, hardware_state, sizeof(software_state)) != 0) {
    ESP_LOGE(TAG, "hardware sha256d self-check failed");
    abort();
  }
  for (uint32_t i = 0; i < 8; ++i) {
    uint32_t pipeline_state[8];
    sha256d_midstate_second_state(
        job.header_midstate, job.header_tail_words, nonce + i, pipeline_state);
    expected_pipeline_checksum ^= nonce + i;
    expected_pipeline_checksum ^= miner_hash_target_word(pipeline_state, 0);
  }
  if (pipeline_result.hashes != 8 || pipeline_result.candidates != 8 ||
      pipeline_checksum != expected_pipeline_checksum) {
    ESP_LOGE(TAG, "pipelined hardware sha256d self-check failed");
    abort();
  }
#if STRATUM_ASM_CANDIDATE_LOOP
  miner_hardware_acquire();
  const bool asm_self_check_ok = s3_miner_scan_asm_self_check(job.header_midstate,
                                                             job.header_tail_words,
                                                             nonce,
                                                             software_state);
  miner_hardware_release();
  if (!asm_self_check_ok) {
    ESP_LOGE(TAG, "assembly hardware sha256d self-check failed");
    abort();
  }
#endif
#endif
}

uint32_t stratum_benchmark_hashrate_us(int64_t duration_us) {
  mining_job_t job = {0};
  uint32_t checksum = 0;
  uint32_t hashes = 0;
  for (size_t i = 0; i < sizeof(job.header); ++i) {
    job.header[i] = (uint8_t)(i * 37U + 11U);
  }
  prepare_job_hashing(&job);
  set_job_share_target(&job, "1");

#if APP_HARDWARE_SHA_MINER
  miner_work_t work = {0};
  work.sequence = 1;
  memcpy(work.midstate, job.header_midstate, sizeof(work.midstate));
  memcpy(work.tail_words, job.header_tail_words, sizeof(work.tail_words));
  memcpy(work.target_words, job.share_target_words, sizeof(work.target_words));
  work.target_word0 = job.share_target_words[0];
#if APP_CLASSIC_HARDWARE_SHA_MINER
  synthetic_hw_prepare_blocks(work.hw_block0, work.hw_block1, job.header);
#endif

  miner_hardware_acquire();
  const int64_t warmup_start = esp_timer_get_time();
  uint32_t warmup_nonce = 0;
  do {
    const miner_scan_result_t result =
        miner_scan_hw_benchmark_range(&work, warmup_nonce, STRATUM_BOOT_BENCHMARK_CHUNK, &checksum);
    warmup_nonce += result.hashes;
  } while (esp_timer_get_time() - warmup_start < STRATUM_BOOT_BENCHMARK_WARMUP_US);

  const int64_t start = esp_timer_get_time();
  do {
    const miner_scan_result_t result =
        miner_scan_hw_benchmark_range(&work, hashes, STRATUM_BOOT_BENCHMARK_CHUNK, &checksum);
    checksum ^= result.candidates;
    hashes += result.hashes;
  } while (esp_timer_get_time() - start < duration_us);
  miner_hardware_release();
#else
  const int64_t start = esp_timer_get_time();
  do {
    for (uint32_t i = 0; i < STRATUM_BOOT_BENCHMARK_CHUNK; ++i) {
      checksum ^= sha256d_midstate_second_target_word0_raw_swapped(
          job.header_midstate,
          job.header_tail_words[0],
          job.header_tail_words[1],
          job.header_tail_words[2],
          byteswap32(hashes + i));
    }
    hashes += STRATUM_BOOT_BENCHMARK_CHUNK;
  } while (esp_timer_get_time() - start < duration_us);
#endif

  g_benchmark_sink = checksum;
  const int64_t elapsed = esp_timer_get_time() - start;
  return elapsed > 0 ? (uint32_t)(((uint64_t)hashes * 1000000ULL) / (uint64_t)elapsed) : hashes;
}
