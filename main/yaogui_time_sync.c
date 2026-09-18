#include "yaogui_time_sync.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "yaogui_logic.h"
#include "yaogui_network_policy.h"

#define PORTAL_AP_CHANNEL 1
#define PORTAL_AP_MAX_CONNECTIONS 3
#define PORTAL_IP_A 66
#define PORTAL_IP_B 66
#define PORTAL_IP_C 66
#define PORTAL_IP_D 66
#define DNS_PORT 53
#define DNS_PACKET_MAX 256
#define DNS_HEADER_SIZE 12
#define DNS_ANSWER_SIZE 16
#define DHCP_OFFER_DNS 0x02
#define HTTP_BODY_MAX 256
#define SCAN_RESULT_MAX 12
#define WIFI_SSID_TEXT_SIZE 33
#define WIFI_PASSWORD_TEXT_SIZE 65
#define WIFI_RETRY_DELAY_MS 2000U
#define WIFI_CONNECT_TIMEOUT_MS 30000U
#define NTP_WAIT_MS 15000U
#define NTP_RETRY_MS 30000U
#define PORTAL_SUCCESS_HOLD_MS 2500U
#define MONITOR_INTERVAL_MS 1000U
#define NVS_NAMESPACE "yaogui_net"
#define NVS_SSID "ssid"
#define NVS_PASSWORD "password"

typedef enum {
  NETWORK_COMMAND_OPEN_PORTAL,
  NETWORK_COMMAND_CONNECT,
  NETWORK_COMMAND_GOT_IP,
  NETWORK_COMMAND_WIFI_FAILED,
} network_command_type_t;

typedef struct {
  network_command_type_t type;
  char ssid[WIFI_SSID_TEXT_SIZE];
  char password[WIFI_PASSWORD_TEXT_SIZE];
} network_command_t;

typedef enum {
  NETWORK_STATE_PORTAL,
  NETWORK_STATE_CONNECTING,
  NETWORK_STATE_DHCP,
  NETWORK_STATE_ONLINE,
  NETWORK_STATE_FAILED,
} network_state_t;

extern const uint8_t yaogui_portal_html_gz[];
extern const size_t yaogui_portal_html_gz_size;
extern const uint8_t yaogui_divination_html_gz[];
extern const size_t yaogui_divination_html_gz_size;

typedef struct {
  uint8_t lines[YAOGUI_LINE_COUNT];
  int64_t timestamp_seconds;
  bool valid;
} reading_snapshot_t;

static const char* TAG = "yaogui_net";
static esp_netif_t* s_ap_netif;
static esp_netif_t* s_sta_netif;
static httpd_handle_t s_http_server;
static TaskHandle_t s_dns_task;
static TaskHandle_t s_service_task;
static QueueHandle_t s_commands;
static volatile bool s_dns_running;
static volatile int s_dns_socket = -1;
static volatile bool s_radio_ready;
static volatile bool s_sta_has_ip;
static volatile bool s_ignore_next_disconnect;
static volatile bool s_ntp_notified;
static volatile network_state_t s_state = NETWORK_STATE_FAILED;
static volatile uint32_t s_sync_generation;
static volatile uint32_t s_error_generation;
static volatile uint32_t s_portal_generation;
static bool s_wifi_started;
static bool s_portal_open;
static bool s_pending_should_persist;
static uint32_t s_connect_started_ms;
static uint32_t s_last_ntp_attempt_ms;
static network_command_t s_active_credentials;
static yaogui_network_policy_t s_policy;
static char s_portal_ap_ssid[WIFI_SSID_TEXT_SIZE] = "CyberYAO";
static portMUX_TYPE s_reading_lock = portMUX_INITIALIZER_UNLOCKED;
static reading_snapshot_t s_reading;

static uint32_t now_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static const char* state_name(network_state_t state) {
  switch (state) {
    case NETWORK_STATE_PORTAL:
      return "portal";
    case NETWORK_STATE_CONNECTING:
      return "connecting";
    case NETWORK_STATE_DHCP:
      return "dhcp";
    case NETWORK_STATE_ONLINE:
      return "online";
    default:
      return "failed";
  }
}

