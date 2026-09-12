#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "yaogui_calendar.h"

static void expect_term(int year,
                        int month,
                        int day,
                        const char* expected_suffix) {
  yaogui_calendar_day_t calendar;
  assert(yaogui_calendar_lookup(year, month, day, &calendar));
  const size_t actual_length = strlen(calendar.ganzhi);
  const size_t suffix_length = strlen(expected_suffix);
  assert(actual_length >= suffix_length);
  assert(strcmp(calendar.ganzhi + actual_length - suffix_length,
                expected_suffix) == 0);
}

int main(void) {
  expect_term(2026, 9, 7, "白露");
  expect_term(2026, 9, 12, "秋分前十一日");
  expect_term(2026, 9, 23, "秋分");
  expect_term(2026, 12, 31, "小寒前五日");

  yaogui_calendar_day_t calendar;
  assert(!yaogui_calendar_lookup(2023, 12, 31, &calendar));
  assert(!yaogui_calendar_lookup(2041, 1, 1, &calendar));
  puts("yaogui calendar tests passed");
  return 0;
}
