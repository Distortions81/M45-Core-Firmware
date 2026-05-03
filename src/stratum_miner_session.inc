/* Private implementation fragment included by stratum_miner.c. */

static void stratum_ensure_miner_tasks(void) {
  if (!share_queue_prepare_storage()) {
    return;
  }
  const uint8_t task_count = 1;
  for (uint8_t i = 0; i < task_count; ++i) {
    if (g_miner_task_handles[i] != NULL) {
      continue;
    }
    static const char* names[] = {"miner0", "miner1"};
    g_miner_task_contexts[i].index = i;
    const BaseType_t core_id = portNUM_PROCESSORS > 1 ? 1 : 0;
    const BaseType_t created = xTaskCreatePinnedToCore(miner_worker_task,
                                                       names[i % 2],
                                                       STRATUM_MINER_TASK_STACK_BYTES,
                                                       &g_miner_task_contexts[i],
                                                       STRATUM_MINER_TASK_PRIORITY,
                                                       &g_miner_task_handles[i],
                                                       core_id);
    if (created != pdPASS) {
      g_miner_task_handles[i] = NULL;
      ESP_LOGE(TAG, "Could not create miner task %u", (unsigned)i);
    }
  }

#if STRATUM_SOFTWARE_MINER_ENABLED
  if (g_software_miner_task_handle == NULL) {
    g_software_miner_task_context.index = STRATUM_MINER_TASK_COUNT;
    const BaseType_t core_id = 0;
    const BaseType_t created = xTaskCreatePinnedToCore(software_miner_worker_task,
                                                       "miner_sw",
                                                       STRATUM_SOFTWARE_MINER_TASK_STACK_BYTES,
                                                       &g_software_miner_task_context,
                                                       STRATUM_SOFTWARE_MINER_TASK_PRIORITY,
                                                       &g_software_miner_task_handle,
                                                       core_id);
    if (created != pdPASS) {
      g_software_miner_task_handle = NULL;
      ESP_LOGE(TAG, "Could not create software miner task");
    }
  }
#endif
}

static void stratum_update_hashrate(void) {
  const int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&g_miner_stats_mux);
  const uint64_t hashes_total = g_stratum_runtime.hashes_total;
  if (g_hashrate_sample_us == 0) {
    g_hashrate_sample_us = now;
    g_hashrate_sample_total = hashes_total;
    portEXIT_CRITICAL(&g_miner_stats_mux);
    return;
  }
  const int64_t elapsed = now - g_hashrate_sample_us;
  const int64_t sample_ms = g_stratum_runtime.hashes_per_second == 0
                                ? STRATUM_INITIAL_HASHRATE_SAMPLE_MS
                                : STRATUM_HASHRATE_SAMPLE_MS;
  if (elapsed < sample_ms * 1000LL) {
    portEXIT_CRITICAL(&g_miner_stats_mux);
    return;
  }
  const uint64_t delta = hashes_total - g_hashrate_sample_total;
  g_stratum_runtime.hashes_per_second = (uint32_t)((delta * 1000000ULL) / (uint64_t)elapsed);
  g_hashrate_sample_total = hashes_total;
  g_hashrate_sample_us = now;
  portEXIT_CRITICAL(&g_miner_stats_mux);
}

static void stratum_finish_rx_line(int sock, char* line, size_t* line_len, bool* line_dropped) {
  if (!*line_dropped) {
    line[*line_len] = '\0';
    if (*line_len > 0) {
      stratum_handle_line(sock, line);
    }
  }
  *line_len = 0;
  *line_dropped = false;
}

static void stratum_drop_rx_line(size_t* line_len, bool* line_dropped) {
  *line_len = 0;
  if (!*line_dropped) {
    ESP_LOGW(TAG, "Dropped oversized Stratum line");
  }
  *line_dropped = true;
}

static void stratum_handle_rx_char(
    int sock, char ch, char* line, size_t* line_len, bool* line_dropped) {
  if (ch == '\n') {
    stratum_finish_rx_line(sock, line, line_len, line_dropped);
    return;
  }
  if (ch == '\r') {
    return;
  }
  if (*line_dropped) {
    return;
  }
  if (*line_len + 1 < STRATUM_LINE_MAX) {
    line[(*line_len)++] = ch;
    return;
  }
  stratum_drop_rx_line(line_len, line_dropped);
}