static size_t dns_question_end(const uint8_t* packet, size_t length) {
  size_t offset = DNS_HEADER_SIZE;
  while (offset < length) {
    const uint8_t label_length = packet[offset++];
    if (label_length == 0) break;
    if ((label_length & 0xC0U) != 0 || label_length > 63U ||
        offset + label_length > length) {
      return 0;
    }
    offset += label_length;
  }
  return offset + 4U <= length ? offset + 4U : 0;
}

static size_t make_dns_reply(const uint8_t* request,
                             size_t request_length,
                             uint8_t* reply,
                             size_t reply_size) {
  if (!request || !reply || request_length < DNS_HEADER_SIZE ||
      request_length > reply_size || (request[2] & 0x80U) != 0 ||
      request[4] != 0 || request[5] != 1) {
    return 0;
  }
  const size_t question_end = dns_question_end(request, request_length);
  if (!question_end) return 0;
  memcpy(reply, request, question_end);
  reply[2] = 0x81;
  reply[3] = 0x80;
  memset(reply + 6, 0, 6);
  const uint16_t query_type =
      (uint16_t)((request[question_end - 4] << 8) | request[question_end - 3]);
  const uint16_t query_class =
      (uint16_t)((request[question_end - 2] << 8) | request[question_end - 1]);
  if (query_type != 1 || query_class != 1) return question_end;
  if (question_end + DNS_ANSWER_SIZE > reply_size) return 0;
  reply[7] = 1;
  uint8_t* answer = reply + question_end;
  const uint8_t fixed[] = {0xC0,
                           0x0C,
                           0,
                           1,
                           0,
                           1,
                           0,
                           0,
                           0,
                           30,
                           0,
                           4,
                           PORTAL_IP_A,
                           PORTAL_IP_B,
                           PORTAL_IP_C,
                           PORTAL_IP_D};
  memcpy(answer, fixed, sizeof(fixed));
  return question_end + DNS_ANSWER_SIZE;
}

static void dns_server_task(void* arg) {
  (void)arg;
  uint8_t request[DNS_PACKET_MAX];
  uint8_t reply[DNS_PACKET_MAX];
  const struct sockaddr_in address = {
      .sin_family = AF_INET,
      .sin_port = htons(DNS_PORT),
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket_fd < 0 ||
      bind(socket_fd, (const struct sockaddr*)&address, sizeof(address)) < 0) {
    if (socket_fd >= 0) close(socket_fd);
    s_dns_running = false;
    s_dns_task = NULL;
    vTaskDelete(NULL);
    return;
  }
  const struct timeval timeout = {.tv_sec = 0, .tv_usec = 250000};
  (void)setsockopt(
      socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  s_dns_socket = socket_fd;
  while (s_dns_running) {
    struct sockaddr_storage source;
    socklen_t source_length = sizeof(source);
    const int received = recvfrom(socket_fd,
                                  request,
                                  sizeof(request),
                                  0,
                                  (struct sockaddr*)&source,
                                  &source_length);
    if (received <= 0) continue;
    const size_t reply_length =
        make_dns_reply(request, (size_t)received, reply, sizeof(reply));
    if (reply_length > 0) {
      (void)sendto(socket_fd,
                   reply,
                   reply_length,
                   0,
                   (struct sockaddr*)&source,
                   source_length);
    }
  }
  s_dns_socket = -1;
  close(socket_fd);
  s_dns_task = NULL;
  vTaskDelete(NULL);
}

static esp_err_t start_dns_server(void) {
  if (s_dns_task) return ESP_OK;
  s_dns_running = true;
  if (xTaskCreate(dns_server_task, "yaogui_dns", 3072, NULL, 4, &s_dns_task) !=
      pdPASS) {
    s_dns_running = false;
    return ESP_ERR_NO_MEM;
  }
  for (unsigned attempt = 0; attempt < 20U && s_dns_socket < 0; attempt++) {
    if (!s_dns_task) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return s_dns_socket >= 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void stop_dns_server(void) {
  s_dns_running = false;
  if (s_dns_socket >= 0) shutdown(s_dns_socket, SHUT_RDWR);
  for (unsigned attempt = 0; attempt < 20U && s_dns_task; attempt++) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (s_dns_task) {
    vTaskDelete(s_dns_task);
    s_dns_task = NULL;
  }
  s_dns_socket = -1;
}

static esp_err_t page_handler(httpd_req_t* request) {
  if (!s_portal_open) {
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "/guaxiang.html");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "Open the CyberYAO reading page");
  }
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(
      request, (const char*)yaogui_portal_html_gz, yaogui_portal_html_gz_size);
}

static esp_err_t divination_page_handler(httpd_req_t* request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request,
                         (const char*)yaogui_divination_html_gz,
                         yaogui_divination_html_gz_size);
}

