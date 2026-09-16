#pragma once

#include "lvgl.h"

typedef struct yaogui_standby yaogui_standby_t;

typedef enum {
  YAOGUI_WIFI_DISCONNECTED = 0,
  YAOGUI_WIFI_CONNECTING,
  YAOGUI_WIFI_LOCAL,
  YAOGUI_WIFI_ONLINE,
} yaogui_wifi_status_t;

yaogui_standby_t* yaogui_standby_create(lv_obj_t* parent);
void yaogui_standby_set_visible(yaogui_standby_t* standby, bool visible);
void yaogui_standby_render(yaogui_standby_t* standby,
                           uint32_t now_ms,
                           int battery_percent,
                           int minute_of_day,
                           const char* date_text,
                           int year,
                           int month,
                           int day,
                           bool time_valid,
                           bool worst_case,
                           yaogui_wifi_status_t wifi_status);
