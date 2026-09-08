#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* 启动 CyberYAO-Time BLE Current Time Service。 */
esp_err_t yaogui_time_sync_start(void);

/* 确保蓝牙校时广播处于可连接状态。 */
esp_err_t yaogui_time_sync_request(void);

/* 每次成功写入系统时间后递增，用于界面确认校时已经完成。 */
uint32_t yaogui_time_sync_generation(void);

/* 蓝牙射频已启动时，硬件随机源可直接使用，无需重复初始化。 */
bool yaogui_time_sync_radio_ready(void);