static void stratum_handle_rx_bytes(
    int sock, const char* bytes, size_t len, char* line, size_t* line_len, bool* line_dropped) {
  while (len > 0) {
    const char* newline = memchr(bytes, '\n', len);
    const size_t segment_len = newline != NULL ? (size_t)(newline - bytes) : len;
    if (segment_len > 0 && !*line_dropped) {
      if (memchr(bytes, '\r', segment_len) != NULL) {
        for (size_t i = 0; i < segment_len; ++i) {
          stratum_handle_rx_char(sock, bytes[i], line, line_len, line_dropped);
        }
      } else if (*line_len + segment_len < STRATUM_LINE_MAX) {
        memcpy(line + *line_len, bytes, segment_len);
        *line_len += segment_len;
      } else {
        stratum_drop_rx_line(line_len, line_dropped);
      }
    }
    if (newline == NULL) {
      return;
    }
    stratum_finish_rx_line(sock, line, line_len, line_dropped);
    bytes = newline + 1;
    len -= segment_len + 1;
  }
}

static bool stratum_drain_socket(
    int sock, char* line, size_t* line_len, bool* line_dropped, int64_t* last_activity) {
  char buffer[STRATUM_SOCKET_RX_BUFFER];
  while (true) {
    const int received = recv(sock, buffer, sizeof(buffer), 0);
    if (received > 0) {
      *last_activity = esp_timer_get_time();
      stratum_handle_rx_bytes(sock, buffer, (size_t)received, line, line_len, line_dropped);
      continue;
    }
    if (received == 0) {
      return false;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return true;
    }
    return false;
  }
}

static bool stratum_wait_for_socket(int sock, uint32_t timeout_ms) {
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(sock, &read_fds);
  struct timeval timeout = {
      .tv_sec = timeout_ms / 1000U,
      .tv_usec = (suseconds_t)(timeout_ms % 1000U) * 1000,
  };
  const int result = select(sock + 1, &read_fds, NULL, NULL, &timeout);
  return result >= 0 || errno == EINTR;
}

static uint32_t stratum_session_wait_ms(void) {
  return g_mining_job.ready ? STRATUM_SESSION_POLL_MS : STRATUM_SESSION_IDLE_POLL_MS;
}

typedef struct {
  char host[sizeof(g_stratum.primary_host)];
  uint16_t port;
} stratum_primary_probe_args_t;

static stratum_primary_probe_args_t g_stratum_primary_probe_args;

static void stratum_primary_probe_task(void* arg) {
  stratum_primary_probe_args_t probe;
  memcpy(&probe, arg, sizeof(probe));
  const int sock = stratum_connect_socket(probe.host, probe.port);
  if (sock >= 0) {
    close(sock);
    if (g_sta_connected &&
        g_stratum_runtime.using_backup &&
        strcmp(g_stratum.primary_host, probe.host) == 0 &&
        g_stratum.primary_port == probe.port) {
      g_stratum_switch_to_primary_requested = true;
      g_stratum_reconnect_requested = true;
      TaskHandle_t session_task = g_stratum_session_task_handle;
      if (session_task != NULL) {
        xTaskNotifyGive(session_task);
      }
      ESP_LOGI(TAG, "Primary pool is reachable again; switching from backup");
    }
  }
  g_stratum_primary_probe_in_progress = false;
  vTaskDelete(NULL);
}

static void stratum_maybe_probe_primary_pool(void) {
  if (!g_sta_connected ||
      !g_stratum_runtime.connected ||
      !g_stratum_runtime.using_backup ||
      g_stratum.primary_host[0] == '\0' ||
      g_stratum.primary_port == 0 ||
      g_stratum_primary_probe_in_progress) {
    return;
  }
  const int64_t now = esp_timer_get_time();
  if (g_stratum_last_primary_probe_us != 0 &&
      now - g_stratum_last_primary_probe_us <
          (int64_t)STRATUM_PRIMARY_PROBE_INTERVAL_MS * 1000LL) {
    return;
  }
  g_stratum_last_primary_probe_us = now;
  copy_str(g_stratum_primary_probe_args.host,
           sizeof(g_stratum_primary_probe_args.host),
           g_stratum.primary_host);
  g_stratum_primary_probe_args.port = g_stratum.primary_port;
  g_stratum_primary_probe_in_progress = true;
  const BaseType_t created = xTaskCreate(stratum_primary_probe_task,
                                         "stratum_probe",
                                         3072,
                                         &g_stratum_primary_probe_args,
                                         STRATUM_SOFTWARE_MINER_TASK_PRIORITY,
                                         NULL);
  if (created != pdPASS) {
    g_stratum_primary_probe_in_progress = false;
    ESP_LOGW(TAG, "Could not create primary pool probe task");
  }
}