static const char* moving_line_name(size_t index, yaogui_line_t line) {
  static const char* const yin_names[YAOGUI_LINE_COUNT] = {
      "初六", "六二", "六三", "六四", "六五", "上六"};
  static const char* const yang_names[YAOGUI_LINE_COUNT] = {
      "初九", "九二", "九三", "九四", "九五", "上九"};
  return yaogui_line_is_yang(line) ? yang_names[index] : yin_names[index];
}

static esp_err_t reading_handler(httpd_req_t* request) {
  reading_snapshot_t snapshot;
  portENTER_CRITICAL(&s_reading_lock);
  snapshot = s_reading;
  portEXIT_CRITICAL(&s_reading_lock);
  if (!snapshot.valid) {
    return httpd_resp_send_err(
        request, HTTPD_404_NOT_FOUND, "no complete reading");
  }
  yaogui_line_t lines[YAOGUI_LINE_COUNT];
  char raw[YAOGUI_LINE_COUNT + 1];
  char moving[96] = "";
  size_t moving_length = 0;
  for (size_t i = 0; i < YAOGUI_LINE_COUNT; i++) {
    lines[i] = (yaogui_line_t)snapshot.lines[i];
    raw[i] = (char)('0' + snapshot.lines[i]);
    if (!yaogui_line_is_old(lines[i])) continue;
    const char* name = moving_line_name(i, lines[i]);
    const int written = snprintf(moving + moving_length,
                                 sizeof(moving) - moving_length,
                                 "%s%s",
                                 moving_length == 0 ? "" : "、",
                                 name);
    if (written > 0 && (size_t)written < sizeof(moving) - moving_length) {
      moving_length += (size_t)written;
    }
  }
  raw[YAOGUI_LINE_COUNT] = '\0';
  if (moving_length == 0) snprintf(moving, sizeof(moving), "无动爻");

  const yaogui_hexagram_t* primary = NULL;
  const yaogui_hexagram_t* changed = NULL;
  if (!yaogui_hexagram_from_lines(lines, false, &primary) ||
      !yaogui_hexagram_from_lines(lines, true, &changed)) {
    return httpd_resp_send_err(
        request, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid reading");
  }
  char response[384];
  const int length = snprintf(response,
                              sizeof(response),
                              "{\"timestamp\":%" PRId64
                              ",\"hexagram\":\"%s\",\"changed\":\"%s\","
                              "\"lines\":\"%s\",\"raw\":\"%s\"}",
                              snapshot.timestamp_seconds,
                              primary->name,
                              changed->name,
                              moving,
                              raw);
  if (length < 0 || (size_t)length >= sizeof(response)) {
    return httpd_resp_send_err(
        request, HTTPD_500_INTERNAL_SERVER_ERROR, "reading too large");
  }
  httpd_resp_set_type(request, "application/json; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, response, length);
}

static esp_err_t status_handler(httpd_req_t* request) {
  char response[48];
  snprintf(
      response, sizeof(response), "{\"state\":\"%s\"}", state_name(s_state));
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(request, response);
}

static esp_err_t send_json_string(httpd_req_t* request,
                                  const uint8_t* value,
                                  size_t length) {
  if (httpd_resp_send_chunk(request, "\"", 1) != ESP_OK) return ESP_FAIL;
  char chunk[7];
  for (size_t i = 0; i < length; i++) {
    const uint8_t byte = value[i];
    if (byte == '"' || byte == '\\') {
      const char escaped[2] = {'\\', (char)byte};
      if (httpd_resp_send_chunk(request, escaped, sizeof(escaped)) != ESP_OK)
        return ESP_FAIL;
    } else if (byte < 0x20U) {
      snprintf(chunk, sizeof(chunk), "\\u%04x", byte);
      if (httpd_resp_send_chunk(request, chunk, 6) != ESP_OK) return ESP_FAIL;
    } else if (httpd_resp_send_chunk(request, (const char*)&byte, 1) !=
               ESP_OK) {
      return ESP_FAIL;
    }
  }
  return httpd_resp_send_chunk(request, "\"", 1);
}

static esp_err_t networks_handler(httpd_req_t* request) {
  if (!s_portal_open) {
    return httpd_resp_send_err(
        request, HTTPD_403_FORBIDDEN, "provisioning portal is closed");
  }
  wifi_scan_config_t scan = {
      .show_hidden = true,
  };
  esp_err_t error = esp_wifi_scan_start(&scan, true);
  if (error != ESP_OK) {
    return httpd_resp_send_err(
        request, HTTPD_500_INTERNAL_SERVER_ERROR, "scan failed");
  }
  uint16_t count = SCAN_RESULT_MAX;
  wifi_ap_record_t records[SCAN_RESULT_MAX];
  memset(records, 0, sizeof(records));
  error = esp_wifi_scan_get_ap_records(&count, records);
  if (error != ESP_OK) {
    return httpd_resp_send_err(
        request, HTTPD_500_INTERNAL_SERVER_ERROR, "scan failed");
  }
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  if (httpd_resp_send_chunk(request, "[", 1) != ESP_OK) return ESP_FAIL;
  for (uint16_t i = 0; i < count; i++) {
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "%s{\"ssid\":", i == 0 ? "" : ",");
    if (httpd_resp_sendstr_chunk(request, prefix) != ESP_OK ||
        send_json_string(request,
                         records[i].ssid,
                         strnlen((char*)records[i].ssid, 32U)) != ESP_OK) {
      return ESP_FAIL;
    }
    char suffix[48];
    snprintf(suffix,
             sizeof(suffix),
             ",\"rssi\":%d,\"open\":%s}",
             records[i].rssi,
             records[i].authmode == WIFI_AUTH_OPEN ? "true" : "false");
    if (httpd_resp_sendstr_chunk(request, suffix) != ESP_OK) return ESP_FAIL;
  }
  if (httpd_resp_send_chunk(request, "]", 1) != ESP_OK) return ESP_FAIL;
  return httpd_resp_send_chunk(request, NULL, 0);
}

static esp_err_t connect_handler(httpd_req_t* request) {
  if (!s_portal_open) {
    return httpd_resp_send_err(
        request, HTTPD_403_FORBIDDEN, "provisioning portal is closed");
  }
  if (request->content_len <= 0 || request->content_len >= HTTP_BODY_MAX) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid body");
  }
  char body[HTTP_BODY_MAX];
  size_t received = 0;
  while (received < (size_t)request->content_len) {
    const int result = httpd_req_recv(
        request, body + received, (size_t)request->content_len - received);
    if (result <= 0) {
      return httpd_resp_send_err(
          request, HTTPD_400_BAD_REQUEST, "invalid body");
    }
    received += (size_t)result;
  }
  body[received] = '\0';
  network_command_t command = {.type = NETWORK_COMMAND_CONNECT};
  if (!yaogui_form_value(body, "ssid", command.ssid, sizeof(command.ssid)) ||
      !yaogui_form_value(
          body, "password", command.password, sizeof(command.password)) ||
      command.ssid[0] == '\0') {
    return httpd_resp_send_err(
        request, HTTPD_400_BAD_REQUEST, "invalid credentials");
  }
  const size_t password_length = strlen(command.password);
  if (password_length > 0 && password_length < 8U) {
    return httpd_resp_send_err(
        request, HTTPD_400_BAD_REQUEST, "invalid password");
  }
  if (xQueueSend(s_commands, &command, 0) != pdTRUE) {
    httpd_resp_set_status(request, "503 Service Unavailable");
    return httpd_resp_sendstr(request, "busy");
  }
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_sendstr(request, "{\"accepted\":true}");
}

