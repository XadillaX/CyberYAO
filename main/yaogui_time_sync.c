#include "yaogui_time_sync.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "nvs_flash.h"

static const char* TAG = "yaogui_time";
static esp_netif_t* s_ap_netif;
static httpd_handle_t s_http_server;
static TaskHandle_t s_dns_task;
static QueueHandle_t s_commands;
static TaskHandle_t s_service_task;
static volatile bool s_dns_running;
static volatile int s_dns_socket = -1;
static volatile bool s_radio_ready;
static volatile uint32_t s_sync_generation;
static volatile uint32_t s_error_generation;

#define TIME_AP_SSID "CyberYAO-Time"
#define TIME_AP_CHANNEL 1
#define TIME_AP_MAX_CONNECTIONS 2
#define SUCCESS_RESPONSE_DELAY_MS 750U
#define DNS_PORT 53
#define DNS_PACKET_MAX 256
#define DNS_HEADER_SIZE 12
#define DNS_ANSWER_SIZE 16
#define DHCP_OFFER_DNS 0x02

typedef enum {
  TIME_COMMAND_START,
  TIME_COMMAND_STOP,
  TIME_COMMAND_STOP_AFTER_RESPONSE,
} time_command_t;

// Keep the offline document readable as HTML/CSS instead of reflowing strings.
// clang-format off
static const char TIME_PAGE[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,"
    "viewport-fit=cover\"><meta name=\"theme-color\" content=\"#a73529\">"
    "<title>CyberYAO 校时</title><style>"
    ":root{color-scheme:light;--paper:#f2dfb8;--paper2:#e5c98f;"
    "--ink:#352014;--muted:#72523a;--cinnabar:#a73529;--gold:#bd8c3d}"
    "*{box-sizing:border-box}html{min-height:100%;background:#c9aa72}"
    "body{min-height:100vh;margin:0;padding:max(20px,env(safe-area-inset-top))"
    " 16px max(20px,env(safe-area-inset-bottom));display:grid;"
    "place-items:center;font-family:'Songti SC','STSong',"
    "'Noto Serif CJK SC',serif;color:var(--ink);"
    "background-color:var(--paper2);background-image:linear-gradient(90deg,"
    "rgba(53,32,20,.055) 1px,transparent 1px),linear-gradient(rgba(53,32,20,"
    ".045) 1px,transparent 1px);background-size:8px 8px}"
    ".sheet{position:relative;width:min(100%,430px);padding:30px 24px 24px;"
    "background:var(--paper);border:3px solid var(--ink);box-shadow:8px 8px 0 "
    "var(--cinnabar),12px 12px 0 var(--ink)}"
    ".sheet:before,.sheet:after{content:'';position:absolute;width:13px;"
    "height:13px;border:2px solid var(--ink);background:var(--gold);top:9px}"
    ".sheet:before{left:9px}.sheet:after{right:9px}"
    "header{text-align:center;border-bottom:2px solid var(--ink);"
    "padding-bottom:"
    "18px}h1{font-size:clamp(30px,9vw,42px);line-height:1;margin:0 0 10px;"
    "letter-spacing:.16em;text-indent:.16em}header p{margin:0;"
    "color:var(--muted);"
    "font-size:14px;letter-spacing:.08em}.seal{position:absolute;right:20px;"
    "top:58px;width:42px;height:42px;display:grid;place-items:center;"
    "border:3px double var(--cinnabar);color:var(--cinnabar);font-weight:700;"
    "font-size:13px;"
    "line-height:1.05;transform:rotate(4deg)}"
    ".dial{position:relative;width:112px;height:112px;margin:24px auto 19px;"
    "border:3px solid var(--ink);border-radius:50%;box-shadow:inset 0 0 0 5px "
    "var(--paper),inset 0 0 0 7px var(--cinnabar)}"
    ".dial:before{content:'';position:absolute;inset:18px;border:2px dashed "
    "var(--gold);border-radius:50%;animation:turn 5s steps(12,end) infinite}"
    ".hand{position:absolute;left:52px;top:19px;width:4px;height:43px;"
    "background:var(--cinnabar);transform-origin:2px 37px;animation:turn 2.4s "
    "steps(12,end) infinite}.hand:after{content:'';position:absolute;left:-4px;"
    "bottom:3px;width:12px;height:12px;background:var(--ink)}"
    "@keyframes turn{to{transform:rotate(360deg)}}"
    ".status{text-align:center;min-height:84px}.eyebrow{margin:0 0 7px;color:"
    "var(--cinnabar);font-size:12px;font-weight:700;letter-spacing:.2em}"
    "#message{margin:0;font-size:21px;font-weight:700;line-height:1.45}"
    "#detail{margin:7px 0 0;color:var(--muted);font-size:14px;line-height:1.55}"
    ".progress{height:8px;margin:18px 0 20px;border:2px solid var(--ink);"
    "background:var(--paper2)}.progress i{display:block;width:34%;height:100%;"
    "background:var(--cinnabar);animation:seek 1.4s steps(5,end) infinite}"
    "@keyframes seek{50%{transform:translateX(190%)}}"
    "button{width:100%;min-height:48px;padding:10px 16px;border:2px solid "
    "var(--ink);border-radius:0;background:var(--cinnabar);"
    "box-shadow:4px 4px 0 "
    "var(--ink);color:var(--paper);font:700 16px/1.2 inherit;letter-spacing:"
    ".12em;cursor:pointer}button:active{transform:translate(4px,4px);"
    "box-shadow:none}button:focus-visible{outline:3px solid var(--gold);"
    "outline-offset:3px}"
    "button[hidden]{display:none}.fallback{margin:23px 0 0;padding-top:17px;"
    "border-top:1px dashed var(--muted);font-size:12px;line-height:1.6;color:"
    "var(--muted);text-align:center}.fallback strong{display:block;color:"
    "var(--ink);font-size:13px}.url{user-select:all;font-family:ui-monospace,"
    "monospace;color:var(--cinnabar);font-weight:700;letter-spacing:.02em}"
    ".ok .dial:before{animation:none;border-style:solid;border-color:"
    "var(--cinnabar)}.ok .hand{animation:none;transform:rotate(135deg)}"
    ".ok .progress i{width:100%;animation:none}.fail .dial,.fail .progress{"
    "border-color:var(--cinnabar)}.fail .hand{animation:none;transform:"
    "rotate(45deg)}.fail .progress i{width:0;animation:none}"
    "@media(max-height:610px){.sheet{padding-top:22px}.dial{width:84px;height:"
    "84px;margin:15px auto}.dial:before{inset:13px}.hand{left:38px;top:13px;"
    "height:34px;transform-origin:2px 29px}.seal{display:none}}"
    "@media(prefers-reduced-motion:reduce){*,*:before,*:after{animation:none"
    "!important;scroll-behavior:auto!important}.progress i{width:58%}}"
    "</style></head><body><main class=\"sheet\" id=\"sheet\"><header>"
    "<h1>校时</h1><p>借手机一刻 · 定龟中辰光</p></header><span class=\"seal\" "
    "aria-hidden=\"true\">摇<br>龟</span><div class=\"dial\" "
    "aria-hidden=\"true\"><i class=\"hand\"></i></div>"
    "<section class=\"status\" aria-live=\"polite\">"
    "<p class=\"eyebrow\" id=\"eyebrow\">正在感知</p><p id=\"message\">读取手机"
    "本地时间</p><p id=\"detail\">请勿关闭此页，完成后热点会自动收起。</p>"
    "</section><div class=\"progress\" aria-hidden=\"true\"><i></i></div>"
    "<button id=\"retry\" type=\"button\" hidden>重新校时</button>"
    "<p class=\"fallback\"><strong>页面未自动打开？</strong>保持连接开放 WiFi "
    "「CyberYAO-Time」，在浏览器输入<span class=\"url\">http://192.168.4.1/"
    "</span></p></main><script>"
    "const $=id=>document.getElementById(id),sheet=$('sheet'),retry=$('retry');"
    "function state(kind,title,msg,detail){sheet.className='sheet '+kind;"
    "$('eyebrow').textContent=title;$('message').textContent=msg;"
    "$('detail').textContent=detail;retry.hidden=kind!=='fail'}"
    "async function sync(){state('','正在校准','将手机时间写入摇龟',"
    "'请保持当前页面开启。');const d=new Date(),q=new URLSearchParams({"
    "year:d.getFullYear(),month:d.getMonth()+1,day:d.getDate(),"
    "hour:d.getHours(),minute:d.getMinutes(),second:d.getSeconds()});"
    "try{const r=await fetch('/sync?'+q,{cache:'no-store'});"
    "if(!r.ok)throw Error(await r.text());state('ok','校时完成','辰光已定',"
    "'热点即将关闭，可以回到 CyberYAO。')}catch(e){state('fail','未能校时',"
    "'请手动重试','确认仍连接 CyberYAO-Time，再点一次重新校时。')}}"
    "retry.addEventListener('click',sync);window.addEventListener('load',"
    "()=>setTimeout(sync,180));</script></body></html>";
// clang-format on

static size_t dns_question_end(const uint8_t* packet, size_t length) {
  size_t offset = DNS_HEADER_SIZE;
  while (offset < length) {
    const uint8_t label_length = packet[offset++];
    if (label_length == 0) break;
    if ((label_length & 0xC0U) != 0 || label_length > 63 ||
        offset + label_length > length) {
      return 0;
    }
    offset += label_length;
  }
  return offset + 4 <= length ? offset + 4 : 0;
}

static size_t make_dns_reply(const uint8_t* request,
                             size_t request_length,
                             uint8_t* reply,
                             size_t reply_size) {
  if (request_length < DNS_HEADER_SIZE || request_length > reply_size ||
      request[2] & 0x80U || request[4] != 0 || request[5] != 1) {
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
  answer[0] = 0xC0;
  answer[1] = 0x0C;
  answer[2] = 0;
  answer[3] = 1;
  answer[4] = 0;
  answer[5] = 1;
  answer[6] = 0;
  answer[7] = 0;
  answer[8] = 0;
  answer[9] = 30;
  answer[10] = 0;
  answer[11] = 4;
  answer[12] = 192;
  answer[13] = 168;
  answer[14] = 4;
  answer[15] = 1;
  return question_end + DNS_ANSWER_SIZE;
}

static void dns_server_task(void* arg) {
  (void)arg;
  uint8_t request[DNS_PACKET_MAX];
  uint8_t reply[DNS_PACKET_MAX];
  struct sockaddr_in address = {
      .sin_family = AF_INET,
      .sin_port = htons(DNS_PORT),
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket_fd < 0 ||
      bind(socket_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
    ESP_LOGE(TAG, "DNS 服务启动失败: errno=%d", errno);
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
  ESP_LOGI(TAG, "DNS wildcard 已监听 0.0.0.0:%d", DNS_PORT);

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
    if (reply_length) {
      (void)sendto(socket_fd,
                   reply,
                   reply_length,
                   0,
                   (struct sockaddr*)&source,
                   source_length);
    }
  }
  s_dns_socket = -1;
  shutdown(socket_fd, SHUT_RDWR);
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
    s_dns_task = NULL;
    return ESP_ERR_NO_MEM;
  }
  for (unsigned attempt = 0; attempt < 20 && s_dns_socket < 0; attempt++) {
    if (!s_dns_task) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return s_dns_socket >= 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void stop_dns_server(void) {
  s_dns_running = false;
  const int socket_fd = s_dns_socket;
  if (socket_fd >= 0) shutdown(socket_fd, SHUT_RDWR);
  for (unsigned attempt = 0; attempt < 20 && s_dns_task; attempt++) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (s_dns_task) {
    vTaskDelete(s_dns_task);
    s_dns_task = NULL;
  }
  s_dns_socket = -1;
}

static bool query_number(const char* query, const char* key, int* value) {
  char text[8];
  if (httpd_query_key_value(query, key, text, sizeof(text)) != ESP_OK) {
    return false;
  }
  char* end = NULL;
  int32_t parsed = (int32_t)strtol(text, &end, 10);
  if (!end || *end != '\0') return false;
  *value = (int)parsed;
  return true;
}

static esp_err_t page_handler(httpd_req_t* request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Pragma", "no-cache");
  return httpd_resp_send(request, TIME_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t captive_probe_handler(httpd_req_t* request) {
  if (strcmp(request->uri, "/hotspot-detect.html") == 0 ||
      strcmp(request->uri, "/library/test/success.html") == 0) {
    return page_handler(request);
  }
  httpd_resp_set_status(request, "302 Found");
  httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(request, "Open the CyberYAO time portal");
}

static esp_err_t sync_handler(httpd_req_t* request) {
  char query[128];
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      !query_number(query, "year", &year) ||
      !query_number(query, "month", &month) ||
      !query_number(query, "day", &day) ||
      !query_number(query, "hour", &hour) ||
      !query_number(query, "minute", &minute) ||
      !query_number(query, "second", &second) || year < 2024 || year > 2099 ||
      month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second < 0 || second > 59) {
    httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid local time");
    return ESP_FAIL;
  }

  struct tm local = {
      .tm_year = year - 1900,
      .tm_mon = month - 1,
      .tm_mday = day,
      .tm_hour = hour,
      .tm_min = minute,
      .tm_sec = second,
      .tm_isdst = -1,
  };
  const struct tm submitted = local;
  setenv("TZ", "CST-8", 1);
  tzset();
  const time_t epoch = mktime(&local);
  if (epoch < 1700000000 || local.tm_year != submitted.tm_year ||
      local.tm_mon != submitted.tm_mon || local.tm_mday != submitted.tm_mday ||
      local.tm_hour != submitted.tm_hour || local.tm_min != submitted.tm_min ||
      local.tm_sec != submitted.tm_sec) {
    httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid local time");
    return ESP_FAIL;
  }

  const struct timeval current = {.tv_sec = epoch, .tv_usec = 0};
  if (settimeofday(&current, NULL) != 0) {
    httpd_resp_send_err(
        request, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot set device time");
    return ESP_FAIL;
  }
  s_sync_generation++;
  ESP_LOGI(TAG,
           "Wi-Fi 校时完成: %04d-%02d-%02d %02d:%02d:%02d",
           year,
           month,
           day,
           hour,
           minute,
           second);
  httpd_resp_set_type(request, "text/plain; charset=utf-8");
  esp_err_t result =
      httpd_resp_sendstr(request, "校时完成，CyberYAO-Time 正在关闭。");
  const time_command_t command = TIME_COMMAND_STOP_AFTER_RESPONSE;
  (void)xQueueSend(s_commands, &command, 0);
  return result;
}

static esp_err_t start_http_server(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 3;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.lru_purge_enable = true;
  esp_err_t error = httpd_start(&s_http_server, &config);
  if (error != ESP_OK) return error;
  const httpd_uri_t page = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = page_handler,
  };
  const httpd_uri_t sync = {
      .uri = "/sync",
      .method = HTTP_GET,
      .handler = sync_handler,
  };
  const httpd_uri_t captive_probe = {
      .uri = "/*",
      .method = HTTP_GET,
      .handler = captive_probe_handler,
  };
  error = httpd_register_uri_handler(s_http_server, &sync);
  if (error == ESP_OK) {
    error = httpd_register_uri_handler(s_http_server, &page);
  }
  if (error == ESP_OK) {
    error = httpd_register_uri_handler(s_http_server, &captive_probe);
  }
  if (error != ESP_OK) {
    httpd_stop(s_http_server);
    s_http_server = NULL;
  }
  return error;
}

static void stop_access_point(void) {
  s_radio_ready = false;
  stop_dns_server();
  if (s_http_server) {
    httpd_stop(s_http_server);
    s_http_server = NULL;
  }
  esp_err_t error = esp_wifi_stop();
  if (error != ESP_OK && error != ESP_ERR_WIFI_NOT_INIT) {
    ESP_LOGW(TAG, "停止 Wi-Fi 热点失败: %s", esp_err_to_name(error));
  }
  error = esp_wifi_deinit();
  if (error != ESP_OK && error != ESP_ERR_WIFI_NOT_INIT) {
    ESP_LOGW(TAG, "反初始化 Wi-Fi 失败: %s", esp_err_to_name(error));
  }
}

static esp_err_t start_access_point(void) {
  if (s_radio_ready) return ESP_OK;
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  init.nvs_enable = 0;
  esp_err_t error = esp_wifi_init(&init);
  if (error != ESP_OK) return error;
  error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (error != ESP_OK) goto failure;
  error = esp_wifi_set_mode(WIFI_MODE_AP);
  if (error != ESP_OK) goto failure;
  wifi_config_t config = {0};
  memcpy(config.ap.ssid, TIME_AP_SSID, sizeof(TIME_AP_SSID));
  config.ap.ssid_len = sizeof(TIME_AP_SSID) - 1;
  config.ap.channel = TIME_AP_CHANNEL;
  config.ap.authmode = WIFI_AUTH_OPEN;
  config.ap.max_connection = TIME_AP_MAX_CONNECTIONS;
  error = esp_wifi_set_config(WIFI_IF_AP, &config);
  if (error != ESP_OK) goto failure;
  error = esp_wifi_start();
  if (error != ESP_OK) goto failure;
  s_radio_ready = true;
  error = start_dns_server();
  if (error != ESP_OK) goto failure;
  error = start_http_server();
  if (error != ESP_OK) goto failure;
  ESP_LOGI(TAG,
           "校时热点与 captive portal 已开启: %s, http://192.168.4.1/",
           TIME_AP_SSID);
  return ESP_OK;

failure:
  stop_access_point();
  return error;
}

static void time_service_task(void* arg) {
  (void)arg;
  time_command_t command;
  for (;;) {
    if (xQueueReceive(s_commands, &command, portMAX_DELAY) != pdTRUE) continue;
    if (command == TIME_COMMAND_START) {
      esp_err_t error = start_access_point();
      if (error != ESP_OK) {
        s_error_generation++;
        ESP_LOGE(TAG, "启动校时热点失败: %s", esp_err_to_name(error));
      }
    } else {
      if (command == TIME_COMMAND_STOP_AFTER_RESPONSE) {
        vTaskDelay(pdMS_TO_TICKS(SUCCESS_RESPONSE_DELAY_MS));
      }
      stop_access_point();
      ESP_LOGI(TAG, "校时热点已关闭");
    }
  }
}

esp_err_t yaogui_time_sync_start(void) {
  esp_err_t error = nvs_flash_init();
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "NVS 初始化失败，未擦除现有数据: %s", esp_err_to_name(error));
    return error;
  }
  error = esp_netif_init();
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
  error = esp_event_loop_create_default();
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
  s_ap_netif = esp_netif_create_default_wifi_ap();
  if (!s_ap_netif) return ESP_ERR_NO_MEM;

  esp_netif_ip_info_t address;
  IP4_ADDR(&address.ip, 192, 168, 4, 1);
  IP4_ADDR(&address.gw, 192, 168, 4, 1);
  IP4_ADDR(&address.netmask, 255, 255, 255, 0);
  error = esp_netif_dhcps_stop(s_ap_netif);
  if (error != ESP_OK && error != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
    return error;
  }
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
  if (error != ESP_OK) return error;
  error = esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
  if (error != ESP_OK) return error;
  error = esp_netif_dhcps_start(s_ap_netif);
  if (error != ESP_OK) return error;

  s_commands = xQueueCreate(4, sizeof(time_command_t));
  if (!s_commands) return ESP_ERR_NO_MEM;
  if (xTaskCreate(
          time_service_task, "yaogui_time", 4096, NULL, 4, &s_service_task) !=
      pdPASS) {
    vQueueDelete(s_commands);
    s_commands = NULL;
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "Wi-Fi 扫码校时服务已就绪");
  return ESP_OK;
}

bool yaogui_time_sync_radio_ready(void) {
  return s_radio_ready;
}

esp_err_t yaogui_time_sync_request(void) {
  if (!s_commands || !s_service_task) return ESP_ERR_INVALID_STATE;
  const time_command_t command = TIME_COMMAND_START;
  return xQueueSend(s_commands, &command, 0) == pdTRUE ? ESP_OK
                                                       : ESP_ERR_TIMEOUT;
}

esp_err_t yaogui_time_sync_cancel(void) {
  if (!s_commands || !s_service_task) return ESP_ERR_INVALID_STATE;
  const time_command_t command = TIME_COMMAND_STOP;
  return xQueueSend(s_commands, &command, 0) == pdTRUE ? ESP_OK
                                                       : ESP_ERR_TIMEOUT;
}

uint32_t yaogui_time_sync_generation(void) {
  return s_sync_generation;
}

uint32_t yaogui_time_sync_error_generation(void) {
  return s_error_generation;
}