static bool stratum_session(int sock) {
  uint32_t request_id = 1;
  uint32_t submit_id = 1000;
  char login[220];
  miner_clear_work();
  g_stratum_reconnect_requested = false;
  memset(&g_mining_job, 0, sizeof(g_mining_job));
  g_pending_notify_available = false;
  g_pending_notify_line[0] = '\0';
  g_stratum_runtime.payout_status = STRATUM_PAYOUT_STATUS_UNCHECKED;
  g_stratum_runtime.payout_percent_x100 = 0;
  set_job_share_target(&g_mining_job, g_stratum_runtime.current_difficulty);
  if (!stratum_set_socket_nonblocking(sock)) {
    return false;
  }
  stratum_build_login(login, sizeof(login));

  stratum_send_subscribe(sock);
  ++request_id;
  if (g_stratum.suggested_difficulty[0] != '\0') {
    stratum_send_suggest_difficulty(sock, request_id++);
  }
  stratum_send_authorize(sock, request_id++, login);
  stratum_send_extranonce_subscribe(sock, request_id++);

  char* line = g_stratum_line;
  size_t line_len = 0;
  bool line_dropped = false;
  int64_t last_activity = esp_timer_get_time();
  g_stratum_session_task_handle = xTaskGetCurrentTaskHandle();
  while (g_sta_connected) {
    if (!stratum_drain_socket(sock, line, &line_len, &line_dropped, &last_activity)) {
      g_stratum_session_task_handle = NULL;
      return false;
    }
    stratum_drain_share_queue(sock, &submit_id, login);
    stratum_update_hashrate();
    stratum_maybe_probe_primary_pool();
    if (g_stratum_reconnect_requested) {
      g_stratum_session_task_handle = NULL;
      return true;
    }
    if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
      continue;
    }
    if (!stratum_wait_for_socket(sock, stratum_session_wait_ms())) {
      g_stratum_session_task_handle = NULL;
      return false;
    }

    if ((esp_timer_get_time() - last_activity) > (int64_t)STRATUM_IDLE_TIMEOUT_MS * 1000LL) {
      g_stratum_session_task_handle = NULL;
      return true;
    }
  }
  g_stratum_session_task_handle = NULL;
  return false;
}

void stratum_task(void* arg) {
  (void)arg;
  stratum_ensure_miner_tasks();
  while (true) {
    if (!g_sta_connected || g_stratum.primary_host[0] == '\0' || g_stratum.wallet[0] == '\0') {
      g_stratum_runtime.enabled = false;
      stratum_set_disconnected();
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    g_stratum_runtime.enabled = true;
    const char* host = g_stratum_runtime.using_backup && g_stratum.backup_host[0] != '\0'
                           ? g_stratum.backup_host
                           : g_stratum.primary_host;
    const uint16_t port = g_stratum_runtime.using_backup && g_stratum.backup_host[0] != '\0'
                              ? g_stratum.backup_port
                              : g_stratum.primary_port;
    copy_str(g_stratum_runtime.current_pool, sizeof(g_stratum_runtime.current_pool), host);
    g_stratum_runtime.current_port = port;
    oled_prepare_pool_endpoint_cache();

    bool rotate_endpoint = false;
    const int sock = stratum_connect_socket(host, port);
    if (sock >= 0) {
      g_stratum_runtime.connected = true;
      g_stratum_runtime.connected_at_us = esp_timer_get_time();
      if (g_stratum_runtime.first_connected_at_us == 0) {
        g_stratum_runtime.first_connected_at_us = g_stratum_runtime.connected_at_us;
      }
      rotate_endpoint = stratum_session(sock);
      close(sock);
    } else {
      rotate_endpoint = true;
    }
    stratum_set_disconnected();
    if (g_stratum_switch_to_primary_requested) {
      g_stratum_runtime.using_backup = false;
      g_stratum_switch_to_primary_requested = false;
    } else if (rotate_endpoint && g_stratum.backup_host[0] != '\0' && g_stratum.backup_port > 0) {
      g_stratum_runtime.using_backup = !g_stratum_runtime.using_backup;
    }
    vTaskDelay(pdMS_TO_TICKS(STRATUM_RECONNECT_MS));
  }
}