static esp_err_t captive_handler(httpd_req_t* request) {
  httpd_resp_set_status(request, "302 Found");
  httpd_resp_set_hdr(request, "Location", "http://cyberyao/");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(request, "Open the CyberYAO portal");
}

static esp_err_t start_http_server(void) {
  if (s_http_server) return ESP_OK;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 7;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.lru_purge_enable = true;
  esp_err_t error = httpd_start(&s_http_server, &config);
  if (error != ESP_OK) return error;
  const httpd_uri_t handlers[] = {
      {.uri = "/connect", .method = HTTP_POST, .handler = connect_handler},
      {.uri = "/networks", .method = HTTP_GET, .handler = networks_handler},
      {.uri = "/status", .method = HTTP_GET, .handler = status_handler},
      {.uri = "/api/reading", .method = HTTP_GET, .handler = reading_handler},
      {.uri = "/guaxiang.html",
       .method = HTTP_GET,
       .handler = divination_page_handler},
      {.uri = "/", .method = HTTP_GET, .handler = page_handler},
      {.uri = "/*", .method = HTTP_GET, .handler = captive_handler},
  };
  for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
    error = httpd_register_uri_handler(s_http_server, &handlers[i]);
    if (error != ESP_OK) {
      httpd_stop(s_http_server);
      s_http_server = NULL;
      return error;
    }
  }
  return ESP_OK;
}

