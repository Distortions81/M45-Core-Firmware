/* Private implementation fragment included by stratum_miner.c. */

static void synthetic_prepare_work(miner_work_t* work) {
  mining_job_t job = {0};
  for (size_t i = 0; i < sizeof(job.header); ++i) {
    job.header[i] = (uint8_t)(i * 37U + 11U);
  }
  prepare_job_hashing(&job);
  set_job_share_target(&job, "0.0000000100");

  memset(work, 0, sizeof(*work));
  work->active = true;
  work->sequence = 1;
  memcpy(work->midstate, job.header_midstate, sizeof(work->midstate));
  memcpy(work->tail_words, job.header_tail_words, sizeof(work->tail_words));
  memcpy(work->target_words, job.share_target_words, sizeof(work->target_words));
  work->target_word0 = job.share_target_words[0];
  synthetic_hw_prepare_blocks(g_synthetic_hw_block0, g_synthetic_hw_block1, job.header);
}

static void synthetic_snapshot(
    uint64_t* total_hashes,
    uint32_t* total_candidates,
    uint32_t* checksum,
    uint64_t per_task_hashes[STRATUM_MINER_TASK_COUNT]) {
  *total_hashes = 0;
  *total_candidates = 0;
  *checksum = 0;
  portENTER_CRITICAL(&g_synthetic_stats_mux);
  for (uint8_t i = 0; i < STRATUM_MINER_TASK_COUNT; ++i) {
    const synthetic_task_context_t* context = &g_synthetic_task_contexts[i];
    per_task_hashes[i] = context->hashes;
    *total_hashes += context->hashes;
    *total_candidates += context->candidates;
    *checksum ^= context->checksum;
  }
  portEXIT_CRITICAL(&g_synthetic_stats_mux);
}

static inline __attribute__((always_inline)) bool IRAM_ATTR synthetic_context_nonce_meets_target(
    synthetic_task_context_t* context,
    uint32_t nonce) {
  return synthetic_hw_nonce_meets_target(context, nonce);
}

static void synthetic_mine_range(
    synthetic_task_context_t* context,
    uint32_t start_nonce,
    uint32_t nonce_count) {
  uint32_t candidates = 0;
  uint32_t checksum = 0;
  uint32_t i = 0;
  esp_sha_lock_engine(SHA2_256);
  for (; i + 3 < nonce_count; i += 4) {
    const uint32_t nonce0 = start_nonce + i;
    if (synthetic_context_nonce_meets_target(context, nonce0)) {
      ++candidates;
      checksum ^= nonce0;
    }
    const uint32_t nonce1 = nonce0 + 1;
    if (synthetic_context_nonce_meets_target(context, nonce1)) {
      ++candidates;
      checksum ^= nonce1;
    }
    const uint32_t nonce2 = nonce0 + 2;
    if (synthetic_context_nonce_meets_target(context, nonce2)) {
      ++candidates;
      checksum ^= nonce2;
    }
    const uint32_t nonce3 = nonce0 + 3;
    if (synthetic_context_nonce_meets_target(context, nonce3)) {
      ++candidates;
      checksum ^= nonce3;
    }
  }
  for (; i < nonce_count; ++i) {
    const uint32_t nonce = start_nonce + i;
    if (synthetic_context_nonce_meets_target(context, nonce)) {
      ++candidates;
      checksum ^= nonce;
    }
  }
  esp_sha_unlock_engine(SHA2_256);
  context->hashes = nonce_count;
  context->candidates = candidates;
  context->checksum = checksum;
}

static void synthetic_worker_task(void* arg) {
  synthetic_task_context_t* context = (synthetic_task_context_t*)arg;
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    synthetic_mine_range(context, context->start_nonce, context->nonce_count);
    xTaskNotifyGive(context->notify_task);
  }
}

