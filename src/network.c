#include "app_state.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static const char* TAG = "network";
static bool g_sta_connect_done = false;
static bool g_wifi_station_started = false;
static bool g_softap_enabled = false;
static uint8_t g_sta_disconnect_reason = 0;
static uint32_t g_wifi_reconnect_delay_ms = WIFI_RECONNECT_BASE_MS;
static int64_t g_wifi_next_reconnect_us = 0;
static TaskHandle_t g_wifi_reconnect_task_handle = NULL;
static esp_netif_t* g_ap_netif = NULL;
static esp_netif_t* g_sta_netif = NULL;

static void configure_setup_network_identity(void) {
  uint8_t mac[6] = {0};
  if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to read softAP MAC; using default setup network identity");
    return;
  }
  snprintf(g_ap_ssid, sizeof(g_ap_ssid), "m-%02X%02X", mac[4], mac[5]);
  g_ap_ip[0] = AP_IP0;
  g_ap_ip[1] = (uint8_t)((mac[5] / 10U) % 10U);
  g_ap_ip[2] = (uint8_t)(mac[5] % 10U);
  g_ap_ip[3] = AP_IP3;
  snprintf(g_ap_ip_text, sizeof(g_ap_ip_text), "%u.%u.%u.%u", g_ap_ip[0], g_ap_ip[1], g_ap_ip[2], g_ap_ip[3]);
}

static esp_err_t configure_softap(void) {
  wifi_config_t ap_config = {
      .ap = {
          .ssid = "",
          .ssid_len = 0,
          .channel = AP_CHANNEL,
          .authmode = WIFI_AUTH_OPEN,
          .max_connection = AP_MAX_CLIENTS,
      },
  };
  copy_str((char*)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), g_ap_ssid);
  ap_config.ap.ssid_len = strlen(g_ap_ssid);
  return esp_wifi_set_config(WIFI_IF_AP, &ap_config);
}

static void dns_task(void* arg) {
  static const uint8_t answer_template[16] = {
      0xc0, 0x0c, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
      0x00, 0x3c, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00};
  uint8_t buffer[512];
  const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(TAG, "DNS socket create failed");
    vTaskDelete(NULL);
  }

  struct sockaddr_in listen_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(53),
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  if (bind(sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) != 0) {
    ESP_LOGE(TAG, "DNS socket bind failed");
    close(sock);
    vTaskDelete(NULL);
  }

  while (true) {
    struct sockaddr_in source_addr;
    socklen_t source_len = sizeof(source_addr);
    const int len =
        recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&source_addr, &source_len);
    if (len < 12) {
      continue;
    }

    int question_end = 12;
    while (question_end < len && buffer[question_end] != 0) {
      question_end += buffer[question_end] + 1;
    }
    question_end += 5;
    if (question_end > len || question_end + 16 > (int)sizeof(buffer)) {
      continue;
    }

    buffer[2] = 0x81;
    buffer[3] = 0x80;
    buffer[6] = 0x00;
    buffer[7] = 0x01;
    buffer[8] = 0x00;
    buffer[9] = 0x00;
    buffer[10] = 0x00;
    buffer[11] = 0x00;

    uint8_t* answer = buffer + question_end;
    memcpy(answer, answer_template, sizeof(answer_template));
    answer[12] = g_ap_ip[0];
    answer[13] = g_ap_ip[1];
    answer[14] = g_ap_ip[2];
    answer[15] = g_ap_ip[3];

    sendto(sock, buffer, question_end + 16, 0, (struct sockaddr*)&source_addr, source_len);
  }
}