static void stop_portal_services(void) {
  stop_dns_server();
  s_portal_open = false;
}

static esp_err_t set_wifi_mode(wifi_mode_t mode) {
  esp_err_t error = esp_wifi_set_mode(mode);
  if (error != ESP_OK) return error;
  if (!s_wifi_started) {
    error = esp_wifi_start();
    if (error == ESP_OK) {
      s_wifi_started = true;
      s_radio_ready = true;
    }
  }
  return error;
}

static esp_err_t open_portal(void) {
  if (s_portal_open) return ESP_OK;
  esp_err_t error = set_wifi_mode(WIFI_MODE_APSTA);
  if (error != ESP_OK) return error;
  wifi_config_t config = {0};
  config.ap.ssid_len = strlen(s_portal_ap_ssid);
  memcpy(config.ap.ssid, s_portal_ap_ssid, config.ap.ssid_len);
  config.ap.channel = PORTAL_AP_CHANNEL;
  config.ap.authmode = WIFI_AUTH_OPEN;
  config.ap.max_connection = PORTAL_AP_MAX_CONNECTIONS;
  error = esp_wifi_set_config(WIFI_IF_AP, &config);
  if (error == ESP_OK) error = start_dns_server();
  if (error == ESP_OK) error = start_http_server();
  if (error != ESP_OK) {
    stop_portal_services();
    return error;
  }
  s_portal_open = true;
  s_state = NETWORK_STATE_PORTAL;
  s_portal_generation++;
  ESP_LOGI(TAG,
           "配网热点已开启: %s, http://cyberyao/ (%d.%d.%d.%d)",
           s_portal_ap_ssid,
           PORTAL_IP_A,
           PORTAL_IP_B,
           PORTAL_IP_C,
           PORTAL_IP_D);
  return ESP_OK;
}

static bool load_credentials(network_command_t* credentials) {
  if (!credentials) return false;
  nvs_handle_t nvs;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;
  size_t ssid_size = sizeof(credentials->ssid);
  size_t password_size = sizeof(credentials->password);
  const esp_err_t ssid_error =
      nvs_get_str(nvs, NVS_SSID, credentials->ssid, &ssid_size);
  const esp_err_t password_error =
      nvs_get_str(nvs, NVS_PASSWORD, credentials->password, &password_size);
  nvs_close(nvs);
  credentials->type = NETWORK_COMMAND_CONNECT;
  return ssid_error == ESP_OK && password_error == ESP_OK &&
         credentials->ssid[0] != '\0';
}

static esp_err_t save_credentials(const network_command_t* credentials) {
  nvs_handle_t nvs;
  esp_err_t error = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
  if (error != ESP_OK) return error;
  error = nvs_set_str(nvs, NVS_SSID, credentials->ssid);
  if (error == ESP_OK) {
    error = nvs_set_str(nvs, NVS_PASSWORD, credentials->password);
  }
  if (error == ESP_OK) error = nvs_commit(nvs);
  nvs_close(nvs);
  return error;
}

