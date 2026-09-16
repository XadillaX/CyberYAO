#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define YAOGUI_WIFI_FAILURE_LIMIT 3U
#define YAOGUI_NO_INTERNET_GRACE_MS 300000U

typedef struct {
  uint8_t consecutive_wifi_failures;
  uint32_t no_internet_since_ms;
  bool no_internet_timer_active;
} yaogui_network_policy_t;

void yaogui_network_policy_init(yaogui_network_policy_t* policy);
void yaogui_network_policy_begin_wifi_attempt(yaogui_network_policy_t* policy);
void yaogui_network_policy_on_dhcp(yaogui_network_policy_t* policy,
                                   uint32_t now_ms);
void yaogui_network_policy_on_internet(yaogui_network_policy_t* policy);
bool yaogui_network_policy_on_wifi_failure(yaogui_network_policy_t* policy);
bool yaogui_network_policy_on_no_internet(yaogui_network_policy_t* policy,
                                          uint32_t now_ms);

/* 使用设备 MAC 后三字节生成稳定、可辨识的配网热点名。 */
bool yaogui_format_ap_ssid(char* output,
                           size_t output_size,
                           const uint8_t mac[6]);

/* 解码受限 application/x-www-form-urlencoded 字段；拒绝控制符和截断。 */
bool yaogui_form_value(const char* body,
                       const char* key,
                       char* output,
                       size_t output_size);