static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
  bool wake_reconnect_task = false;
  if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START && g_wifi.ssid[0] != '\0') {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    const wifi_event_sta_disconnected_t* event = (const wifi_event_sta_disconnected_t*)event_data;
    g_sta_connected = false;
    g_sta_connect_done = true;
    g_sta_disconnect_reason = event != NULL ? event->reason : 0;
    g_sta_ip[0] = '\0';
    if (g_wifi.ssid[0] != '\0') {
      g_wifi_next_reconnect_us =
          esp_timer_get_time() + (int64_t)g_wifi_reconnect_delay_ms * 1000LL;
      if (g_wifi_reconnect_delay_ms < WIFI_RECONNECT_MAX_MS) {
        g_wifi_reconnect_delay_ms *= 2;
        if (g_wifi_reconnect_delay_ms > WIFI_RECONNECT_MAX_MS) {
          g_wifi_reconnect_delay_ms = WIFI_RECONNECT_MAX_MS;
        }
      }
    }
    wake_reconnect_task = true;
  } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t* event = (const ip_event_got_ip_t*)event_data;
    snprintf(g_sta_ip, sizeof(g_sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
    oled_prepare_runtime_qr();
    g_sta_connected = true;
    g_sta_connect_done = true;
    g_sta_disconnect_reason = 0;
    g_wifi_reconnect_delay_ms = WIFI_RECONNECT_BASE_MS;
    g_wifi_next_reconnect_us = 0;
    wake_reconnect_task = true;
  }

  if (wake_reconnect_task && g_wifi_reconnect_task_handle != NULL) {
    xTaskNotifyGive(g_wifi_reconnect_task_handle);
  }
}

static int64_t min_positive_deadline_us(int64_t current, int64_t candidate) {
  if (candidate <= 0) {
    return current;
  }
  return current == 0 || candidate < current ? candidate : current;
}

static TickType_t wifi_reconnect_wait_ticks(int64_t now) {
  if (g_wifi.ssid[0] == '\0') {
    return pdMS_TO_TICKS(WIFI_RECONNECT_IDLE_MS);
  }
  if (g_sta_connected) {
    return pdMS_TO_TICKS(WIFI_RECONNECT_IDLE_MS);
  }

  int64_t next_deadline_us = min_positive_deadline_us(0, g_wifi_next_reconnect_us);
  if (next_deadline_us <= now) {
    return 0;
  }

  int64_t wait_ms = (next_deadline_us - now + 999LL) / 1000LL;
  if (wait_ms > WIFI_RECONNECT_IDLE_MS) {
    wait_ms = WIFI_RECONNECT_IDLE_MS;
  }
  if (wait_ms < 1) {
    wait_ms = 1;
  }
  return pdMS_TO_TICKS((uint32_t)wait_ms);
}

static void wifi_reconnect_task(void* arg) {
  (void)arg;
  g_wifi_reconnect_task_handle = xTaskGetCurrentTaskHandle();
  while (true) {
    const int64_t now = esp_timer_get_time();
    if (g_wifi.ssid[0] != '\0' && !g_sta_connected && g_wifi_next_reconnect_us > 0 &&
        now >= g_wifi_next_reconnect_us) {
      g_wifi_next_reconnect_us = 0;
      esp_wifi_connect();
    }
    ulTaskNotifyTake(pdTRUE, wifi_reconnect_wait_ticks(esp_timer_get_time()));
  }
}

bool wifi_setup_ap_active(void) {
  return g_softap_enabled;
}

bool wifi_station_started(void) {
  return g_wifi_station_started;
}

static const char* wifi_connect_failure_text(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "Unable to join that Wi-Fi network. Check the password and try again.";
    case WIFI_REASON_NO_AP_FOUND:
      return "Unable to find that Wi-Fi network. Check the Wi-Fi name and try again.";
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_ASSOC_EXPIRE:
      return "Unable to associate with that Wi-Fi network. Move closer or try again.";
    case WIFI_REASON_BEACON_TIMEOUT:
      return "Wi-Fi signal was lost while connecting. Move closer or try again.";
    default:
      break;
  }
  return "Unable to connect to that Wi-Fi network. Check the Wi-Fi name and password.";
}

