// main/fap_screenshot.c -- FAP_SCREENSHOT_V1 串口截屏协议。
#include "fap_screenshot.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bsp_display.h"
#include "bsp_pins.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "src/core/lv_refr_private.h"
#include "src/display/lv_display_private.h"
#include "src/draw/lv_draw_private.h"

static const char* TAG = "fap_shot";

#define FAP_CMD "FAP_SCREENSHOT_V1"
#define FAP_CMD_LEN (sizeof(FAP_CMD) - 1)
#define FAP_LINE_MAX 24
#define FAP_TASK_STACK 8192
#define FAP_TASK_PRIO 3
#define FAP_TX_CHUNK 512
#define FAP_STRIP_LINES 20
#define FAP_STRIP_BYTES ((uint32_t)BSP_LCD_W * FAP_STRIP_LINES * 2)
#define FAP_FRAME_BYTES ((uint32_t)BSP_LCD_W * BSP_LCD_H * 2)

_Static_assert(BSP_LCD_H % FAP_STRIP_LINES == 0,
               "screen height must be a whole number of strips");

static lv_draw_buf_t s_strip_desc;
static uint8_t s_strip_buf[FAP_STRIP_BYTES] __attribute__((aligned(64)));

static bool write_all(const void* data, size_t len) {
  const uint8_t* next = data;
  while (len > 0) {
    size_t chunk = len > FAP_TX_CHUNK ? FAP_TX_CHUNK : len;
    int written = usb_serial_jtag_write_bytes(next, chunk, pdMS_TO_TICKS(2000));
    if (written <= 0) return false;
    next += written;
    len -= (size_t)written;
  }
  return true;
}

static void render_strip(lv_obj_t* screen, int32_t y) {
  lv_area_t area = {
      .x1 = 0,
      .y1 = y,
      .x2 = BSP_LCD_W - 1,
      .y2 = y + FAP_STRIP_LINES - 1,
  };
  lv_layer_t layer;
  lv_layer_init(&layer);
  layer.draw_buf = &s_strip_desc;
  layer.buf_area = area;
  layer.color_format = LV_COLOR_FORMAT_RGB565;
  layer._clip_area = area;
  layer.phy_clip_area = area;

  lv_draw_buf_clear(&s_strip_desc, NULL);
  lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_CREATED, &layer);

  lv_display_t* display = lv_obj_get_display(screen);
  lv_display_t* old_display = lv_refr_get_disp_refreshing();
  lv_layer_t* old_layer = display->layer_head;
  display->layer_head = &layer;
  lv_refr_set_disp_refreshing(display);

  lv_obj_redraw(&layer, screen);
  layer.all_tasks_added = true;
  while (layer.draw_task_head) {
    lv_draw_dispatch_wait_for_request();
    lv_draw_dispatch();
  }

  display->layer_head = old_layer;
  lv_refr_set_disp_refreshing(old_display);
  lv_draw_unit_send_event(NULL, LV_EVENT_SCREEN_LOAD_START, &layer);
  lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_DELETED, &layer);
}

static void dump_screen(void) {
  if (!bsp_lvgl_lock(2000)) {
    ESP_LOGE(TAG, "拿不到 LVGL 锁，放弃本次截屏");
    return;
  }

  lv_obj_t* screen = lv_screen_active();
  if (screen) lv_obj_update_layout(screen);
  lv_result_t init_result = lv_draw_buf_init(&s_strip_desc,
                                             BSP_LCD_W,
                                             FAP_STRIP_LINES,
                                             LV_COLOR_FORMAT_RGB565,
                                             BSP_LCD_W * 2,
                                             s_strip_buf,
                                             sizeof(s_strip_buf));
  if (!screen || init_result != LV_RESULT_OK ||
      lv_obj_get_width(screen) != BSP_LCD_W ||
      lv_obj_get_height(screen) != BSP_LCD_H) {
    bsp_lvgl_unlock();
    ESP_LOGE(TAG, "活动屏幕尺寸或条带缓冲不符合截屏约定");
    return;
  }

  char header[48];
  int header_len = snprintf(header,
                            sizeof(header),
                            "%s %d %d RGB565LE %" PRIu32 "\n",
                            FAP_CMD,
                            BSP_LCD_W,
                            BSP_LCD_H,
                            FAP_FRAME_BYTES);
  if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
    bsp_lvgl_unlock();
    ESP_LOGE(TAG, "截屏响应头生成失败");
    return;
  }

  esp_log_level_set("*", ESP_LOG_NONE);
  bool sent = write_all(header, (size_t)header_len);
  for (int32_t y = 0; sent && y < BSP_LCD_H; y += FAP_STRIP_LINES) {
    render_strip(screen, y);
    sent = write_all(s_strip_buf, sizeof(s_strip_buf));
  }
  esp_log_level_set("*", ESP_LOG_INFO);
  bsp_lvgl_unlock();

  if (sent) {
    ESP_LOGI(TAG,
             "已回传截屏 %dx%d(%" PRIu32 " 字节)",
             BSP_LCD_W,
             BSP_LCD_H,
             FAP_FRAME_BYTES);
  } else {
    ESP_LOGW(TAG, "截屏回传中断");
  }
}

static void fap_task(void* arg) {
  (void)arg;
  char line[FAP_LINE_MAX];
  size_t used = 0;
  uint8_t buf[16];

  for (;;) {
    if (!usb_serial_jtag_is_driver_installed()) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    int read = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(500));
    if (read < 0) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (read == 0) continue;

    for (int i = 0; i < read; i++) {
      char byte = (char)buf[i];
      if (byte == '\r' || byte == '\n') {
        used = 0;
        continue;
      }
      if (used < sizeof(line) - 1) {
        line[used++] = byte;
      } else {
        memmove(line, line + 1, sizeof(line) - 2);
        used = sizeof(line) - 2;
        line[used++] = byte;
      }
      if (used == FAP_CMD_LEN && memcmp(line, FAP_CMD, FAP_CMD_LEN) == 0) {
        ESP_LOGI(TAG, "收到截屏命令");
        dump_screen();
        used = 0;
      }
    }
  }
}

void fap_screenshot_start(void) {
  if (!usb_serial_jtag_is_driver_installed()) {
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 256,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "串口驱动安装失败(%d)，截屏协议不可用", err);
      return;
    }
  }

  usb_serial_jtag_vfs_use_driver();
  BaseType_t created = xTaskCreate(
      fap_task, "fap_shot", FAP_TASK_STACK, NULL, FAP_TASK_PRIO, NULL);
  if (created != pdPASS) {
    ESP_LOGE(TAG, "截屏任务创建失败(栈 %d 字节)", FAP_TASK_STACK);
    return;
  }
  ESP_LOGI(TAG, "截屏协议就绪：等待 FAP_SCREENSHOT_V1 命令");
}
