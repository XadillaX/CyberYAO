// components/bsp/include/bsp_power.h
// 供电状态查询。CW2017 电量计不提供充电位，板上充电 IC 的状态脚也未接 MCU，
// 因此以 ESP32-C3 USB Serial/JTAG 是否连到主机（SOF 包）近似“正在充电”。
// 注意：只接充电头或充电宝（无数据链路、无 SOF）不会被判定为已连接。
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 当 USB 连接到主机（能收到 SOF 包）时返回 true，近似“充电中”。
// 该连接监测由 ESP-IDF 在系统启动时自动注册，无需额外初始化。
bool bsp_power_is_charging(void);

#ifdef __cplusplus
}
#endif