static esp_err_t connect_station(const network_command_t* credentials,
                                 bool should_persist) {
  if (!credentials || credentials->ssid[0] == '\0') return ESP_ERR_INVALID_ARG;
  if (should_persist) yaogui_network_policy_begin_wifi_attempt(&s_policy);
  esp_err_t error =
      set_wifi_mode(s_portal_open ? WIFI_MODE_APSTA : WIFI_MODE_STA);
  if (error != ESP_OK) return error;
  wifi_config_t config = {0};
  const size_t ssid_length =
      strnlen(credentials->ssid, sizeof(config.sta.ssid));
  const size_t password_length =
      strnlen(credentials->password, sizeof(config.sta.password));
  memcpy(config.sta.ssid, credentials->ssid, ssid_length);
  memcpy(config.sta.password, credentials->password, password_length);
  config.sta.threshold.authmode =
      password_length == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  config.sta.pmf_cfg.capable = true;
  s_ignore_next_disconnect = s_sta_has_ip;
  s_sta_has_ip = false;
  s_state = NETWORK_STATE_CONNECTING;
  error = esp_wifi_set_config(WIFI_IF_STA, &config);
  if (error == ESP_OK) error = esp_wifi_connect();
  if (error == ESP_OK) {
    s_active_credentials = *credentials;
    s_pending_should_persist = should_persist;
    s_connect_started_ms = now_ms();
  }
  return error;
}

static void ntp_notification(struct timeval* value) {
  (void)value;
  s_ntp_notified = true;
}

static bool sync_ntp(void) {
  s_ntp_notified = false;
  esp_sntp_stop();
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_set_time_sync_notification_cb(ntp_notification);
  esp_sntp_init();
  const uint32_t started = now_ms();
  while (!s_ntp_notified && (uint32_t)(now_ms() - started) < NTP_WAIT_MS) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  esp_sntp_stop();
  if (!s_ntp_notified) return false;
  setenv("TZ", "CST-8", 1);
  tzset();
  s_sync_generation++;
  return true;
}

static void wifi_event(void* arg,
                       esp_event_base_t event_base,
                       int32_t event_id,
                       void* event_data) {
  (void)arg;
  network_command_t command = {0};
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    s_sta_has_ip = true;
    command.type = NETWORK_COMMAND_GOT_IP;
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    const wifi_event_sta_disconnected_t* disconnected = event_data;
    ESP_LOGW(TAG,
             "WiFi 连接断开，reason=%u",
             disconnected ? disconnected->reason : 0U);
    s_sta_has_ip = false;
    if (s_ignore_next_disconnect) {
      s_ignore_next_disconnect = false;
      return;
    }
    command.type = NETWORK_COMMAND_WIFI_FAILED;
  } else {
    return;
  }
  if (s_commands) (void)xQueueSend(s_commands, &command, 0);
}

