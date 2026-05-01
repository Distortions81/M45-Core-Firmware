/* Private implementation fragment included by stratum_miner.c. */

static void stratum_set_disconnected(void) {
  const int64_t now = esp_timer_get_time();
  if (g_stratum_runtime.connected && g_stratum_runtime.connected_at_us > 0 &&
      now > (int64_t)g_stratum_runtime.connected_at_us) {
    g_stratum_runtime.connected_total_us +=
        (uint64_t)(now - (int64_t)g_stratum_runtime.connected_at_us);
  }
  g_stratum_runtime.connected = false;
  g_stratum_runtime.connected_at_us = 0;
  g_stratum_runtime.disconnected_at_us = now;
  g_stratum_runtime.payout_status = STRATUM_PAYOUT_STATUS_UNCHECKED;
  g_stratum_runtime.payout_percent_x100 = 0;
  miner_reset_hashrate_sample(g_stratum_runtime.hashes_per_second != g_stratum.benchmark_hashes_per_second);
  miner_clear_work();
}

static bool stratum_send_subscribe(int sock) {
  char miner_info[80];
  char line[160];
  size_t offset = 0;
  stratum_build_miner_info(miner_info, sizeof(miner_info));
  line[0] = '\0';
  return append_text(line, sizeof(line), &offset, "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[") &&
         append_json_token_value(line, sizeof(line), &offset, miner_info) &&
         append_text(line, sizeof(line), &offset, "]}\n") &&
         send_all(sock, line);
}

static bool stratum_send_suggest_difficulty(int sock, uint32_t id) {
  char line[128];
  size_t offset = 0;
  line[0] = '\0';
  return append_text(line, sizeof(line), &offset, "{\"id\":") &&
         append_u32_dec(line, sizeof(line), &offset, id) &&
         append_text(line, sizeof(line), &offset, ",\"method\":\"mining.suggest_difficulty\",\"params\":[") &&
         append_text(line, sizeof(line), &offset, g_stratum.suggested_difficulty) &&
         append_text(line, sizeof(line), &offset, "]}\n") &&
         send_all(sock, line);
}

static bool stratum_send_extranonce_subscribe(int sock, uint32_t id) {
  char line[96];
  size_t offset = 0;
  line[0] = '\0';
  return append_text(line, sizeof(line), &offset, "{\"id\":") &&
         append_u32_dec(line, sizeof(line), &offset, id) &&
         append_text(line,
                     sizeof(line),
                     &offset,
                     ",\"method\":\"mining.extranonce.subscribe\",\"params\":[]}\n") &&
         send_all(sock, line);
}

static void stratum_build_login(char* login, size_t login_size) {
  size_t offset = 0;
  login[0] = '\0';
  append_text(login, login_size, &offset, g_stratum.wallet);
  if (g_stratum.username[0] != '\0') {
    append_text(login, login_size, &offset, ".");
    append_text(login, login_size, &offset, g_stratum.username);
  }
}

static bool stratum_send_authorize(int sock, uint32_t id, const char* login) {
  char line[320];
  size_t offset = 0;
  line[0] = '\0';
  return append_text(line, sizeof(line), &offset, "{\"id\":") &&
         append_u32_dec(line, sizeof(line), &offset, id) &&
         append_text(line, sizeof(line), &offset, ",\"method\":\"mining.authorize\",\"params\":[") &&
         append_json_token_value(line, sizeof(line), &offset, login) &&
         append_text(line, sizeof(line), &offset, ",\"x\"]}\n") &&
         send_all(sock, line);
}

static bool stratum_send_submit(int sock, uint32_t id, const char* login, const found_share_t* share) {
  char nonce_hex[9];
  char extra_nonce2_hex[33];
  char line[640];
  size_t offset = 0;
  uint32_to_hex_be(share->nonce, nonce_hex, sizeof(nonce_hex));
  extra_nonce2_to_hex_le(share->extra_nonce2,
                         share->extra_nonce2_size,
                         extra_nonce2_hex,
                         sizeof(extra_nonce2_hex));
  line[0] = '\0';
  return append_text(line, sizeof(line), &offset, "{\"id\":") &&
         append_u32_dec(line, sizeof(line), &offset, id) &&
         append_text(line, sizeof(line), &offset, ",\"method\":\"mining.submit\",\"params\":[") &&
         append_json_token_value(line, sizeof(line), &offset, login) &&
         append_text(line, sizeof(line), &offset, ",") &&
         append_json_token_value(line, sizeof(line), &offset, share->job_id) &&
         append_text(line, sizeof(line), &offset, ",\"") &&
         append_text(line, sizeof(line), &offset, extra_nonce2_hex) &&
         append_text(line, sizeof(line), &offset, "\",") &&
         append_json_token_value(line, sizeof(line), &offset, share->ntime) &&
         append_text(line, sizeof(line), &offset, ",\"") &&
         append_text(line, sizeof(line), &offset, nonce_hex) &&
         append_text(line, sizeof(line), &offset, "\"]}\n") &&
         send_all(sock, line);
}

static bool stratum_send_pong(int sock, int id) {
  char line[64];
  size_t offset = 0;
  if (id < 0) {
    return false;
  }
  line[0] = '\0';
  return append_text(line, sizeof(line), &offset, "{\"id\":") &&
         append_u32_dec(line, sizeof(line), &offset, (uint32_t)id) &&
         append_text(line, sizeof(line), &offset, ",\"result\":\"pong\",\"error\":null}\n") &&
         send_all(sock, line);
}

static void stratum_connect_addrinfo_hints(struct addrinfo* hints) {
  memset(hints, 0, sizeof(*hints));
  hints->ai_family = AF_INET;
  hints->ai_socktype = SOCK_STREAM;
}

static const char* stratum_fallback_ip_for_host(const char* host) {
  if (strcmp(host, STRATUM_DEFAULT_PRIMARY) == 0) {
    return DEFAULT_PRIMARY_IP;
  }
  if (strcmp(host, STRATUM_DEFAULT_BACKUP) == 0) {
    return DEFAULT_BACKUP_IP;
  }
  return NULL;
}

static int stratum_connect_ip4(const char* ip, uint16_t port) {
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
    return -1;
  }

  const int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (sock < 0) {
    return -1;
  }
  struct timeval timeout = {
      .tv_sec = STRATUM_SOCKET_TIMEOUT_MS / 1000,
      .tv_usec = (STRATUM_SOCKET_TIMEOUT_MS % 1000) * 1000,
  };
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
    return sock;
  }
  close(sock);
  return -1;
}

static int stratum_connect_socket(const char* host, uint16_t port) {
  char port_text[8];
  struct addrinfo hints;
  struct addrinfo* result = NULL;
  snprintf(port_text, sizeof(port_text), "%u", port);
  stratum_connect_addrinfo_hints(&hints);
  if (getaddrinfo(host, port_text, &hints, &result) != 0 || result == NULL) {
    const char* fallback_ip = stratum_fallback_ip_for_host(host);
    return fallback_ip != NULL ? stratum_connect_ip4(fallback_ip, port) : -1;
  }

  int sock = -1;
  for (struct addrinfo* entry = result; entry != NULL; entry = entry->ai_next) {
    sock = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
    if (sock < 0) {
      continue;
    }
    struct timeval timeout = {
        .tv_sec = STRATUM_SOCKET_TIMEOUT_MS / 1000,
        .tv_usec = (STRATUM_SOCKET_TIMEOUT_MS % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(sock, entry->ai_addr, entry->ai_addrlen) == 0) {
      break;
    }
    close(sock);
    sock = -1;
  }

  freeaddrinfo(result);
  return sock;
}
