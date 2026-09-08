#pragma once

#include <stdbool.h>

typedef struct {
  char lunar[48];
  char ganzhi[48];
  const char* yi;
  const char* ji;
} yaogui_calendar_day_t;

/*
 * 查询由 lunar-javascript 生成的逐日历法数据。
 * 当前固件表覆盖 2024-01-01 至 2040-12-31。
 */
bool yaogui_calendar_lookup(int year,
                            int month,
                            int day,
                            yaogui_calendar_day_t* result);
