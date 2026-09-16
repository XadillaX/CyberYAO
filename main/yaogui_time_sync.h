#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* 初始化持久联网、NTP 校时和按需恢复的 CyberYAO Wi-Fi 服务。 */
esp_err_t yaogui_time_sync_start(void);

/* 异步开启 CyberYAO 开放热点与本地配网页。 */
esp_err_t yaogui_time_sync_request(void);

/* 兼容旧调用；联网服务由后台状态机持续管理，不主动关闭射频。 */
esp_err_t yaogui_time_sync_cancel(void);

/* 每次成功联网并完成 NTP 校时后递增。 */
uint32_t yaogui_time_sync_generation(void);

/* 每次网络服务或配网热点启动失败后递增。 */
uint32_t yaogui_time_sync_error_generation(void);

/* 每次配网热点开放后递增，用于设备显示联网指引。 */
uint32_t yaogui_time_sync_portal_generation(void);

/* 尚无经过 DHCP 验证并持久化的网络凭据时返回 true。 */
bool yaogui_time_sync_required(void);

/* 网络服务持有 Wi-Fi 射频时，硬件随机源可直接使用。 */
bool yaogui_time_sync_radio_ready(void);

/* 待机页使用的连接状态；三者分别表示关联、互联网验证和重连阶段。 */
bool yaogui_time_sync_has_ip(void);
bool yaogui_time_sync_internet_ready(void);
bool yaogui_time_sync_connecting(void);

/* 返回由设备 Wi-Fi MAC 生成的稳定配网热点名。 */
const char* yaogui_time_sync_ap_ssid(void);

/* 保存最近一次完整卦象，供同一局域网内的手机页面读取。 */
void yaogui_time_sync_set_reading(const uint8_t lines[6],
                                  int64_t timestamp_seconds);

/* 新一轮起卦开始时清除旧快照，避免网页读到上一卦。 */
void yaogui_time_sync_clear_reading(void);

/* 取得当前局域网解卦页地址；未联网或尚无完整卦象时返回 false。 */
bool yaogui_time_sync_reading_url(char* buffer, size_t buffer_size);