bool wifi_test_station_credentials(const char* ssid, const char* password, char* error, size_t error_size) {
  if (error_size > 0) {
    error[0] = '\0';
  }
  if (ssid == NULL || ssid[0] == '\0') {
    copy_str(error, error_size, "Wi-Fi name is required");
    return false;
  }

  wifi_config_t sta_config = {0};
  copy_str((char*)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), ssid);
  copy_str((char*)sta_config.sta.password, sizeof(sta_config.sta.password), password != NULL ? password : "");
  sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

  esp_wifi_disconnect();
  vTaskDelay(pdMS_TO_TICKS(100));
  g_sta_connect_done = false;
  g_sta_disconnect_reason = 0;
  g_sta_connected = false;
  g_sta_ip[0] = '\0';
  g_wifi_next_reconnect_us = 0;

  esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
  if (result != ESP_OK) {
    snprintf(error, error_size, "Wi-Fi setup failed: %s", esp_err_to_name(result));
    return false;
  }

  result = esp_wifi_connect();
  if (result != ESP_OK) {
    snprintf(error, error_size, "Wi-Fi connect failed: %s", esp_err_to_name(result));
    return false;
  }
  g_wifi_station_started = true;

  const int wait_ticks = WIFI_CONNECT_TIMEOUT_MS / 100;
  for (int i = 0; i < wait_ticks && !g_sta_connected && !g_sta_connect_done; ++i) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (g_sta_connected) {
    return true;
  }

  if (g_sta_disconnect_reason != 0) {
    copy_str(error, error_size, wifi_connect_failure_text(g_sta_disconnect_reason));
  } else {
    copy_str(error, error_size, "Wi-Fi connection timed out. Check the Wi-Fi name and password.");
  }
  esp_wifi_disconnect();
  return false;
}

void start_wifi(void) {
  APP_SERIAL_LOGF("network: init\n");
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  configure_setup_network_identity();

  g_ap_netif = esp_netif_create_default_wifi_ap();
  g_sta_netif = esp_netif_create_default_wifi_sta();
  esp_netif_ip_info_t ip_info = {
      .ip = {.addr = ESP_IP4TOADDR(AP_IP0, AP_IP1, AP_IP2, AP_IP3)},
      .gw = {.addr = ESP_IP4TOADDR(AP_IP0, AP_IP1, AP_IP2, AP_IP3)},
      .netmask = {.addr = ESP_IP4TOADDR(255, 255, 255, 0)},
  };
  ip_info.ip.addr = ESP_IP4TOADDR(g_ap_ip[0], g_ap_ip[1], g_ap_ip[2], g_ap_ip[3]);
  ip_info.gw.addr = ESP_IP4TOADDR(g_ap_ip[0], g_ap_ip[1], g_ap_ip[2], g_ap_ip[3]);
  ESP_ERROR_CHECK(esp_netif_dhcps_stop(g_ap_netif));
  ESP_ERROR_CHECK(esp_netif_set_ip_info(g_ap_netif, &ip_info));
  ESP_ERROR_CHECK(esp_netif_dhcps_start(g_ap_netif));
  APP_SERIAL_LOGF("network: ap netif configured\n");

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init_config));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

  if (g_wifi.ssid[0] != '\0') {
    wifi_config_t sta_config = {0};
    copy_str((char*)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), g_wifi.ssid);
    copy_str((char*)sta_config.sta.password, sizeof(sta_config.sta.password), g_wifi.password);
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    g_softap_enabled = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
  } else {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(configure_softap());
    g_softap_enabled = true;
  }

  ESP_ERROR_CHECK(esp_wifi_start());
  g_wifi_station_started = g_wifi.ssid[0] != '\0';
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
  oled_update_status();
  APP_SERIAL_LOGF("network: wifi started\n");

  if (g_wifi.ssid[0] != '\0') {
    const int wait_ticks = WIFI_CONNECT_TIMEOUT_MS / 100;
    for (int i = 0; i < wait_ticks && !g_sta_connect_done; ++i) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

void start_network_tasks(void) {
  APP_SERIAL_LOGF("network: starting network tasks\n");
  if (g_softap_enabled) {
    xTaskCreate(dns_task, "dns", 3072, NULL, 4, NULL);
  }
  xTaskCreate(wifi_reconnect_task, "wifi_reconnect", 2048, NULL, 4, NULL);
}
