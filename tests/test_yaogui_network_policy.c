#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "yaogui_network_policy.h"

static void test_wifi_failures_require_a_streak(void) {
  yaogui_network_policy_t policy;
  yaogui_network_policy_init(&policy);
  assert(!yaogui_network_policy_on_wifi_failure(&policy));
  assert(!yaogui_network_policy_on_wifi_failure(&policy));
  yaogui_network_policy_on_dhcp(&policy, 100U);
  assert(!yaogui_network_policy_on_wifi_failure(&policy));
  assert(!yaogui_network_policy_on_wifi_failure(&policy));
  assert(yaogui_network_policy_on_wifi_failure(&policy));
  yaogui_network_policy_begin_wifi_attempt(&policy);
  assert(!yaogui_network_policy_on_wifi_failure(&policy));
}

static void test_no_internet_uses_a_grace_period(void) {
  yaogui_network_policy_t policy;
  yaogui_network_policy_init(&policy);
  assert(!yaogui_network_policy_on_no_internet(&policy, 1000U));
  assert(!yaogui_network_policy_on_no_internet(
      &policy, 1000U + YAOGUI_NO_INTERNET_GRACE_MS - 1U));
  assert(yaogui_network_policy_on_no_internet(
      &policy, 1000U + YAOGUI_NO_INTERNET_GRACE_MS));
  yaogui_network_policy_on_internet(&policy);
  assert(!yaogui_network_policy_on_no_internet(&policy, 999999U));
}

static void test_grace_period_handles_tick_wraparound(void) {
  yaogui_network_policy_t policy;
  yaogui_network_policy_init(&policy);
  const uint32_t start = UINT32_MAX - 1000U;
  assert(!yaogui_network_policy_on_no_internet(&policy, start));
  assert(!yaogui_network_policy_on_no_internet(
      &policy, start + YAOGUI_NO_INTERNET_GRACE_MS - 1U));
  assert(yaogui_network_policy_on_no_internet(
      &policy, start + YAOGUI_NO_INTERNET_GRACE_MS));
}

static void test_form_values_are_bounded_and_decoded(void) {
  char value[33];
  assert(yaogui_form_value(
      "ssid=Home+WiFi&password=a%2Bb", "ssid", value, sizeof(value)));
  assert(strcmp(value, "Home WiFi") == 0);
  assert(yaogui_form_value(
      "ssid=Home+WiFi&password=a%2Bb", "password", value, sizeof(value)));
  assert(strcmp(value, "a+b") == 0);
  assert(!yaogui_form_value("ssid=%00hidden", "ssid", value, sizeof(value)));
  assert(!yaogui_form_value("ssid=bad%2", "ssid", value, sizeof(value)));
  assert(!yaogui_form_value(
      "ssid=123456789012345678901234567890123", "ssid", value, sizeof(value)));
  assert(!yaogui_form_value("password=x", "missing", value, sizeof(value)));
}

static void test_ap_ssid_uses_stable_mac_suffix(void) {
  const uint8_t mac[6] = {0x02, 0x11, 0x22, 0xA1, 0xB2, 0xC3};
  char ssid[33];
  assert(yaogui_format_ap_ssid(ssid, sizeof(ssid), mac));
  assert(strcmp(ssid, "CyberYAO-A1B2C3") == 0);
  char too_small[15];
  assert(!yaogui_format_ap_ssid(too_small, sizeof(too_small), mac));
}

int main(void) {
  test_wifi_failures_require_a_streak();
  test_no_internet_uses_a_grace_period();
  test_grace_period_handles_tick_wraparound();
  test_form_values_are_bounded_and_decoded();
  test_ap_ssid_uses_stable_mac_suffix();
  puts("yaogui network policy tests: PASS");
  return 0;
}
