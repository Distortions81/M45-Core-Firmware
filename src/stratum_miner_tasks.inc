/* Private implementation fragment included by stratum_miner.c. */

static void miner_wake_tasks(void) {
  for (uint8_t i = 0; i < STRATUM_MINER_TASK_COUNT; ++i) {
    if (g_miner_task_handles[i] != NULL) {
      xTaskNotifyGive(g_miner_task_handles[i]);
    }
  }
  if (g_software_miner_task_handle != NULL) {
    xTaskNotifyGive(g_software_miner_task_handle);
  }
}

static void miner_worker_task(void* arg) {
#if STRATUM_MINE_YIELD_BATCHES > 0
  const miner_task_context_t* context = (const miner_task_context_t*)arg;
  uint32_t batches_since_delay = 0;
  if (context != NULL) {
    batches_since_delay = context->index % STRATUM_MINE_YIELD_BATCHES;
  }
#else
  (void)arg;
#endif
  while (true) {
    miner_work_t work;
    uint32_t start_nonce = 0;
    uint32_t claimed_nonce_count = 0;
    if (!miner_claim_batch(&work, &start_nonce, &claimed_nonce_count)) {
#if STRATUM_MINE_YIELD_BATCHES > 0
      batches_since_delay = 0;
#endif
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }

    esp_sha_lock_engine(SHA2_256);
    const miner_scan_result_t result = miner_scan_batch(&work, start_nonce);
    esp_sha_unlock_engine(SHA2_256);
    if (result.hashes < claimed_nonce_count) {
      miner_return_unscanned(&work,
                             start_nonce + result.hashes,
                             start_nonce + claimed_nonce_count);
    } else if (result.hashes > claimed_nonce_count) {
      miner_advance_overrun_claim(&work,
                                  start_nonce + result.hashes,
                                  start_nonce + claimed_nonce_count);
    }

    if (result.stale_work) {
#if STRATUM_MINE_YIELD_BATCHES > 0
      batches_since_delay = 0;
#endif
      continue;
    }

    if (result.queue_full) {
      if (g_stratum_session_task_handle != NULL) {
        xTaskNotifyGive(g_stratum_session_task_handle);
      }
#if STRATUM_MINE_YIELD_BATCHES > 0
      batches_since_delay = 0;
#endif
      vTaskDelay(1);
      continue;
    }

#if STRATUM_MINE_YIELD_BATCHES > 0
    ++batches_since_delay;
    if (batches_since_delay >= STRATUM_MINE_YIELD_BATCHES) {
      vTaskDelay(1);
      batches_since_delay = 0;
    }
#endif
  }
}

static void software_miner_worker_task(void* arg) {
  (void)arg;
  while (true) {
    miner_work_t work;
    uint32_t start_nonce = 0;
    if (!miner_claim_range(&work, &start_nonce, STRATUM_SOFTWARE_MINE_BATCH)) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }

    const miner_scan_result_t result =
        miner_scan_software_range(&work, start_nonce, STRATUM_SOFTWARE_MINE_BATCH);
    if (result.hashes < STRATUM_SOFTWARE_MINE_BATCH) {
      miner_return_unscanned(&work,
                             start_nonce + result.hashes,
                             start_nonce + STRATUM_SOFTWARE_MINE_BATCH);
    }

    if (result.queue_full && g_stratum_session_task_handle != NULL) {
      xTaskNotifyGive(g_stratum_session_task_handle);
    }

    vTaskDelay(1);
  }
}
