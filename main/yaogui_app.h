#pragma once

#include "bsp_button.h"
#include "esp_err.h"

// 创建独立应用 UI、随机源工作任务和消息通道。只应从 app_main 调用一次。
esp_err_t yaogui_app_start(void);

// 按键组件任务回调；只尝试投递按键队列，不访问 LVGL 或执行其他工作。
void yaogui_app_key(bsp_btn_t btn, bsp_btn_ev_t ev, void* user);
