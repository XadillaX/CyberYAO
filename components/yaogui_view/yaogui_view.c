#include "yaogui_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_pixel.h"
#include "yaogui_shell_images.h"
#include "yaogui_standby.h"
#include "yaogui_table_image.h"

#define COIN_FRAME_SIZE 40
#define SHELL_FRAME_SIZE 84
#define SOURCE_PIXEL_BYTES 4
#define INDEXED_PALETTE_COLORS 256
#define INDEXED_PALETTE_BYTES (INDEXED_PALETTE_COLORS * 4)
#define INDEXED_FRAME_BYTES(size) (INDEXED_PALETTE_BYTES + (size) * (size))

LV_FONT_DECLARE(yaogui_font_14)
LV_FONT_DECLARE(yaogui_classic_14)
LV_FONT_DECLARE(yaogui_mifu_18)

typedef struct {
  const lv_image_dsc_t* source;
  int16_t angle;
  uint16_t scale_x;
  uint16_t scale_y;
  bool valid;
} sprite_frame_state_t;

struct yaogui_view {
  lv_obj_t* screen;
  lv_obj_t* table;
  lv_obj_t* bottom_panel;
  lv_obj_t* shells[YAOGUI_SHELL_COUNT];
  lv_obj_t* shadows[YAOGUI_SHELL_COUNT];
  lv_obj_t* center_shell;
  lv_obj_t* primary_solid[YAOGUI_LINE_COUNT];
  lv_obj_t* primary_left[YAOGUI_LINE_COUNT];
  lv_obj_t* primary_right[YAOGUI_LINE_COUNT];
  lv_obj_t* changed_solid[YAOGUI_LINE_COUNT];
  lv_obj_t* changed_left[YAOGUI_LINE_COUNT];
  lv_obj_t* changed_right[YAOGUI_LINE_COUNT];
  lv_obj_t* moving_mark[YAOGUI_LINE_COUNT];
  lv_obj_t* result_name;
  lv_obj_t* result_text;
  lv_obj_t* result_hint;
  lv_obj_t* status;
  lv_obj_t* reading_panel;
  lv_obj_t* reading_title;
  lv_obj_t* reading_badge;
  lv_obj_t* reading_viewport;
  lv_obj_t* reading_text;
  lv_obj_t* reading_footer;
  uint8_t rendered_reading_page;
  bool reading_rendered;
  yaogui_standby_t* standby;
  lv_obj_t* time_sync_panel;
  lv_obj_t* time_sync_label;
  _Alignas(4) uint8_t
      coin_pixels[YAOGUI_SHELL_COUNT][INDEXED_FRAME_BYTES(COIN_FRAME_SIZE)];
  lv_image_dsc_t coin_frames[YAOGUI_SHELL_COUNT];
  sprite_frame_state_t coin_frame_states[YAOGUI_SHELL_COUNT];
  _Alignas(4) uint8_t shell_pixels[INDEXED_FRAME_BYTES(SHELL_FRAME_SIZE)];
  lv_image_dsc_t shell_frame;
  sprite_frame_state_t shell_frame_state;
};

static const int16_t ROTATION_COS_Q14[32] = {
    16384,  16069,  15137,  13623,  11585,  9102,   6270,   3196,
    0,      -3196,  -6270,  -9102,  -11585, -13623, -15137, -16069,
    -16384, -16069, -15137, -13623, -11585, -9102,  -6270,  -3196,
    0,      3196,   6270,   9102,   11585,  13623,  15137,  16069,
};

static const int16_t ROTATION_SIN_Q14[32] = {
    0,      3196,   6270,   9102,   11585,  13623,  15137,  16069,
    16384,  16069,  15137,  13623,  11585,  9102,   6270,   3196,
    0,      -3196,  -6270,  -9102,  -11585, -13623, -15137, -16069,
    -16384, -16069, -15137, -13623, -11585, -9102,  -6270,  -3196,
};

