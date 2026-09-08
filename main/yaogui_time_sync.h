#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* 初始化按需启动的 CyberYAO-Time Wi-Fi 校时服务，不开启射频。 */
esp_err_t yaogui_time_sync_start(void);

// 异步开启 CyberYAO-Time 开放热点与本地 HTTP 校时页。
esp_err_t yaogui_time_sync_request(void);

/* 异步关闭校时页面、热点和 Wi-Fi 射频。 */
esp_err_t yaogui_time_sync_cancel(void);

/* 每次成功写入系统时间后递增，用于界面确认校时已经完成。 */
uint32_t yaogui_time_sync_generation(void);

/* 每次热点启动失败后递增，用于界面立即结束等待。 */
uint32_t yaogui_time_sync_error_generation(void);

/* Wi-Fi 热点射频已启动时，硬件随机源可直接使用，无需重复初始化。 */
bool yaogui_time_sync_radio_ready(void);
