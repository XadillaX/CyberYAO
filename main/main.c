// main/main.c —— AI Passport 独立“龟壳”应用入口。
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "fap_screenshot.h"
#include "lvgl.h"
#include "yaogui_app.h"
#include "yaogui_time_sync.h"

static const char* TAG = "main";

void app_main(void) {
  ESP_LOGI(TAG, "AI Passport Yaogui 启动");
  bsp_i2c_init();

  // 显示是应用唯一输出；失败时只记录硬件事实，不启动后台随机源任务。
  if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
    ESP_LOGE(TAG,
             "显示/LVGL 初始化失败；检查 SPI 接线"
             "(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
             BSP_LCD_MOSI,
             BSP_LCD_SCLK,
             BSP_LCD_CS,
             BSP_LCD_DC,
             BSP_LCD_BL);
    return;
  }
  bsp_display_backlight(100);

  // 电量计是可选能力；应用会在读值失败时隐藏电量，而不是阻止占卜。
  esp_err_t battery_err = bsp_battery_init();
  if (battery_err != ESP_OK) {
    ESP_LOGW(TAG, "电量计不可用: %s", esp_err_to_name(battery_err));
  }

  esp_err_t err = yaogui_app_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "龟壳应用启动失败: %s", esp_err_to_name(err));
    return;
  }
  // UI 完整就绪后再监听，确保协议抓取的是当前应用页面。
  fap_screenshot_start();

  /*
   * C3 为单核：完整 UI 和截屏先完成固定内存分配，再短暂停止 LVGL
   * 定时器完成无线协议栈初始化，避免启动期全屏动画饿死 IDLE 任务。
   */
  if (bsp_lvgl_lock(1000)) {
    lv_timer_enable(false);
    bsp_lvgl_unlock();
  }
  err = yaogui_time_sync_start();
  if (bsp_lvgl_lock(1000)) {
    lv_timer_enable(true);
    bsp_lvgl_unlock();
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "时间同步服务启动失败: %s", esp_err_to_name(err));
  }

  err = bsp_button_init(yaogui_app_key, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(err));
  }
}
