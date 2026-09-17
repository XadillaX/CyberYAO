#include "yaogui_network_policy.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

void yaogui_network_policy_init(yaogui_network_policy_t* policy) {
  if (!policy) return;
  *policy = (yaogui_network_policy_t){0};
}

void yaogui_network_policy_begin_wifi_attempt(yaogui_network_policy_t* policy) {
  if (!policy) return;
  policy->consecutive_wifi_failures = 0;
}

void yaogui_network_policy_on_dhcp(yaogui_network_policy_t* policy,
                                   uint32_t now_ms) {
  if (!policy) return;
  policy->consecutive_wifi_failures = 0;
  policy->no_internet_since_ms = now_ms;
  policy->no_internet_timer_active = true;
}

void yaogui_network_policy_on_internet(yaogui_network_policy_t* policy) {
  if (!policy) return;
  policy->consecutive_wifi_failures = 0;
  policy->no_internet_timer_active = false;
}

bool yaogui_network_policy_on_wifi_failure(yaogui_network_policy_t* policy) {
  if (!policy) return false;
  if (policy->consecutive_wifi_failures < UINT8_MAX) {
    policy->consecutive_wifi_failures++;
  }
  return policy->consecutive_wifi_failures >= YAOGUI_WIFI_FAILURE_LIMIT;
}

bool yaogui_network_policy_on_no_internet(yaogui_network_policy_t* policy,
                                          uint32_t now_ms) {
  if (!policy) return false;
  if (!policy->no_internet_timer_active) {
    policy->no_internet_since_ms = now_ms;
    policy->no_internet_timer_active = true;
    return false;
  }
  return (uint32_t)(now_ms - policy->no_internet_since_ms) >=
         YAOGUI_NO_INTERNET_GRACE_MS;
}

bool yaogui_format_ap_ssid(char* output,
                           size_t output_size,
                           const uint8_t mac[6]) {
  if (!output || !mac || output_size == 0) return false;
  const int written = snprintf(
      output, output_size, "CyberYAO-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return written > 0 && (size_t)written < output_size;
}

static int hex_value(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

static bool form_decode(const char* source,
                        size_t source_length,
                        char* output,
                        size_t output_size) {
  if (!source || !output || output_size == 0) return false;
  size_t written = 0;
  for (size_t i = 0; i < source_length; i++) {
    unsigned char value = (unsigned char)source[i];
    if (value == '+') {
      value = ' ';
    } else if (value == '%') {
      if (i + 2U >= source_length) return false;
      const int high = hex_value(source[i + 1U]);
      const int low = hex_value(source[i + 2U]);
      if (high < 0 || low < 0) return false;
      value = (unsigned char)((high << 4) | low);
      i += 2U;
    }
    if (value == 0 || value < 0x20U || value == 0x7FU ||
        written + 1U >= output_size) {
      return false;
    }
    output[written++] = (char)value;
  }
  output[written] = '\0';
  return true;
}

bool yaogui_form_value(const char* body,
                       const char* key,
                       char* output,
                       size_t output_size) {
  if (!body || !key) return false;
  const size_t key_length = strlen(key);
  const char* field = body;
  while (*field) {
    const char* end = strchr(field, '&');
    if (!end) end = field + strlen(field);
    const char* equals = memchr(field, '=', (size_t)(end - field));
    if (equals && (size_t)(equals - field) == key_length &&
        memcmp(field, key, key_length) == 0) {
      return form_decode(
          equals + 1, (size_t)(end - equals - 1), output, output_size);
    }
    field = *end ? end + 1 : end;
  }
  return false;
}
