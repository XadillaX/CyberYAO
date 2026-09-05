#pragma once

#include "lvgl.h"
#include "yaogui_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yaogui_view yaogui_view_t;

typedef struct {
    const yaogui_model_t *model;
    uint32_t now_ms;
    int battery_percent;
} yaogui_view_state_t;

/* 创建完整的 240x320 摇龟界面；调用方负责加载返回视图的 screen。 */
lv_obj_t *yaogui_loading_screen_create(void);
yaogui_view_t *yaogui_view_create(void);
void yaogui_view_destroy(yaogui_view_t *view);
lv_obj_t *yaogui_view_screen(yaogui_view_t *view);

/* 固件和主机模拟器共同使用的唯一渲染入口。 */
void yaogui_view_render(yaogui_view_t *view,
                        const yaogui_view_state_t *state);

/* 详细阅读页按键滚动；已滚动返回 true，到达边界返回 false。 */
bool yaogui_view_reading_scroll(yaogui_view_t *view, int direction);

#ifdef __cplusplus
}
#endif