static void rotation_components(int16_t angle, int32_t* cosine, int32_t* sine) {
  int32_t normalized = angle % 3600;
  if (normalized < 0) normalized += 3600;
  int32_t signed_angle = normalized > 1800 ? normalized - 3600 : normalized;
  int32_t absolute = signed_angle < 0 ? -signed_angle : signed_angle;

  /* Web 龟壳在 ±2.4° 内细微摇动，单独保留这三个像素角度。 */
  if (absolute <= 30) {
    if (absolute >= 20) {
      *cosine = 16370;
      *sine = 686;
    } else if (absolute >= 8) {
      *cosine = 16378;
      *sine = 457;
    } else if (absolute > 0) {
      *cosine = 16382;
      *sine = 229;
    } else {
      *cosine = 16384;
      *sine = 0;
    }
    if (signed_angle < 0) *sine = -*sine;
    return;
  }

  uint32_t index = ((uint32_t)normalized * 32U + 1800U) / 3600U;
  index %= 32U;
  *cosine = ROTATION_COS_Q14[index];
  *sine = ROTATION_SIN_Q14[index];
}

static void initialize_frame(lv_image_dsc_t* frame,
                             uint8_t* pixels,
                             uint16_t width,
                             uint16_t height) {
  memset(pixels, 0, INDEXED_PALETTE_BYTES);
  for (uint16_t index = 1; index < INDEXED_PALETTE_COLORS; index++) {
    uint16_t color = index - 1U;
    pixels[index * 4U] = (uint8_t)((color & 0x03U) * 85U);
    pixels[index * 4U + 1U] = (uint8_t)(((color >> 2U) & 0x07U) * 255U / 7U);
    pixels[index * 4U + 2U] = (uint8_t)(((color >> 5U) & 0x07U) * 255U / 7U);
    pixels[index * 4U + 3U] = 255U;
  }
  *frame = (lv_image_dsc_t){
      .header.magic = LV_IMAGE_HEADER_MAGIC,
      .header.cf = LV_COLOR_FORMAT_I8,
      .header.flags = 0,
      .header.w = width,
      .header.h = height,
      .header.stride = width,
      .data_size = INDEXED_PALETTE_BYTES + (uint32_t)width * height,
      .data = pixels,
  };
}

static bool render_sprite_frame(const lv_image_dsc_t* source,
                                lv_image_dsc_t* frame,
                                sprite_frame_state_t* state,
                                int16_t angle,
                                uint16_t scale_x,
                                uint16_t scale_y) {
  if (!source || !frame || !state || scale_x == 0 || scale_y == 0) return false;
  if (state->valid && state->source == source && state->angle == angle &&
      state->scale_x == scale_x && state->scale_y == scale_y)
    return false;

  const int source_width = source->header.w;
  const int source_height = source->header.h;
  const int frame_width = frame->header.w;
  const int frame_height = frame->header.h;
  const bool source_indexed = source->header.cf == LV_COLOR_FORMAT_I8;
  const uint8_t* source_pixels =
      source->data + (source_indexed ? INDEXED_PALETTE_BYTES : 0);
  uint8_t* frame_pixels = (uint8_t*)frame->data + INDEXED_PALETTE_BYTES;
  int32_t cosine = 0;
  int32_t sine = 0;
  rotation_components(angle, &cosine, &sine);
  if (source_indexed)
    memcpy((uint8_t*)frame->data, source->data, INDEXED_PALETTE_BYTES);
  memset(frame_pixels, 0, (size_t)frame_width * frame_height);

  for (int y = 0; y < frame_height; y++) {
    const int dy = y - frame_height / 2;
    for (int x = 0; x < frame_width; x++) {
      const int dx = x - frame_width / 2;
      const int32_t rotated_x = cosine * dx + sine * dy;
      const int32_t rotated_y = -sine * dx + cosine * dy;
      const int source_x = source_width / 2 + (int)((rotated_x * 256) /
                                                    (16384 * (int32_t)scale_x));
      const int source_y =
          source_height / 2 +
          (int)((rotated_y * 256) / (16384 * (int32_t)scale_y));
      if (source_x < 0 || source_x >= source_width || source_y < 0 ||
          source_y >= source_height)
        continue;
      if (source_indexed) {
        frame_pixels[(size_t)y * frame_width + x] =
            source_pixels[(size_t)source_y * source->header.stride + source_x];
      } else {
        const size_t source_offset =
            ((size_t)source_y * source_width + source_x) * SOURCE_PIXEL_BYTES;
        if (source_pixels[source_offset + 3U] < 32U) continue;
        uint16_t color =
            (uint16_t)((source_pixels[source_offset + 2U] >> 5U) << 5U);
        color |= (uint16_t)((source_pixels[source_offset + 1U] >> 5U) << 2U);
        color |= source_pixels[source_offset] >> 6U;
        if (color == 255U) color = 254U;
        frame_pixels[(size_t)y * frame_width + x] = (uint8_t)(color + 1U);
      }
    }
  }

  *state = (sprite_frame_state_t){
      .source = source,
      .angle = angle,
      .scale_x = scale_x,
      .scale_y = scale_y,
      .valid = true,
  };
  return true;
}