void stratum_synthetic_benchmark(void) {
  miner_work_t work;
  synthetic_prepare_work(&work);
  const uint8_t task_count = 1;
  APP_SERIAL_LOGF("synthetic: no wifi, no oled, no http, no pool\n");
  APP_SERIAL_LOGF("synthetic: engine=esp32-hardware-sha256\n");
  APP_SERIAL_LOGF("synthetic: tasks=%u hashes_per_task=%u total_hashes=%u\n",
         (unsigned)task_count,
         (unsigned)STRATUM_SYNTHETIC_HASHES_PER_TASK,
         (unsigned)(STRATUM_SYNTHETIC_HASHES_PER_TASK * task_count));

  synthetic_task_context_t check_context = {0};
  check_context.work = work;
  memcpy(check_context.hw_block0, g_synthetic_hw_block0, sizeof(check_context.hw_block0));
  memcpy(check_context.hw_block1, g_synthetic_hw_block1, sizeof(check_context.hw_block1));
  if (!synthetic_hw_self_check(&check_context)) {
    return;
  }
  APP_SERIAL_LOGF("synthetic: hardware sha self-check ok\n");

  for (uint8_t i = 0; i < task_count; ++i) {
    synthetic_task_context_t* context = &g_synthetic_task_contexts[i];
    memset(context, 0, sizeof(*context));
    context->index = i;
    context->work = work;
    memcpy(context->hw_block0, g_synthetic_hw_block0, sizeof(context->hw_block0));
    memcpy(context->hw_block1, g_synthetic_hw_block1, sizeof(context->hw_block1));
    context->notify_task = xTaskGetCurrentTaskHandle();
    static const char* names[] = {"synth0", "synth1"};
    const BaseType_t created = xTaskCreatePinnedToCore(synthetic_worker_task,
                                                       names[i % 2],
                                                       STRATUM_MINER_TASK_STACK_BYTES,
                                                       context,
                                                       STRATUM_MINER_TASK_PRIORITY,
                                                       &g_synthetic_task_handles[i],
                                                       portNUM_PROCESSORS > 1 ? 1 : 0);
    if (created != pdPASS) {
      g_synthetic_task_handles[i] = NULL;
      APP_SERIAL_LOGF("synthetic: failed to create task %u\n", (unsigned)i);
    }
  }

  uint32_t round = 0;
  while (true) {
    ++round;
    for (uint8_t i = 0; i < task_count; ++i) {
      synthetic_task_context_t* context = &g_synthetic_task_contexts[i];
      context->start_nonce = (uint32_t)round * STRATUM_SYNTHETIC_HASHES_PER_TASK *
                             task_count +
                             (uint32_t)i * STRATUM_SYNTHETIC_HASHES_PER_TASK;
      context->nonce_count = STRATUM_SYNTHETIC_HASHES_PER_TASK;
      context->hashes = 0;
      context->candidates = 0;
      context->checksum = 0;
    }

    uint8_t running_tasks = 0;
    APP_SERIAL_LOGF("synthetic: round=%u begin hashes=%u\n",
           (unsigned)round,
           (unsigned)(STRATUM_SYNTHETIC_HASHES_PER_TASK * task_count));
    const int64_t started_us = esp_timer_get_time();
    for (uint8_t i = 0; i < task_count; ++i) {
      if (g_synthetic_task_handles[i] != NULL) {
        ++running_tasks;
        xTaskNotifyGive(g_synthetic_task_handles[i]);
      }
    }
    for (uint8_t i = 0; i < running_tasks; ++i) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    const int64_t elapsed_us_signed = esp_timer_get_time() - started_us;

    uint64_t per_task_hashes[STRATUM_MINER_TASK_COUNT] = {0};
    uint64_t total_hashes = 0;
    uint32_t total_candidates = 0;
    uint32_t checksum = 0;
    synthetic_snapshot(&total_hashes, &total_candidates, &checksum, per_task_hashes);

#if APP_ENABLE_SERIAL_LOGS
    const uint64_t elapsed_us = elapsed_us_signed > 0 ? (uint64_t)elapsed_us_signed : 0;
    const uint64_t hashes_per_second =
        elapsed_us > 0 ? (total_hashes * 1000000ULL) / elapsed_us : 0;
    APP_SERIAL_LOGF("synthetic: round=%u elapsed_us=%llu hps=%llu hashes=%llu candidates=%u checksum=%08x worker=%llu\n",
           (unsigned)round,
           (unsigned long long)elapsed_us,
           (unsigned long long)hashes_per_second,
           (unsigned long long)total_hashes,
           (unsigned)total_candidates,
           (unsigned)checksum,
           (unsigned long long)per_task_hashes[0]);
#else
    (void)elapsed_us_signed;
#endif
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
