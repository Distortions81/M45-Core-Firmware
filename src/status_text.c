#include "app_state.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"

void uptime_text(char* out, size_t out_size) {
  const uint64_t uptime_s = (esp_timer_get_time() - g_boot_us) / 1000000ULL;
  const unsigned hours = (unsigned)(uptime_s / 3600ULL);
  const unsigned minutes = (unsigned)((uptime_s / 60ULL) % 60ULL);
  if (hours > 999) {
    copy_str(out, out_size, "999h+");
  } else if (hours > 0) {
    snprintf(out, out_size, "%uh %um", hours, minutes);
  } else {
    snprintf(out, out_size, "%um", minutes);
  }
}

const char* pool_status_text(void) {
  return g_stratum_runtime.connected ? "Connected"
                                     : (g_stratum_runtime.enabled ? "Connecting" : "Not connected");
}

void pool_endpoint_text(char* out, size_t out_size) {
  const char* pool = g_stratum_runtime.current_pool[0] != '\0'
                         ? g_stratum_runtime.current_pool
                         : (g_stratum.primary_host[0] != '\0' ? g_stratum.primary_host : "Not configured");
  const uint16_t port =
      g_stratum_runtime.current_port > 0 ? g_stratum_runtime.current_port : g_stratum.primary_port;
  if (strcmp(pool, "Not configured") == 0) {
    copy_str(out, out_size, pool);
  } else {
    const int max_host_len = out_size > 7 ? (int)out_size - 7 : 0;
    snprintf(out, out_size, "%.*s:%u", max_host_len, pool, port);
  }
  strip_commas(out);
}

void shares_per_min_text(char* out, size_t out_size) {
  const int64_t now = esp_timer_get_time();
  const uint64_t connected_us =
      g_stratum_runtime.connected_at_us > 0 && now > (int64_t)g_stratum_runtime.connected_at_us
          ? (uint64_t)(now - (int64_t)g_stratum_runtime.connected_at_us)
          : 0;
  if (connected_us == 0 || g_stratum_runtime.accepted == 0) {
    snprintf(out, out_size, "0.00");
    return;
  }

  const uint64_t hundredths =
      ((uint64_t)g_stratum_runtime.accepted * 60ULL * 100ULL * 1000000ULL) / connected_us;
  snprintf(out, out_size, "%llu.%02llu", hundredths / 100ULL, hundredths % 100ULL);
}

const char* current_difficulty_text(void) {
  return g_stratum_runtime.current_difficulty[0] != '\0'
             ? g_stratum_runtime.current_difficulty
             : (g_stratum.suggested_difficulty[0] != '\0' ? g_stratum.suggested_difficulty : "0.0000000000");
}

void device_name_text(char* out, size_t out_size) {
  const char* suffix = strrchr(g_ap_ssid, '-');
  suffix = suffix != NULL && suffix[1] != '\0' ? suffix + 1 : g_ap_ssid;
  snprintf(out, out_size, "M45-Miner-%s", suffix);
}