static void set_hidden(lv_obj_t* object, bool hidden) {
  if (hidden)
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
}

static const char* error_text(yaogui_error_t error) {
  return error == YAOGUI_ERROR_RANDOM_SOURCE
             ? "真随机源启动失败\n请按确认键重试"
             : "内部错误\n请按确认键重试";
}

static void format_chinese_number(unsigned value, char* buffer, size_t size) {
  static const char* const digits[] = {
      "零",
      "一",
      "二",
      "三",
      "四",
      "五",
      "六",
      "七",
      "八",
      "九",
  };
  if (!buffer || size == 0) return;
  if (value < 10U) {
    snprintf(buffer, size, "%s", digits[value]);
  } else if (value < 20U) {
    snprintf(buffer, size, value == 10U ? "十" : "十%s", digits[value % 10U]);
  } else {
    snprintf(buffer,
             size,
             value % 10U == 0U ? "%s十" : "%s十%s",
             digits[value / 10U],
             digits[value % 10U]);
  }
}

static lv_obj_t* create_block(
    lv_obj_t* parent, int x, int y, int width, int height, uint32_t color) {
  lv_obj_t* block = lv_obj_create(parent);
  lv_obj_remove_flag(block, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(block, x, y);
  lv_obj_set_size(block, width, height);
  lv_obj_set_style_radius(block, 0, 0);
  lv_obj_set_style_border_width(block, 0, 0);
  lv_obj_set_style_bg_color(block, lv_color_hex(color), 0);
  lv_obj_set_style_pad_all(block, 0, 0);
  return block;
}

static void loading_seal_opacity(void* object, int32_t value) {
  lv_obj_set_style_opa((lv_obj_t*)object, value, 0);
}

lv_obj_t* yaogui_loading_screen_create(void) {
  lv_obj_t* screen = ui_pixel_screen_create("初始化");
  if (!screen) return NULL;

  lv_obj_t* outer = lv_obj_create(screen);
  lv_obj_remove_flag(outer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(outer, 12, 12);
  lv_obj_set_size(outer, 216, 296);
  lv_obj_set_style_bg_opa(outer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(outer, 2, 0);
  lv_obj_set_style_border_color(outer, lv_color_hex(0x352014), 0);
  lv_obj_set_style_radius(outer, 0, 0);

  lv_obj_t* inner = lv_obj_create(screen);
  lv_obj_remove_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(inner, 17, 17);
  lv_obj_set_size(inner, 206, 286);
  lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(inner, 1, 0);
  lv_obj_set_style_border_color(inner, lv_color_hex(0x9A6B43), 0);
  lv_obj_set_style_radius(inner, 0, 0);

  lv_obj_t* title =
      ui_pixel_label(screen, "启坛", &yaogui_classic_14, 0x6D2F20);
  lv_obj_set_pos(title, 20, 54);
  lv_obj_set_width(title, 200);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t* trigrams = ui_pixel_label(
      screen, "☰　☱　☲　☳\n☴　☵　☶　☷", &yaogui_font_14, 0x352014);
  lv_obj_set_pos(trigrams, 22, 92);
  lv_obj_set_width(trigrams, 196);
  lv_obj_set_style_text_align(trigrams, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(trigrams, 18, 0);

  lv_obj_t* seal = lv_obj_create(screen);
  lv_obj_remove_flag(seal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(seal, 101, 151);
  lv_obj_set_size(seal, 38, 38);
  lv_obj_set_style_bg_opa(seal, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(seal, 2, 0);
  lv_obj_set_style_border_color(seal, lv_color_hex(0xA93226), 0);
  lv_obj_set_style_radius(seal, 1, 0);
  lv_obj_t* seal_text =
      ui_pixel_label(seal, "卜", &yaogui_classic_14, 0xA93226);
  lv_obj_center(seal_text);

  lv_obj_t* status =
      ui_pixel_label(screen, "正在初始化", &yaogui_font_14, 0x352014);
  lv_obj_set_pos(status, 20, 212);
  lv_obj_set_width(status, 200);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t* detail =
      ui_pixel_label(screen, "检点卦具·静候启坛", &yaogui_font_14, 0x6D2F20);
  lv_obj_set_pos(detail, 20, 238);
  lv_obj_set_width(detail, 200);
  lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, seal);
  lv_anim_set_exec_cb(&animation, loading_seal_opacity);
  lv_anim_set_values(&animation, 110, 255);
  lv_anim_set_duration(&animation, 720);
  lv_anim_set_playback_duration(&animation, 720);
  lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&animation);
  return screen;
}

static lv_obj_t* create_shadow(lv_obj_t* parent, int x) {
  lv_obj_t* shadow = lv_obj_create(parent);
  lv_obj_remove_flag(shadow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(shadow, x + 6, 102);
  lv_obj_set_size(shadow, 40, 9);
  lv_obj_set_style_radius(shadow, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(shadow, 0, 0);
  lv_obj_set_style_bg_color(shadow, lv_color_hex(0x3E3328), 0);
  lv_obj_set_style_bg_opa(shadow, 72, 0);
  lv_obj_set_style_pad_all(shadow, 0, 0);
  lv_obj_set_style_transform_pivot_x(shadow, 20, 0);
  lv_obj_set_style_transform_pivot_y(shadow, 4, 0);
  return shadow;
}

static lv_obj_t* create_coin(lv_obj_t* parent,
                             int x,
                             const lv_image_dsc_t* frame) {
  lv_obj_t* shell = lv_image_create(parent);
  lv_obj_remove_flag(shell, LV_OBJ_FLAG_SCROLLABLE);
  lv_image_set_src(shell, frame);
  lv_obj_set_pos(shell, x + 6, 78);
  return shell;
}

static lv_obj_t* create_center_shell(lv_obj_t* parent,
                                     const lv_image_dsc_t* frame) {
  lv_obj_t* shell = lv_image_create(parent);
  lv_obj_remove_flag(shell, LV_OBJ_FLAG_SCROLLABLE);
  lv_image_set_src(shell, frame);
  lv_obj_set_pos(shell, 65, 60);
  return shell;
}

static void create_hexagram_lines(yaogui_view_t* view, lv_obj_t* parent) {
  for (size_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
    int y = 70 - (int)line * 7;
    view->primary_solid[line] = create_block(parent, 7, y, 40, 3, UI_INK);
    view->primary_left[line] = create_block(parent, 7, y, 16, 3, UI_INK);
    view->primary_right[line] = create_block(parent, 31, y, 16, 3, UI_INK);
    view->moving_mark[line] = create_block(parent, 52, y, 3, 3, 0xA93226);
    view->changed_solid[line] = create_block(parent, 62, y, 40, 3, UI_INK);
    view->changed_left[line] = create_block(parent, 62, y, 16, 3, UI_INK);
    view->changed_right[line] = create_block(parent, 86, y, 16, 3, UI_INK);
  }
}

static void render_line(
    lv_obj_t* solid, lv_obj_t* left, lv_obj_t* right, bool yang, bool visible) {
  set_hidden(solid, !yang);
  set_hidden(left, yang);
  set_hidden(right, yang);
  lv_obj_set_style_opa(solid, visible ? LV_OPA_COVER : LV_OPA_20, 0);
  lv_obj_set_style_opa(left, visible ? LV_OPA_COVER : LV_OPA_20, 0);
  lv_obj_set_style_opa(right, visible ? LV_OPA_COVER : LV_OPA_20, 0);
}

static void render_hexagram_lines(yaogui_view_t* view,
                                  const yaogui_model_t* model) {
  const bool complete = model->line_count == YAOGUI_LINE_COUNT;
  for (size_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
    const bool filled = line < model->line_count;
    const yaogui_line_t value = filled ? model->lines[line] : YAOGUI_YOUNG_YANG;
    render_line(view->primary_solid[line],
                view->primary_left[line],
                view->primary_right[line],
                filled && yaogui_line_is_yang(value),
                filled);
    set_hidden(view->moving_mark[line], !filled || !yaogui_line_is_old(value));
    render_line(view->changed_solid[line],
                view->changed_left[line],
                view->changed_right[line],
                complete && yaogui_line_changed_is_yang(value),
                complete);
    if (!complete) {
      set_hidden(view->changed_solid[line], true);
      set_hidden(view->changed_left[line], true);
      set_hidden(view->changed_right[line], true);
    }
  }
}

static const char* reading_kind(yaogui_reading_page_type_t type) {
  switch (type) {
    case YAOGUI_READING_PRIMARY_GUACI:
      return "本卦全文";
    case YAOGUI_READING_MOVING_LINE:
      return "本卦爻辞";
    case YAOGUI_READING_CHANGED_GUACI:
      return "之卦全文";
    case YAOGUI_READING_CHANGED_STATIC_LINE:
      return "之卦爻辞";
    case YAOGUI_READING_SPECIAL:
      return "用九用六";
    default:
      return "解读";
  }
}

static void create_reading_panel(yaogui_view_t* view) {
  view->reading_panel = create_block(view->screen, 0, 0, 240, 320, 0xF7EEDB);
  view->reading_title =
      ui_pixel_label(view->reading_panel, "", &yaogui_classic_14, 0x352014);
  lv_obj_set_pos(view->reading_title, 12, 12);
  lv_obj_set_size(view->reading_title, 216, 24);
  lv_obj_set_style_text_align(view->reading_title, LV_TEXT_ALIGN_CENTER, 0);

  view->reading_badge =
      ui_pixel_label(view->reading_panel, "", &yaogui_font_14, 0xA93226);
  lv_obj_set_pos(view->reading_badge, 12, 42);
  lv_obj_set_size(view->reading_badge, 216, 20);
  lv_obj_set_style_text_align(view->reading_badge, LV_TEXT_ALIGN_CENTER, 0);
  create_block(view->reading_panel, 18, 66, 204, 2, 0xA93226);

  view->reading_viewport = lv_obj_create(view->reading_panel);
  lv_obj_set_pos(view->reading_viewport, 10, 76);
  lv_obj_set_size(view->reading_viewport, 220, 190);
  lv_obj_set_style_bg_opa(view->reading_viewport, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(view->reading_viewport, 0, 0);
  lv_obj_set_style_pad_all(view->reading_viewport, 2, 0);
  lv_obj_set_scroll_dir(view->reading_viewport, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(view->reading_viewport, LV_SCROLLBAR_MODE_ACTIVE);
  view->reading_text =
      ui_pixel_label(view->reading_viewport, "", &yaogui_font_14, UI_INK);
  lv_obj_set_pos(view->reading_text, 0, 0);
  lv_obj_set_width(view->reading_text, 208);
  lv_obj_set_height(view->reading_text, LV_SIZE_CONTENT);
  lv_label_set_long_mode(view->reading_text, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(view->reading_text, 4, 0);

  view->reading_footer =
      ui_pixel_label(view->reading_panel, "", &yaogui_font_14, 0x6D2F20);
  lv_obj_set_pos(view->reading_footer, 8, 278);
  lv_obj_set_size(view->reading_footer, 224, 30);
  lv_obj_set_style_text_align(view->reading_footer, LV_TEXT_ALIGN_CENTER, 0);
  set_hidden(view->reading_panel, true);
}

static void render_reading(yaogui_view_t* view,
                           const yaogui_reading_plan_t* plan) {
  if (view->reading_rendered && view->rendered_reading_page == plan->current)
    return;
  const yaogui_reading_page_t* page = &plan->pages[plan->current];
  char hexagram_number[16];
  char current_page[16];
  char total_pages[16];
  format_chinese_number(
      page->hexagram->number, hexagram_number, sizeof(hexagram_number));
  format_chinese_number(
      (unsigned)plan->current + 1U, current_page, sizeof(current_page));
  format_chinese_number(
      (unsigned)plan->count, total_pages, sizeof(total_pages));
  lv_label_set_text_fmt(view->reading_title,
                        "第%s卦·%s·%s",
                        hexagram_number,
                        page->hexagram->name,
                        reading_kind(page->type));
  lv_label_set_text(view->reading_badge, page->is_primary ? "主读" : "");
  lv_label_set_text(view->reading_text, page->text);
  lv_label_set_text_fmt(view->reading_footer,
                        "↑↓滚动·至底换卦\n第%s页·共%s页　OK返回",
                        current_page,
                        total_pages);
  lv_obj_update_layout(view->reading_viewport);
  lv_obj_scroll_to_y(view->reading_viewport, 0, LV_ANIM_OFF);
  view->rendered_reading_page = plan->current;
  view->reading_rendered = true;
}

static void render_time_sync_indicator(yaogui_view_t* view,
                                       const yaogui_view_state_t* state) {
  if (state->time_sync_indicator == YAOGUI_TIME_SYNC_IDLE) {
    set_hidden(view->time_sync_panel, true);
    return;
  }
  char text[32];
  if (state->time_sync_indicator == YAOGUI_TIME_SYNC_WAITING) {
    static const char* const frames[] = {"|", "/", "-", "\\"};
    const char* frame = frames[(state->now_ms / 150U) % 4U];
    snprintf(text, sizeof(text), "蓝牙校时 %s", frame);
  } else if (state->time_sync_indicator == YAOGUI_TIME_SYNC_SUCCESS) {
    snprintf(text, sizeof(text), "校时完成");
  } else {
    snprintf(text, sizeof(text), "校时超时");
  }
  lv_label_set_text(view->time_sync_label, text);
  set_hidden(view->time_sync_panel, false);
}

yaogui_view_t* yaogui_view_create(void) {
  yaogui_view_t* view = calloc(1, sizeof(*view));
  if (!view) return NULL;

  view->screen = ui_pixel_screen_create("问卦");
  if (!view->screen) {
    free(view);
    return NULL;
  }
  view->table = lv_obj_create(view->screen);
  lv_obj_remove_flag(view->table, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(view->table, 12, 5);
  lv_obj_set_size(view->table, 214, 214);
  lv_obj_set_style_bg_opa(view->table, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(view->table, 0, 0);
  lv_obj_set_style_pad_all(view->table, 0, 0);
  lv_obj_set_style_radius(view->table, 0, 0);
  lv_obj_set_style_clip_corner(view->table, false, 0);
  lv_obj_t* table_image = lv_image_create(view->table);
  lv_image_set_src(table_image, &yaogui_table_image);
  lv_obj_set_pos(table_image, 0, 0);

  for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++) {
    initialize_frame(&view->coin_frames[i],
                     view->coin_pixels[i],
                     COIN_FRAME_SIZE,
                     COIN_FRAME_SIZE);
    (void)render_sprite_frame(&yaogui_coin_back,
                              &view->coin_frames[i],
                              &view->coin_frame_states[i],
                              0,
                              256,
                              256);
  }
  initialize_frame(&view->shell_frame,
                   view->shell_pixels,
                   SHELL_FRAME_SIZE,
                   SHELL_FRAME_SIZE);
  (void)render_sprite_frame(&yaogui_shell_back,
                            &view->shell_frame,
                            &view->shell_frame_state,
                            0,
                            256,
                            256);

  for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++)
    view->shadows[i] = create_shadow(view->table, 8 + (int)i * 64);
  for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++)
    view->shells[i] =
        create_coin(view->table, 8 + (int)i * 64, &view->coin_frames[i]);
  view->center_shell = create_center_shell(view->table, &view->shell_frame);

  lv_obj_t* result_panel =
      create_block(view->screen, 6, 226, 228, 88, 0xF2E8D1);
  view->bottom_panel = result_panel;
  lv_obj_set_style_border_width(result_panel, 2, 0);
  lv_obj_set_style_border_color(result_panel, lv_color_hex(0x352014), 0);
  create_hexagram_lines(view, result_panel);
  view->result_name =
      ui_pixel_label(result_panel, "", &yaogui_mifu_18, 0x6D2F20);
  lv_obj_set_pos(view->result_name, 8, 2);
  lv_obj_set_width(view->result_name, 212);
  lv_obj_set_style_text_align(view->result_name, LV_TEXT_ALIGN_CENTER, 0);
  view->result_text = ui_pixel_label(result_panel, "", &yaogui_font_14, UI_INK);
  lv_obj_set_pos(view->result_text, 114, 31);
  lv_obj_set_size(view->result_text, 106, 34);
  lv_label_set_long_mode(view->result_text, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_line_space(view->result_text, 1, 0);
  view->result_hint =
      ui_pixel_label(result_panel, "", &yaogui_font_14, 0x6D2F20);
  lv_obj_set_pos(view->result_hint, 114, 70);
  lv_obj_set_size(view->result_hint, 106, 14);
  lv_obj_set_style_text_align(view->result_hint, LV_TEXT_ALIGN_CENTER, 0);
  view->status = ui_pixel_label(result_panel, "", &yaogui_font_14, UI_INK);
  lv_obj_set_pos(view->status, 61, 9);
  lv_obj_set_size(view->status, 159, 68);
  lv_obj_set_style_text_align(view->status, LV_TEXT_ALIGN_CENTER, 0);
  create_reading_panel(view);
  view->standby = yaogui_standby_create(view->screen);
  if (!view->standby) {
    lv_obj_delete(view->screen);
    free(view);
    return NULL;
  }
  view->time_sync_panel =
      create_block(view->screen, 68, 147, 104, 26, 0xF7EEDB);
  lv_obj_set_style_border_width(view->time_sync_panel, 2, 0);
  lv_obj_set_style_border_color(
      view->time_sync_panel, lv_color_hex(0xA73529), 0);
  view->time_sync_label =
      ui_pixel_label(view->time_sync_panel, "", &yaogui_font_14, 0x352014);
  lv_obj_set_size(view->time_sync_label, 100, 18);
  lv_obj_center(view->time_sync_label);
  lv_obj_set_style_text_align(view->time_sync_label, LV_TEXT_ALIGN_CENTER, 0);
  set_hidden(view->time_sync_panel, true);
  return view;
}

void yaogui_view_destroy(yaogui_view_t* view) {
  if (!view) return;
  if (view->screen) lv_obj_delete(view->screen);
  free(view);
}

lv_obj_t* yaogui_view_screen(yaogui_view_t* view) {
  return view ? view->screen : NULL;
}

void yaogui_view_render(yaogui_view_t* view, const yaogui_view_state_t* state) {
  if (!view || !state || !state->model) return;
  yaogui_standby_set_visible(view->standby, state->standby);
  render_time_sync_indicator(view, state);
  if (state->standby) {
    yaogui_standby_render(view->standby,
                          state->now_ms,
                          state->battery_percent,
                          state->minute_of_day,
                          state->date_text,
                          state->year,
                          state->month,
                          state->day,
                          state->time_valid,
                          state->standby_worst_case);
    return;
  }
  const yaogui_model_t* model = state->model;
  set_hidden(view->table, model->reading.open);
  set_hidden(view->bottom_panel, model->reading.open);
  set_hidden(view->reading_panel, !model->reading.open);
  if (model->reading.open && model->reading.count > 0) {
    render_reading(view, &model->reading);
    return;
  }
  view->reading_rendered = false;
  const uint32_t roll_elapsed = state->now_ms - model->started_ms;
  const bool coins_dancing =
      model->phase == YAOGUI_ROLLING && roll_elapsed >= YAOGUI_SHELL_HIDE_MS;

  for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++) {
    yaogui_shell_motion_t motion;
    yaogui_model_motion(model, state->now_ms, i, &motion);
    /*
     * 三钱法约定：字面记 2（阴），背面记 3（阳）。
     * motion.belly 表示本枚取值为 1，即应显示无字背面。
     */
    const lv_image_dsc_t* coin_source =
        motion.belly ? &yaogui_coin_back : &yaogui_coin_front;
    if (render_sprite_frame(coin_source,
                            &view->coin_frames[i],
                            &view->coin_frame_states[i],
                            motion.rotation,
                            motion.scale_x,
                            motion.scale_y))
      lv_obj_invalidate(view->shells[i]);
    lv_obj_set_pos(view->shells[i], motion.x + 6, motion.y + 6);
    int shadow_width = (int)(40U * motion.shadow_scale * 176U / (256U * 256U));
    if (shadow_width < 4) shadow_width = 4;
    lv_obj_set_pos(view->shadows[i],
                   motion.x + YAOGUI_SHELL_WIDTH / 2 - shadow_width / 2,
                   motion.y + 39);
    lv_obj_set_width(view->shadows[i], shadow_width);
    lv_obj_set_style_opa(view->shadows[i], motion.shadow_opa, 0);
    set_hidden(view->shadows[i],
               model->phase != YAOGUI_RESULT && !coins_dancing);
  }

  bool show_center_shell =
      model->phase == YAOGUI_READY ||
      (model->phase == YAOGUI_ROLLING && roll_elapsed < YAOGUI_SHELL_HIDE_MS);
  set_hidden(view->center_shell, !show_center_shell);
  if (show_center_shell) {
    int x = 65;
    int y = 60;
    int16_t rotation = 0;
    uint16_t scale = 256;
    if (model->phase == YAOGUI_ROLLING &&
        roll_elapsed < YAOGUI_SHELL_SHAKE_MS) {
      static const int8_t shake_x[] = {-3, 2, -2, 3, -1, 1};
      size_t frame =
          (roll_elapsed / 45U) % (sizeof(shake_x) / sizeof(shake_x[0]));
      int offset = shake_x[frame];
      x += offset;
      rotation = (int16_t)(offset * 8);
    } else if (model->phase == YAOGUI_ROLLING) {
      uint32_t move_ms = YAOGUI_SHELL_HIDE_MS - YAOGUI_SHELL_SHAKE_MS;
      uint32_t progress =
          (roll_elapsed - YAOGUI_SHELL_SHAKE_MS) * 256U / move_ms;
      if (progress > 256U) progress = 256U;
      x += (int)(170U * progress / 256U);
      y -= (int)(100U * progress / 256U);
      rotation = (int16_t)(700U * progress / 256U);
      scale = (uint16_t)(256U - 68U * progress / 256U);
    }
    if (render_sprite_frame(&yaogui_shell_back,
                            &view->shell_frame,
                            &view->shell_frame_state,
                            rotation,
                            scale,
                            scale))
      lv_obj_invalidate(view->center_shell);
    lv_obj_set_pos(view->center_shell, x, y);
  }

  render_hexagram_lines(view, model);
  const bool complete = model->line_count == YAOGUI_LINE_COUNT;
  set_hidden(view->result_name, !complete);
  set_hidden(view->result_text, !complete);
  set_hidden(view->result_hint, !complete);
  set_hidden(view->status, complete);
  if (complete) {
    const yaogui_hexagram_t* primary = NULL;
    const yaogui_hexagram_t* changed = NULL;
    if (yaogui_hexagram_from_lines(model->lines, false, &primary) &&
        yaogui_hexagram_from_lines(model->lines, true, &changed)) {
      if (primary == changed) {
        lv_label_set_text(view->result_name, primary->name);
      } else {
        lv_label_set_text_fmt(
            view->result_name, "%s之%s", primary->name, changed->name);
      }
      lv_label_set_text(view->result_text, primary->text);
      lv_label_set_text(view->result_hint, "OK 查看解读");
    }
  }

  switch (model->phase) {
    case YAOGUI_READY: {
      char line_number[16];
      format_chinese_number(
          (unsigned)model->line_count + 1U, line_number, sizeof(line_number));
      lv_label_set_text_fmt(
          view->status, "第%s爻·共六爻\n按确认键起卦", line_number);
    } break;
    case YAOGUI_ROLLING: {
      char line_number[16];
      format_chinese_number(
          (unsigned)model->line_count + 1U, line_number, sizeof(line_number));
      lv_label_set_text_fmt(
          view->status, "第%s爻·共六爻\n静候龟甲落定", line_number);
    } break;
    case YAOGUI_RESULT:
      if (!complete) {
        const yaogui_line_t line = model->lines[model->line_count - 1U];
        char line_number[16];
        format_chinese_number(
            (unsigned)model->line_count, line_number, sizeof(line_number));
        lv_label_set_text_fmt(view->status,
                              "第%s爻·%s\n确认下一爻",
                              line_number,
                              yaogui_line_name(line));
      }
      break;
    case YAOGUI_ERROR:
      lv_label_set_text(view->status, error_text(model->error));
      break;
  }
}

bool yaogui_view_reading_scroll(yaogui_view_t* view, int direction) {
  if (!view || !view->reading_rendered || direction == 0) return false;
  lv_obj_update_layout(view->reading_viewport);
  if (direction > 0) {
    if (lv_obj_get_scroll_bottom(view->reading_viewport) <= 0) return false;
    lv_obj_scroll_by_bounded(view->reading_viewport, 0, -72, LV_ANIM_ON);
  } else {
    if (lv_obj_get_scroll_top(view->reading_viewport) <= 0) return false;
    lv_obj_scroll_by_bounded(view->reading_viewport, 0, 72, LV_ANIM_ON);
  }
  return true;
}