static void handle_wifi_failure(void) {
  if (yaogui_network_policy_on_wifi_failure(&s_policy)) {
    s_state = NETWORK_STATE_FAILED;
    if (open_portal() != ESP_OK) s_error_generation++;
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
  if (esp_wifi_connect() != ESP_OK) {
    network_command_t retry = {.type = NETWORK_COMMAND_WIFI_FAILED};
    (void)xQueueSend(s_commands, &retry, 0);
  }
}

static void handle_got_ip(void) {
  s_connect_started_ms = 0;
  s_state = NETWORK_STATE_DHCP;
  yaogui_network_policy_on_dhcp(&s_policy, now_ms());
  if (s_pending_should_persist) {
    const esp_err_t error = save_credentials(&s_active_credentials);
    if (error != ESP_OK) {
      ESP_LOGW(TAG, "DHCP 成功但保存网络凭据失败: %s", esp_err_to_name(error));
    }
    s_pending_should_persist = false;
  }
  s_last_ntp_attempt_ms = now_ms();
  if (sync_ntp()) {
    yaogui_network_policy_on_internet(&s_policy);
    s_state = NETWORK_STATE_ONLINE;
    ESP_LOGI(TAG, "已联网并通过 NTP 校时");
    if (s_portal_open) {
      vTaskDelay(pdMS_TO_TICKS(PORTAL_SUCCESS_HOLD_MS));
      stop_portal_services();
      (void)set_wifi_mode(WIFI_MODE_STA);
    }
  } else {
    ESP_LOGW(TAG, "已取得 DHCP 地址，NTP 暂不可达，将继续后台重试");
  }
}

static void monitor_wifi_connection(void) {
  if (s_state != NETWORK_STATE_CONNECTING ||
      (uint32_t)(now_ms() - s_connect_started_ms) < WIFI_CONNECT_TIMEOUT_MS) {
    return;
  }
  ESP_LOGW(TAG, "WiFi 连接超过 30 秒，返回配网页");
  s_state = NETWORK_STATE_FAILED;
  s_ignore_next_disconnect = true;
  (void)esp_wifi_disconnect();
  if (open_portal() != ESP_OK) s_error_generation++;
}

static void monitor_internet(void) {
  if (!s_sta_has_ip ||
      (uint32_t)(now_ms() - s_last_ntp_attempt_ms) < NTP_RETRY_MS) {
    return;
  }
  s_last_ntp_attempt_ms = now_ms();
  if (sync_ntp()) {
    yaogui_network_policy_on_internet(&s_policy);
    s_state = NETWORK_STATE_ONLINE;
    if (s_portal_open) {
      vTaskDelay(pdMS_TO_TICKS(PORTAL_SUCCESS_HOLD_MS));
      stop_portal_services();
      (void)set_wifi_mode(WIFI_MODE_STA);
    }
    return;
  }
  if (yaogui_network_policy_on_no_internet(&s_policy, now_ms())) {
    ESP_LOGW(TAG, "互联网长期不可达，重新开放配网热点");
    if (open_portal() != ESP_OK) s_error_generation++;
  }
}

static void network_service_task(void* arg) {
  (void)arg;
  network_command_t saved = {0};
  if (load_credentials(&saved)) {
    if (connect_station(&saved, false) != ESP_OK) handle_wifi_failure();
  } else if (open_portal() != ESP_OK) {
    s_error_generation++;
  }
  for (;;) {
    network_command_t command;
    if (xQueueReceive(s_commands,
                      &command,
                      pdMS_TO_TICKS(MONITOR_INTERVAL_MS)) == pdTRUE) {
      switch (command.type) {
        case NETWORK_COMMAND_OPEN_PORTAL:
          if (open_portal() != ESP_OK) s_error_generation++;
          break;
        case NETWORK_COMMAND_CONNECT:
          if (connect_station(&command, true) != ESP_OK) handle_wifi_failure();
          break;
        case NETWORK_COMMAND_GOT_IP:
          handle_got_ip();
          break;
        case NETWORK_COMMAND_WIFI_FAILED:
          handle_wifi_failure();
          break;
      }
    }
    monitor_wifi_connection();
    monitor_internet();
  }
}

static esp_err_t configure_ap_network(void) {
  esp_netif_ip_info_t address;
  IP4_ADDR(&address.ip, PORTAL_IP_A, PORTAL_IP_B, PORTAL_IP_C, PORTAL_IP_D);
  address.gw = address.ip;
  IP4_ADDR(&address.netmask, 255, 255, 255, 0);
  esp_err_t error = esp_netif_dhcps_stop(s_ap_netif);
  if (error != ESP_OK && error != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
    return error;
  error = esp_netif_set_ip_info(s_ap_netif, &address);
  if (error != ESP_OK) return error;
  esp_netif_dns_info_t dns = {
      .ip.type = IPADDR_TYPE_V4,
      .ip.u_addr.ip4.addr = address.ip.addr,
  };
  uint8_t offer_dns = DHCP_OFFER_DNS;
  error = esp_netif_dhcps_option(s_ap_netif,
                                 ESP_NETIF_OP_SET,
                                 ESP_NETIF_DOMAIN_NAME_SERVER,
                                 &offer_dns,
                                 sizeof(offer_dns));
  if (error == ESP_OK) {
    error = esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
  }
  if (error == ESP_OK) error = esp_netif_set_hostname(s_ap_netif, "cyberyao");
  if (error == ESP_OK) error = esp_netif_dhcps_start(s_ap_netif);
  return error;
}

esp_err_t yaogui_time_sync_start(void) {
  esp_err_t error = nvs_flash_init();
  if (error != ESP_OK) return error;
  error = esp_netif_init();
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
  error = esp_event_loop_create_default();
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
  s_ap_netif = esp_netif_create_default_wifi_ap();
  s_sta_netif = esp_netif_create_default_wifi_sta();
  if (!s_ap_netif || !s_sta_netif) return ESP_ERR_NO_MEM;
  error = configure_ap_network();
  if (error != ESP_OK) return error;
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  init.nvs_enable = 0;
  error = esp_wifi_init(&init);
  if (error != ESP_OK) return error;
  uint8_t softap_mac[6];
  error = esp_read_mac(softap_mac, ESP_MAC_WIFI_SOFTAP);
  if (error != ESP_OK ||
      !yaogui_format_ap_ssid(
          s_portal_ap_ssid, sizeof(s_portal_ap_ssid), softap_mac)) {
    return error == ESP_OK ? ESP_ERR_INVALID_SIZE : error;
  }
  error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (error != ESP_OK) return error;
  error = esp_event_handler_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
  if (error == ESP_OK) {
    error = esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL);
  }
  if (error != ESP_OK) return error;
  error = set_wifi_mode(WIFI_MODE_STA);
  if (error != ESP_OK) return error;
  error = start_http_server();
  if (error != ESP_OK) return error;
  s_commands = xQueueCreate(6, sizeof(network_command_t));
  if (!s_commands) return ESP_ERR_NO_MEM;
  yaogui_network_policy_init(&s_policy);
  if (xTaskCreate(
          network_service_task, "yaogui_net", 6144, NULL, 4, &s_service_task) !=
      pdPASS) {
    vQueueDelete(s_commands);
    s_commands = NULL;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

bool yaogui_time_sync_radio_ready(void) {
  return s_radio_ready;
}

bool yaogui_time_sync_has_ip(void) {
  return s_sta_has_ip;
}

bool yaogui_time_sync_internet_ready(void) {
  return s_state == NETWORK_STATE_ONLINE;
}

bool yaogui_time_sync_connecting(void) {
  return s_state == NETWORK_STATE_CONNECTING;
}

const char* yaogui_time_sync_ap_ssid(void) {
  return s_portal_ap_ssid;
}

void yaogui_time_sync_set_reading(const uint8_t lines[YAOGUI_LINE_COUNT],
                                  int64_t timestamp_seconds) {
  if (!lines) return;
  reading_snapshot_t snapshot = {
      .timestamp_seconds = timestamp_seconds,
      .valid = true,
  };
  for (size_t i = 0; i < YAOGUI_LINE_COUNT; i++) {
    if (lines[i] < YAOGUI_OLD_YIN || lines[i] > YAOGUI_OLD_YANG) return;
    snapshot.lines[i] = lines[i];
  }
  portENTER_CRITICAL(&s_reading_lock);
  s_reading = snapshot;
  portEXIT_CRITICAL(&s_reading_lock);
}

void yaogui_time_sync_clear_reading(void) {
  portENTER_CRITICAL(&s_reading_lock);
  memset(&s_reading, 0, sizeof(s_reading));
  portEXIT_CRITICAL(&s_reading_lock);
}

bool yaogui_time_sync_reading_url(char* buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0 || !s_sta_has_ip) return false;
  portENTER_CRITICAL(&s_reading_lock);
  const bool reading_valid = s_reading.valid;
  portEXIT_CRITICAL(&s_reading_lock);
  if (!reading_valid) return false;
  esp_netif_ip_info_t address;
  if (!s_sta_netif || esp_netif_get_ip_info(s_sta_netif, &address) != ESP_OK ||
      address.ip.addr == 0) {
    return false;
  }
  const int written = snprintf(buffer,
                               buffer_size,
                               "http://" IPSTR "/guaxiang.html",
                               IP2STR(&address.ip));
  return written > 0 && (size_t)written < buffer_size;
}

esp_err_t yaogui_time_sync_request(void) {
  if (!s_commands || !s_service_task) return ESP_ERR_INVALID_STATE;
  const network_command_t command = {.type = NETWORK_COMMAND_OPEN_PORTAL};
  return xQueueSend(s_commands, &command, 0) == pdTRUE ? ESP_OK
                                                       : ESP_ERR_TIMEOUT;
}

esp_err_t yaogui_time_sync_cancel(void) {
  return ESP_OK;
}

uint32_t yaogui_time_sync_generation(void) {
  return s_sync_generation;
}

uint32_t yaogui_time_sync_error_generation(void) {
  return s_error_generation;
}

uint32_t yaogui_time_sync_portal_generation(void) {
  return s_portal_generation;
}

bool yaogui_time_sync_required(void) {
  network_command_t credentials = {0};
  return !load_credentials(&credentials);
}
