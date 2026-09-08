#include "yaogui_standby.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_pixel.h"
#include "yaogui_calendar.h"
#include "yaogui_standby_images.h"

LV_FONT_DECLARE(yaogui_font_14)
LV_FONT_DECLARE(yaogui_classic_14)
LV_FONT_DECLARE(yaogui_clock_28)
LV_FONT_DECLARE(yaogui_standby_display_12)
LV_FONT_DECLARE(yaogui_standby_calendar_10)
LV_FONT_DECLARE(yaogui_standby_pixel_10)

#define SCREEN_W 240
#define SCREEN_H 320
#define INSTRUMENT_X 16
#define INSTRUMENT_Y 62
#define INSTRUMENT_W 208
#define INSTRUMENT_H 150

/* 日晷逻辑坐标，与 sundial-pixi.js 完全相同。 */
#define SUNDIAL_W 167
#define SUNDIAL_H 184
#define SUNDIAL_SCALE (184.0f / 132.0f)
#define SUNDIAL_CX (60.0f * SUNDIAL_SCALE)
#define SUNDIAL_CY (48.0f * SUNDIAL_SCALE)
#define SUNDIAL_RX (52.0f * SUNDIAL_SCALE)
#define SUNDIAL_RY (29.0f * SUNDIAL_SCALE)
#define SUNDIAL_TILT (SUNDIAL_RY / SUNDIAL_RX)
#define SUNDIAL_SHADOW_RADIUS (41.0f * SUNDIAL_SCALE)

/* 刻漏逻辑画布 110×214 统一缩至 77×150；所有值均由同一比例取整。 */
#define CLEP_W 77
#define CLEP_H 150
#define CLEP_CX 39
#define CLEP_ARROW_W 11
#define CLEP_ARROW_H 84
#define CLEP_MOUTH_Y 111
#define CLEP_ARROW_RISE 28
#define CLEP_MOUTH_CLIP 114
#define CLEP_DROP_W 7
#define CLEP_DROP_H 10
#define CLEP_DROP_TIP 6
#define CLEP_FALL_END 0.86f

typedef struct {
  int from_y;
  int sink_y;
  int radius_y;
} clep_gap_t;

static const clep_gap_t CLEP_GAPS[3] = {
    {27, 34, 2},
    {56, 63, 2},
    {83, 112, 4},
};

typedef struct {
  lv_obj_t* viewport;
  lv_obj_t* image;
} clep_drop_t;

struct yaogui_standby {
  lv_obj_t* root;
  lv_obj_t* date;
  lv_obj_t* battery_body;
  lv_obj_t* battery_fill;
  lv_obj_t* battery_terminal;
  lv_obj_t* battery_percent;
  lv_obj_t* time;
  lv_obj_t* period;
  lv_obj_t* sundial;
  lv_obj_t* sundial_shadow;
  lv_obj_t* clepsydra;
  lv_obj_t* arrow_viewport;
  lv_obj_t* arrow;
  clep_drop_t drops[3];
  lv_obj_t* calendar_rule;
  lv_obj_t* lunar;
  lv_obj_t* ganzhi;
  lv_obj_t* yi_mark;
  lv_obj_t* yi_text;
  lv_obj_t* ji_mark;
  lv_obj_t* ji_text;
  lv_obj_t* footer_rule;
  lv_obj_t* footer_slots;
  lv_obj_t* footer;
  float shown_angle;
  float arrow_shown_y;
  uint32_t last_ms;
  bool night;
};

static lv_obj_t* plain_object(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* object = lv_obj_create(parent);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_size(object, w, h);
  lv_obj_set_style_pad_all(object, 0, 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_radius(object, 0, 0);
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
  return object;
}

static lv_obj_t* image_at(lv_obj_t* parent,
                          const lv_image_dsc_t* source,
                          int x,
                          int y) {
  lv_obj_t* image = lv_image_create(parent);
  lv_image_set_src(image, source);
  lv_obj_set_pos(image, x, y);
  return image;
}

static void set_hidden(lv_obj_t* object, bool hidden) {
  if (hidden)
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
}

static void draw_triangle(lv_layer_t* layer,
                          lv_point_precise_t a,
                          lv_point_precise_t b,
                          lv_point_precise_t c,
                          uint32_t color,
                          lv_opa_t opacity) {
  lv_draw_triangle_dsc_t descriptor;
  lv_draw_triangle_dsc_init(&descriptor);
  descriptor.p[0] = a;
  descriptor.p[1] = b;
  descriptor.p[2] = c;
  descriptor.color = lv_color_hex(color);
  descriptor.opa = opacity;
  lv_draw_triangle(layer, &descriptor);
}

static void draw_quad(lv_layer_t* layer,
                      const lv_point_precise_t points[4],
                      uint32_t color,
                      lv_opa_t opacity) {
  draw_triangle(layer, points[0], points[1], points[2], color, opacity);
  draw_triangle(layer, points[0], points[2], points[3], color, opacity);
}

static void draw_circle(lv_layer_t* layer,
                        int x,
                        int y,
                        int radius,
                        uint32_t color,
                        lv_opa_t opacity) {
  lv_draw_rect_dsc_t descriptor;
  lv_draw_rect_dsc_init(&descriptor);
  descriptor.bg_color = lv_color_hex(color);
  descriptor.bg_opa = opacity;
  descriptor.radius = LV_RADIUS_CIRCLE;
  const lv_area_t area = {
      .x1 = x - radius,
      .y1 = y - radius,
      .x2 = x + radius,
      .y2 = y + radius,
  };
  lv_draw_rect(layer, &descriptor, &area);
}

static lv_point_precise_t point_at(float x, float y, int ox, int oy) {
  return (lv_point_precise_t){
      .x = ox + (int32_t)lroundf(x),
      .y = oy + (int32_t)lroundf(y),
  };
}

static void sundial_shadow_draw(lv_event_t* event) {
  yaogui_standby_t* standby = lv_event_get_user_data(event);
  lv_layer_t* layer = lv_event_get_layer(event);
  if (!standby || !layer || standby->night) return;

  lv_area_t coords;
  lv_obj_get_coords(standby->sundial_shadow, &coords);
  const int ox = coords.x1;
  const int oy = coords.y1;
  const float angle = standby->shown_angle;
  const float tip_x =
      SUNDIAL_CX + SUNDIAL_SHADOW_RADIUS * cosf(angle);
  const float tip_y = SUNDIAL_CY +
                      SUNDIAL_SHADOW_RADIUS * SUNDIAL_TILT * sinf(angle);
  const float nx = -sinf(angle);
  const float ny = cosf(angle) * SUNDIAL_TILT;
  const float normal_length = hypotf(nx, ny);
  const float ux = nx / (normal_length > 0.0f ? normal_length : 1.0f);
  const float uy = ny / (normal_length > 0.0f ? normal_length : 1.0f);
  const float root_width = 3.4f * SUNDIAL_SCALE;
  const float tip_width = 1.2f * SUNDIAL_SCALE;
  const float dx = tip_x - SUNDIAL_CX;
  const float dy = tip_y - SUNDIAL_CY;
  const float direction_length = hypotf(dx, dy);
  const float shadow_x =
      dx / (direction_length > 0.0f ? direction_length : 1.0f) *
      1.4f * SUNDIAL_SCALE;
  const float shadow_y =
      dy / (direction_length > 0.0f ? direction_length : 1.0f) *
      1.4f * SUNDIAL_SCALE;

  const float px[4] = {
      SUNDIAL_CX + ux * root_width,
      SUNDIAL_CX - ux * root_width,
      tip_x - ux * tip_width,
      tip_x + ux * tip_width,
  };
  const float py[4] = {
      SUNDIAL_CY + uy * root_width,
      SUNDIAL_CY - uy * root_width,
      tip_y - uy * tip_width,
      tip_y + uy * tip_width,
  };
  lv_point_precise_t penumbra[4] = {
      point_at(px[0] + ux + shadow_x, py[0] + uy + shadow_y, ox, oy),
      point_at(px[1] - ux + shadow_x, py[1] - uy + shadow_y, ox, oy),
      point_at(px[2] - ux + shadow_x, py[2] - uy + shadow_y, ox, oy),
      point_at(px[3] + ux + shadow_x, py[3] + uy + shadow_y, ox, oy),
  };
  lv_point_precise_t body[4] = {
      point_at(px[0], py[0], ox, oy),
      point_at(px[1], py[1], ox, oy),
      point_at(px[2], py[2], ox, oy),
      point_at(px[3], py[3], ox, oy),
  };
  draw_quad(layer, penumbra, 0x0A0603, 77);
  draw_quad(layer, body, 0x0D0904, 209);
  draw_circle(layer,
              ox + (int)SUNDIAL_CX,
              oy + (int)SUNDIAL_CY,
              4,
              0x0D0904,
              217);
}

static void create_sundial(yaogui_standby_t* standby) {
  standby->sundial =
      plain_object(standby->root, INSTRUMENT_X, INSTRUMENT_Y,
                   INSTRUMENT_W, SUNDIAL_H);
  const int x = (INSTRUMENT_W - SUNDIAL_W) / 2;
  const int y = 0;
  lv_obj_t* scene =
      plain_object(standby->sundial, x, y, SUNDIAL_W, SUNDIAL_H);
  image_at(scene, &yaogui_sundial_base, 0, 0);
  image_at(scene, &yaogui_sundial_face, 0, 0);
  standby->sundial_shadow =
      plain_object(scene, 0, 0, SUNDIAL_W, SUNDIAL_H);
  lv_obj_add_event_cb(standby->sundial_shadow,
                      sundial_shadow_draw,
                      LV_EVENT_DRAW_MAIN,
                      standby);
  image_at(scene, &yaogui_sundial_gnomon, 0, 0);
}

static void create_clepsydra(yaogui_standby_t* standby) {
  standby->clepsydra =
      plain_object(standby->root, INSTRUMENT_X, INSTRUMENT_Y,
                   INSTRUMENT_W, INSTRUMENT_H);
  lv_obj_set_style_bg_color(standby->clepsydra, lv_color_hex(0x20242A), 0);
  lv_obj_set_style_bg_opa(standby->clepsydra, 26, 0);
  const int scene_x = (INSTRUMENT_W - CLEP_W) / 2;
  lv_obj_t* scene =
      plain_object(standby->clepsydra, scene_x, 0, CLEP_W, CLEP_H);

  /* Pixi 图层：四壶 -> 漏箭 -> 水滴。 */
  image_at(scene, &yaogui_clep_pot_ri, 24, 1);
  image_at(scene, &yaogui_clep_pot_yue, 22, 31);
  image_at(scene, &yaogui_clep_pot_xing, 22, 60);
  image_at(scene, &yaogui_clep_pot_shou, 20, 107);

  const int mask_top = CLEP_MOUTH_Y - CLEP_ARROW_RISE - 5;
  standby->arrow_viewport =
      plain_object(scene, CLEP_CX - 7, mask_top, 14,
                   CLEP_MOUTH_CLIP - mask_top);
  standby->arrow = image_at(standby->arrow_viewport,
                            &yaogui_clep_arrow,
                            7 - CLEP_ARROW_W / 2,
                            CLEP_MOUTH_Y - mask_top);
  standby->arrow_shown_y = CLEP_MOUTH_Y;

  for (size_t i = 0; i < 3; i++) {
    const clep_gap_t* gap = &CLEP_GAPS[i];
    const int lip_y = gap->sink_y;
    standby->drops[i].viewport =
        plain_object(scene, CLEP_CX - 7, gap->from_y, 14,
                     lip_y - gap->from_y);
    standby->drops[i].image =
        image_at(standby->drops[i].viewport,
                 &yaogui_clep_drop_0,
                 7 - 3,
                 -CLEP_DROP_TIP);
  }
}

static void create_footer_lines(lv_obj_t* parent) {
  for (int i = 0; i < 6; i++) {
    lv_obj_t* line = plain_object(parent, 0, 15 - i * 3, 27, 2);
    lv_obj_set_style_border_width(line, 1, 0);
    lv_obj_set_style_border_color(line, lv_color_hex(0x8B7F6D), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_TRANSP, 0);
  }
}

yaogui_standby_t* yaogui_standby_create(lv_obj_t* parent) {
  yaogui_standby_t* standby = calloc(1, sizeof(*standby));
  if (!standby) return NULL;
  standby->root = plain_object(parent, 0, 0, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(standby->root, lv_color_hex(0xE8DCC5), 0);
  lv_obj_set_style_bg_opa(standby->root, LV_OPA_COVER, 0);

  standby->date =
      ui_pixel_label(standby->root, "", &yaogui_font_14, 0x211812);
  lv_obj_set_pos(standby->date, 16, 13);
  lv_obj_set_size(standby->date, 155, 16);
  standby->battery_body = plain_object(standby->root, 175, 15, 19, 9);
  lv_obj_set_style_border_width(standby->battery_body, 1, 0);
  lv_obj_set_style_border_color(
      standby->battery_body, lv_color_hex(0x211812), 0);
  standby->battery_fill =
      plain_object(standby->battery_body, 1, 1, 13, 5);
  lv_obj_set_style_bg_color(
      standby->battery_fill, lv_color_hex(0x211812), 0);
  lv_obj_set_style_bg_opa(standby->battery_fill, LV_OPA_COVER, 0);
  standby->battery_terminal =
      plain_object(standby->root, 194, 18, 2, 3);
  lv_obj_set_style_bg_color(
      standby->battery_terminal, lv_color_hex(0x211812), 0);
  lv_obj_set_style_bg_opa(standby->battery_terminal, LV_OPA_COVER, 0);
  standby->battery_percent =
      ui_pixel_label(standby->root, "", &yaogui_standby_pixel_10, 0x211812);
  lv_obj_set_pos(standby->battery_percent, 198, 13);
  lv_obj_set_size(standby->battery_percent, 26, 13);
  lv_obj_set_style_text_align(
      standby->battery_percent, LV_TEXT_ALIGN_RIGHT, 0);

  lv_obj_t* top_rule = plain_object(standby->root, 16, 32, 208, 1);
  lv_obj_set_style_bg_color(top_rule, lv_color_hex(0x695A4D), 0);
  lv_obj_set_style_bg_opa(top_rule, 60, 0);
  standby->time =
      ui_pixel_label(standby->root, "", &yaogui_clock_28, 0x211812);
  lv_obj_set_pos(standby->time, 16, 34);
  lv_obj_set_size(standby->time, 100, 28);
  standby->period =
      ui_pixel_label(standby->root, "", &yaogui_standby_pixel_10, 0x695A4D);
  lv_obj_set_pos(standby->period, 105, 44);
  lv_obj_set_size(standby->period, 119, 13);
  lv_obj_set_style_text_align(standby->period, LV_TEXT_ALIGN_RIGHT, 0);

  create_sundial(standby);
  create_clepsydra(standby);

  standby->calendar_rule =
      plain_object(standby->root, 16, 216, 208, 1);
  lv_obj_set_style_bg_color(
      standby->calendar_rule, lv_color_hex(0x695A4D), 0);
  lv_obj_set_style_bg_opa(standby->calendar_rule, 52, 0);
  standby->lunar =
      ui_pixel_label(standby->root, "农历丙午年七月廿五",
                     &yaogui_standby_display_12, 0x211812);
  lv_obj_set_pos(standby->lunar, 16, 220);
  lv_obj_set_size(standby->lunar, 208, 16);
  lv_obj_set_style_text_align(standby->lunar, LV_TEXT_ALIGN_CENTER, 0);
  standby->ganzhi =
      ui_pixel_label(standby->root, "甲申月 · 癸未日 · 白露将至",
                     &yaogui_standby_calendar_10, 0x695A4D);
  lv_obj_set_pos(standby->ganzhi, 16, 237);
  lv_obj_set_size(standby->ganzhi, 208, 15);
  lv_obj_set_style_text_align(standby->ganzhi, LV_TEXT_ALIGN_CENTER, 0);
  standby->yi_mark =
      ui_pixel_label(standby->root, "宜", &yaogui_standby_pixel_10, 0xA73529);
  lv_obj_set_pos(standby->yi_mark, 16, 254);
  lv_obj_set_size(standby->yi_mark, 12, 12);
  standby->yi_text = ui_pixel_label(
      standby->root, "沐浴·静思·安床", &yaogui_standby_pixel_10, 0x211812);
  lv_obj_set_pos(standby->yi_text, 30, 254);
  lv_obj_set_size(standby->yi_text, 194, 12);
  standby->ji_mark =
      ui_pixel_label(standby->root, "忌", &yaogui_standby_pixel_10, 0xA73529);
  lv_obj_set_pos(standby->ji_mark, 16, 265);
  lv_obj_set_size(standby->ji_mark, 12, 12);
  standby->ji_text = ui_pixel_label(
      standby->root, "远行·动土·争讼", &yaogui_standby_pixel_10, 0x211812);
  lv_obj_set_pos(standby->ji_text, 30, 265);
  lv_obj_set_size(standby->ji_text, 194, 12);

  standby->footer_rule =
      plain_object(standby->root, 16, 295, 208, 1);
  lv_obj_set_style_bg_color(
      standby->footer_rule, lv_color_hex(0x695A4D), 0);
  lv_obj_set_style_bg_opa(standby->footer_rule, 52, 0);
  standby->footer_slots =
      plain_object(standby->root, 16, 303, 27, 18);
  create_footer_lines(standby->footer_slots);
  standby->footer =
      ui_pixel_label(standby->root, "六爻未启　确认键·启坛起卦",
                     &yaogui_standby_pixel_10, 0xA73529);
  lv_obj_set_pos(standby->footer, 49, 304);
  lv_obj_set_size(standby->footer, 175, 14);
  lv_obj_set_style_text_align(standby->footer, LV_TEXT_ALIGN_RIGHT, 0);

  standby->shown_angle = (float)(M_PI * 1.5);
  standby->arrow_shown_y = CLEP_MOUTH_Y;
  set_hidden(standby->root, true);
  return standby;
}

void yaogui_standby_set_visible(yaogui_standby_t* standby, bool visible) {
  if (!standby) return;
  set_hidden(standby->root, !visible);
}

static const char* period_text(int minute, bool night) {
  const int hour = minute / 60;
  if (!night) {
    if (hour < 8) return "卯正 · 晷影初落东刻";
    if (hour < 10) return "辰时 · 晷影渐西移";
    if (hour < 12) return "巳时 · 晷影近午";
    if (hour < 13) return "午正 · 晷影指南";
    if (hour < 15) return "未时 · 晷影过午西斜";
    if (hour < 17) return "申时 · 晷影长向东";
    return "酉时 · 晷影没于西刻";
  }
  if (hour >= 19 && hour < 21) return "戌时 · 初更";
  if (hour >= 21 && hour < 23) return "亥时 · 二更";
  if (hour >= 23 || hour < 1) return "子时 · 三更";
  if (hour < 3) return "丑时 · 四更";
  if (hour < 5) return "寅时 · 五更";
  if (hour < 6) return "卯时 · 将明";
  return "酉时 · 入夜";
}

static void set_compact_text(lv_obj_t* label, const char* text) {
  char compact[64];
  size_t used = 0;
  if (!label || !text) return;
  for (size_t index = 0; text[index] != '\0' && used + 1 < sizeof(compact);
       index++) {
    if (text[index] != ' ') compact[used++] = text[index];
  }
  compact[used] = '\0';
  lv_label_set_text(label, compact);
}

static float clamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

static void render_sundial(yaogui_standby_t* standby,
                           float progress,
                           float delta_frames) {
  const float target = (float)M_PI + clamp01(progress) * (float)M_PI;
  float difference = target - standby->shown_angle;
  while (difference > (float)M_PI) difference -= (float)(2.0 * M_PI);
  while (difference < (float)-M_PI) difference += (float)(2.0 * M_PI);
  const float amount = fminf(1.0f, 0.12f * delta_frames);
  standby->shown_angle += difference * amount;
  lv_obj_invalidate(standby->sundial_shadow);
}

static void render_clepsydra(yaogui_standby_t* standby,
                             uint32_t now_ms,
                             float progress,
                             float delta_frames) {
  const float target_y = CLEP_MOUTH_Y - clamp01(progress) * CLEP_ARROW_RISE;
  standby->arrow_shown_y +=
      (target_y - standby->arrow_shown_y) *
      fminf(1.0f, 0.12f * delta_frames);
  const int mask_top = CLEP_MOUTH_Y - CLEP_ARROW_RISE - 5;
  lv_obj_set_y(standby->arrow,
               (int)lroundf(standby->arrow_shown_y) - mask_top);

  const float base_phase = fmodf((float)now_ms / 1515.0f, 1.0f);
  for (size_t i = 0; i < 3; i++) {
    const clep_gap_t* gap = &CLEP_GAPS[i];
    float phase = base_phase + (float)i / 3.0f;
    if (phase >= 1.0f) phase -= 1.0f;
    const int lip_y = gap->sink_y;
    float tip_y;
    lv_opa_t opacity;
    const lv_image_dsc_t* frame;
    if (phase < CLEP_FALL_END) {
      const float k = phase / CLEP_FALL_END;
      tip_y = gap->from_y +
              (lip_y - gap->from_y) * k * k;
      opacity = phase < 0.06f
                    ? (lv_opa_t)lroundf(255.0f * phase / 0.06f)
                    : LV_OPA_COVER;
      frame = k < 0.5f ? &yaogui_clep_drop_0 : &yaogui_clep_drop_1;
    } else {
      const float k =
          (phase - CLEP_FALL_END) / (1.0f - CLEP_FALL_END);
      tip_y = lip_y + k * (CLEP_DROP_H + 3);
      opacity = (lv_opa_t)lroundf(255.0f * (1.0f - k * 0.7f));
      frame = &yaogui_clep_drop_1;
    }
    lv_image_set_src(standby->drops[i].image, frame);
    lv_obj_set_y(standby->drops[i].image,
                 (int)lroundf(tip_y) - CLEP_DROP_TIP - gap->from_y);
    lv_obj_set_style_opa(standby->drops[i].image, opacity, 0);
  }
}

static void apply_palette(yaogui_standby_t* standby, bool night) {
  const uint32_t foreground = night ? 0xCFC5AE : 0x211812;
  const uint32_t secondary = night ? 0x8F897B : 0x695A4D;
  lv_obj_set_style_bg_color(
      standby->root, lv_color_hex(night ? 0x191B21 : 0xE8DCC5), 0);
  lv_obj_set_style_text_color(standby->date, lv_color_hex(foreground), 0);
  lv_obj_set_style_border_color(
      standby->battery_body, lv_color_hex(foreground), 0);
  lv_obj_set_style_bg_color(
      standby->battery_fill, lv_color_hex(foreground), 0);
  lv_obj_set_style_bg_color(
      standby->battery_terminal, lv_color_hex(foreground), 0);
  lv_obj_set_style_text_color(
      standby->battery_percent, lv_color_hex(foreground), 0);
  lv_obj_set_style_text_color(standby->time, lv_color_hex(foreground), 0);
  lv_obj_set_style_text_color(standby->period, lv_color_hex(secondary), 0);
  lv_obj_set_style_text_color(standby->lunar, lv_color_hex(foreground), 0);
  lv_obj_set_style_text_color(standby->ganzhi, lv_color_hex(secondary), 0);
  lv_obj_set_style_text_color(standby->yi_text, lv_color_hex(foreground), 0);
  lv_obj_set_style_text_color(standby->ji_text, lv_color_hex(foreground), 0);
}

void yaogui_standby_render(yaogui_standby_t* standby,
                           uint32_t now_ms,
                           int battery_percent,
                           int minute_of_day,
                           const char* date_text,
                           int year,
                           int month,
                           int day,
                           bool time_valid,
                           bool worst_case) {
  if (!standby) return;
  if (minute_of_day < 0 || minute_of_day >= 24 * 60)
    minute_of_day = 12 * 60;
  const bool night = minute_of_day < 6 * 60 || minute_of_day >= 18 * 60;
  if (night != standby->night) {
    standby->night = night;
    apply_palette(standby, night);
  }
  set_hidden(standby->sundial, night);
  set_hidden(standby->clepsydra, !night);
  const int calendar_y = night ? 216 : 236;
  lv_obj_set_y(standby->calendar_rule, calendar_y);
  lv_obj_set_y(standby->lunar, calendar_y + 4);
  lv_obj_set_y(standby->ganzhi, calendar_y + 20);
  lv_obj_set_y(standby->yi_mark, calendar_y + 36);
  lv_obj_set_y(standby->yi_text, calendar_y + 36);
  lv_obj_set_y(standby->ji_mark, calendar_y + 47);
  lv_obj_set_y(standby->ji_text, calendar_y + 47);
  lv_obj_set_y(standby->footer_rule, calendar_y + 59);
  lv_obj_set_y(standby->footer_slots, calendar_y + 67);
  lv_obj_set_y(standby->footer, calendar_y + 68);

  lv_label_set_text(
      standby->date,
      time_valid && date_text && date_text[0] ? date_text : "等待校时");
  set_hidden(standby->battery_body, battery_percent < 0);
  set_hidden(standby->battery_terminal, battery_percent < 0);
  set_hidden(standby->battery_percent, battery_percent < 0);
  if (battery_percent >= 0) {
    int battery = battery_percent > 100 ? 100 : battery_percent;
    lv_obj_set_width(standby->battery_fill, battery * 16 / 100);
    lv_label_set_text_fmt(standby->battery_percent, "%d%%", battery);
  }
  if (time_valid) {
    lv_label_set_text_fmt(
        standby->time, "%02d:%02d", minute_of_day / 60, minute_of_day % 60);
    set_compact_text(standby->period, period_text(minute_of_day, night));
  } else {
    lv_label_set_text(standby->time, "--:--");
    lv_label_set_text(standby->period, "等待蓝牙校时");
  }

  yaogui_calendar_day_t calendar;
  set_hidden(standby->yi_mark, !time_valid);
  set_hidden(standby->ji_mark, !time_valid);
  if (worst_case) {
    lv_label_set_text(standby->lunar, "农历癸亥年闰十二月三十");
    lv_label_set_text(standby->ganzhi, "癸亥月  癸亥日  冬至");
    set_compact_text(standby->yi_text, "修饰垣墙 · 平治道涂");
    set_compact_text(standby->ji_text, "会亲友 · 进人口");
  } else if (time_valid &&
      yaogui_calendar_lookup(year, month, day, &calendar)) {
    lv_label_set_text(standby->lunar, calendar.lunar);
    lv_label_set_text(standby->ganzhi, calendar.ganzhi);
    set_compact_text(standby->yi_text, calendar.yi);
    set_compact_text(standby->ji_text, calendar.ji);
  } else {
    lv_label_set_text(standby->lunar, "农历等待校时");
    lv_label_set_text(standby->ganzhi, "");
    lv_label_set_text(standby->yi_text, "");
    lv_label_set_text(standby->ji_text, "");
  }

  float delta_frames = 1.0f;
  if (standby->last_ms != 0U) {
    delta_frames = (float)(now_ms - standby->last_ms) * 60.0f / 1000.0f;
    if (delta_frames > 6.0f) delta_frames = 6.0f;
  }
  standby->last_ms = now_ms;
  if (!night) {
    const float progress =
        (float)(minute_of_day - 6 * 60) / (12.0f * 60.0f);
    render_sundial(standby, progress, delta_frames);
  } else {
    const int elapsed =
        minute_of_day >= 18 * 60
            ? minute_of_day - 18 * 60
            : 6 * 60 + minute_of_day;
    render_clepsydra(
        standby, now_ms, (float)elapsed / (12.0f * 60.0f), delta_frames);
  }
}
