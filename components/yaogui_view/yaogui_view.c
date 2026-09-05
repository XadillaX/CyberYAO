#include "yaogui_view.h"

#include "ui_pixel.h"
#include "yaogui_shell_images.h"
#include "yaogui_table_image.h"

#include <stdio.h>
#include <stdlib.h>

#define COIN_SCALE 176U
#define CENTER_SHELL_SCALE 300U

LV_FONT_DECLARE(yaogui_font_14)
LV_FONT_DECLARE(yaogui_classic_14)
LV_FONT_DECLARE(yaogui_mifu_18)
struct yaogui_view {
    lv_obj_t *screen;
    lv_obj_t *table;
    lv_obj_t *bottom_panel;
    lv_obj_t *shells[YAOGUI_SHELL_COUNT];
    lv_obj_t *shadows[YAOGUI_SHELL_COUNT];
    lv_obj_t *center_shell;
    lv_obj_t *primary_solid[YAOGUI_LINE_COUNT];
    lv_obj_t *primary_left[YAOGUI_LINE_COUNT];
    lv_obj_t *primary_right[YAOGUI_LINE_COUNT];
    lv_obj_t *changed_solid[YAOGUI_LINE_COUNT];
    lv_obj_t *changed_left[YAOGUI_LINE_COUNT];
    lv_obj_t *changed_right[YAOGUI_LINE_COUNT];
    lv_obj_t *moving_mark[YAOGUI_LINE_COUNT];
    lv_obj_t *result_name;
    lv_obj_t *result_text;
    lv_obj_t *result_hint;
    lv_obj_t *status;
    lv_obj_t *reading_panel;
    lv_obj_t *reading_title;
    lv_obj_t *reading_badge;
    lv_obj_t *reading_viewport;
    lv_obj_t *reading_text;
    lv_obj_t *reading_footer;
    uint8_t rendered_reading_page;
    bool reading_rendered;
};

static void set_hidden(lv_obj_t *object, bool hidden)
{
    if (hidden) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
}

static const char *error_text(yaogui_error_t error)
{
    return error == YAOGUI_ERROR_RANDOM_SOURCE
        ? "真随机源启动失败\n请按确认键重试"
        : "内部错误\n请按确认键重试";
}

static void format_chinese_number(unsigned value, char *buffer, size_t size)
{
    static const char *const digits[] = {
        "零", "一", "二", "三", "四", "五", "六", "七", "八", "九",
    };
    if (!buffer || size == 0) return;
    if (value < 10U) {
        snprintf(buffer, size, "%s", digits[value]);
    } else if (value < 20U) {
        snprintf(buffer, size, value == 10U ? "十" : "十%s",
                 digits[value % 10U]);
    } else {
        snprintf(buffer, size, value % 10U == 0U ? "%s十" : "%s十%s",
                 digits[value / 10U], digits[value % 10U]);
    }
}

static lv_obj_t *create_block(lv_obj_t *parent, int x, int y, int width,
                              int height, uint32_t color)
{
    lv_obj_t *block = lv_obj_create(parent);
    lv_obj_remove_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(block, x, y);
    lv_obj_set_size(block, width, height);
    lv_obj_set_style_radius(block, 0, 0);
    lv_obj_set_style_border_width(block, 0, 0);
    lv_obj_set_style_bg_color(block, lv_color_hex(color), 0);
    lv_obj_set_style_pad_all(block, 0, 0);
    return block;
}

static void loading_seal_opacity(void *object, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)object, value, 0);
}

lv_obj_t *yaogui_loading_screen_create(void)
{
    lv_obj_t *screen = ui_pixel_screen_create("初始化");
    if (!screen) return NULL;

    lv_obj_t *outer = lv_obj_create(screen);
    lv_obj_remove_flag(outer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(outer, 12, 12);
    lv_obj_set_size(outer, 216, 296);
    lv_obj_set_style_bg_opa(outer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(outer, 2, 0);
    lv_obj_set_style_border_color(outer, lv_color_hex(0x352014), 0);
    lv_obj_set_style_radius(outer, 0, 0);

    lv_obj_t *inner = lv_obj_create(screen);
    lv_obj_remove_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(inner, 17, 17);
    lv_obj_set_size(inner, 206, 286);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(inner, 1, 0);
    lv_obj_set_style_border_color(inner, lv_color_hex(0x9A6B43), 0);
    lv_obj_set_style_radius(inner, 0, 0);

    lv_obj_t *title = ui_pixel_label(
        screen, "启坛", &yaogui_classic_14, 0x6D2F20);
    lv_obj_set_pos(title, 20, 54);
    lv_obj_set_width(title, 200);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *trigrams = ui_pixel_label(
        screen, "☰　☱　☲　☳\n☴　☵　☶　☷",
        &yaogui_font_14, 0x352014);
    lv_obj_set_pos(trigrams, 22, 92);
    lv_obj_set_width(trigrams, 196);
    lv_obj_set_style_text_align(trigrams, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(trigrams, 18, 0);

    lv_obj_t *seal = lv_obj_create(screen);
    lv_obj_remove_flag(seal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(seal, 101, 151);
    lv_obj_set_size(seal, 38, 38);
    lv_obj_set_style_bg_opa(seal, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(seal, 2, 0);
    lv_obj_set_style_border_color(seal, lv_color_hex(0xA93226), 0);
    lv_obj_set_style_radius(seal, 1, 0);
    lv_obj_t *seal_text = ui_pixel_label(
        seal, "卜", &yaogui_classic_14, 0xA93226);
    lv_obj_center(seal_text);

    lv_obj_t *status = ui_pixel_label(
        screen, "正在初始化", &yaogui_font_14, 0x352014);
    lv_obj_set_pos(status, 20, 212);
    lv_obj_set_width(status, 200);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *detail = ui_pixel_label(
        screen, "检点卦具·静候启坛", &yaogui_font_14, 0x6D2F20);
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

static lv_obj_t *create_shadow(lv_obj_t *parent, int x)
{
    lv_obj_t *shadow = lv_obj_create(parent);
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

static lv_obj_t *create_coin(lv_obj_t *parent, int x)
{
    lv_obj_t *shell = lv_image_create(parent);
    lv_obj_remove_flag(shell, LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(shell, &yaogui_coin_back);
    lv_obj_set_pos(shell, x, 72);
    lv_obj_set_style_transform_pivot_x(shell, YAOGUI_SHELL_WIDTH / 2, 0);
    lv_obj_set_style_transform_pivot_y(shell, YAOGUI_SHELL_WIDTH / 2, 0);
    return shell;
}

static lv_obj_t *create_center_shell(lv_obj_t *parent)
{
    lv_obj_t *shell = lv_image_create(parent);
    lv_obj_remove_flag(shell, LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(shell, &yaogui_shell_back);
    lv_obj_set_pos(shell, 81, 70);
    lv_obj_set_style_transform_pivot_x(shell, YAOGUI_SHELL_WIDTH / 2, 0);
    lv_obj_set_style_transform_pivot_y(shell, YAOGUI_SHELL_HEIGHT / 2, 0);
    lv_obj_set_style_transform_scale_x(shell, CENTER_SHELL_SCALE, 0);
    lv_obj_set_style_transform_scale_y(shell, CENTER_SHELL_SCALE, 0);
    return shell;
}

static void create_hexagram_lines(yaogui_view_t *view, lv_obj_t *parent)
{
    for (size_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
        int y = 70 - (int)line * 7;
        view->primary_solid[line] = create_block(parent, 7, y, 40, 3, UI_INK);
        view->primary_left[line] = create_block(parent, 7, y, 16, 3, UI_INK);
        view->primary_right[line] = create_block(parent, 31, y, 16, 3, UI_INK);
        view->moving_mark[line] =
            create_block(parent, 52, y, 3, 3, 0xA93226);
        view->changed_solid[line] =
            create_block(parent, 62, y, 40, 3, UI_INK);
        view->changed_left[line] =
            create_block(parent, 62, y, 16, 3, UI_INK);
        view->changed_right[line] =
            create_block(parent, 86, y, 16, 3, UI_INK);
    }
}

static void render_line(lv_obj_t *solid, lv_obj_t *left, lv_obj_t *right,
                        bool yang, bool visible)
{
    set_hidden(solid, !yang);
    set_hidden(left, yang);
    set_hidden(right, yang);
    lv_obj_set_style_opa(solid, visible ? LV_OPA_COVER : LV_OPA_20, 0);
    lv_obj_set_style_opa(left, visible ? LV_OPA_COVER : LV_OPA_20, 0);
    lv_obj_set_style_opa(right, visible ? LV_OPA_COVER : LV_OPA_20, 0);
}

static void render_hexagram_lines(yaogui_view_t *view,
                                   const yaogui_model_t *model)
{
    const bool complete = model->line_count == YAOGUI_LINE_COUNT;
    for (size_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
        const bool filled = line < model->line_count;
        const yaogui_line_t value =
            filled ? model->lines[line] : YAOGUI_YOUNG_YANG;
        render_line(view->primary_solid[line], view->primary_left[line],
                    view->primary_right[line],
                    filled && yaogui_line_is_yang(value), filled);
        set_hidden(view->moving_mark[line],
                   !filled || !yaogui_line_is_old(value));
        render_line(view->changed_solid[line], view->changed_left[line],
                    view->changed_right[line],
                    complete && yaogui_line_changed_is_yang(value), complete);
        if (!complete) {
            set_hidden(view->changed_solid[line], true);
            set_hidden(view->changed_left[line], true);
            set_hidden(view->changed_right[line], true);
        }
    }
}

static const char *reading_kind(yaogui_reading_page_type_t type)
{
    switch (type) {
    case YAOGUI_READING_PRIMARY_GUACI: return "本卦全文";
    case YAOGUI_READING_MOVING_LINE: return "本卦爻辞";
    case YAOGUI_READING_CHANGED_GUACI: return "之卦全文";
    case YAOGUI_READING_CHANGED_STATIC_LINE: return "之卦爻辞";
    case YAOGUI_READING_SPECIAL: return "用九用六";
    default: return "解读";
    }
}

static void create_reading_panel(yaogui_view_t *view)
{
    view->reading_panel = create_block(view->screen, 0, 0, 240, 320, 0xF7EEDB);
    view->reading_title = ui_pixel_label(
        view->reading_panel, "", &yaogui_classic_14, 0x352014);
    lv_obj_set_pos(view->reading_title, 12, 12);
    lv_obj_set_size(view->reading_title, 216, 24);
    lv_obj_set_style_text_align(view->reading_title, LV_TEXT_ALIGN_CENTER, 0);

    view->reading_badge = ui_pixel_label(
        view->reading_panel, "", &yaogui_font_14, 0xA93226);
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
    view->reading_text = ui_pixel_label(
        view->reading_viewport, "", &yaogui_font_14, UI_INK);
    lv_obj_set_pos(view->reading_text, 0, 0);
    lv_obj_set_width(view->reading_text, 208);
    lv_obj_set_height(view->reading_text, LV_SIZE_CONTENT);
    lv_label_set_long_mode(view->reading_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(view->reading_text, 4, 0);

    view->reading_footer = ui_pixel_label(
        view->reading_panel, "", &yaogui_font_14, 0x6D2F20);
    lv_obj_set_pos(view->reading_footer, 8, 278);
    lv_obj_set_size(view->reading_footer, 224, 30);
    lv_obj_set_style_text_align(view->reading_footer, LV_TEXT_ALIGN_CENTER, 0);
    set_hidden(view->reading_panel, true);
}

static void render_reading(yaogui_view_t *view,
                           const yaogui_reading_plan_t *plan)
{
    if (view->reading_rendered &&
        view->rendered_reading_page == plan->current) return;
    const yaogui_reading_page_t *page = &plan->pages[plan->current];
    char hexagram_number[16];
    char current_page[16];
    char total_pages[16];
    format_chinese_number(page->hexagram->number,
                          hexagram_number, sizeof(hexagram_number));
    format_chinese_number((unsigned)plan->current + 1U,
                          current_page, sizeof(current_page));
    format_chinese_number((unsigned)plan->count,
                          total_pages, sizeof(total_pages));
    lv_label_set_text_fmt(view->reading_title, "第%s卦·%s·%s",
                          hexagram_number,
                          page->hexagram->name, reading_kind(page->type));
    lv_label_set_text(view->reading_badge, page->is_primary ? "主读" : "");
    lv_label_set_text(view->reading_text, page->text);
    lv_label_set_text_fmt(view->reading_footer,
        "↑↓滚动·至底换卦\n第%s页·共%s页　OK返回",
        current_page, total_pages);
    lv_obj_update_layout(view->reading_viewport);
    lv_obj_scroll_to_y(view->reading_viewport, 0, LV_ANIM_OFF);
    view->rendered_reading_page = plan->current;
    view->reading_rendered = true;
}

yaogui_view_t *yaogui_view_create(void)
{
    yaogui_view_t *view = calloc(1, sizeof(*view));
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
    lv_obj_t *table_image = lv_image_create(view->table);
    lv_image_set_src(table_image, &yaogui_table_image);
    lv_obj_set_pos(table_image, 0, 0);

    for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++)
        view->shadows[i] = create_shadow(view->table, 8 + (int)i * 64);
    for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++)
        view->shells[i] = create_coin(view->table, 8 + (int)i * 64);
    view->center_shell = create_center_shell(view->table);

    lv_obj_t *result_panel =
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
    view->result_text =
        ui_pixel_label(result_panel, "", &yaogui_font_14, UI_INK);
    lv_obj_set_pos(view->result_text, 114, 31);
    lv_obj_set_size(view->result_text, 106, 34);
    lv_label_set_long_mode(view->result_text, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_line_space(view->result_text, 1, 0);
    view->result_hint =
        ui_pixel_label(result_panel, "", &yaogui_font_14, 0x6D2F20);
    lv_obj_set_pos(view->result_hint, 114, 70);
    lv_obj_set_size(view->result_hint, 106, 14);
    lv_obj_set_style_text_align(view->result_hint, LV_TEXT_ALIGN_CENTER, 0);
    view->status = ui_pixel_label(
        result_panel, "", &yaogui_font_14, UI_INK);
    lv_obj_set_pos(view->status, 61, 9);
    lv_obj_set_size(view->status, 159, 68);
    lv_obj_set_style_text_align(view->status, LV_TEXT_ALIGN_CENTER, 0);
    create_reading_panel(view);
    return view;
}

void yaogui_view_destroy(yaogui_view_t *view)
{
    if (!view) return;
    if (view->screen) lv_obj_delete(view->screen);
    free(view);
}

lv_obj_t *yaogui_view_screen(yaogui_view_t *view)
{
    return view ? view->screen : NULL;
}

void yaogui_view_render(yaogui_view_t *view,
                        const yaogui_view_state_t *state)
{
    if (!view || !state || !state->model) return;
    const yaogui_model_t *model = state->model;
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
        model->phase == YAOGUI_ROLLING &&
        roll_elapsed >= YAOGUI_SHELL_HIDE_MS;

    for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++) {
        yaogui_shell_motion_t motion;
        yaogui_model_motion(model, state->now_ms, i, &motion);
        /*
         * 三钱法约定：字面记 2（阴），背面记 3（阳）。
         * motion.belly 表示本枚取值为 1，即应显示无字背面。
         */
        lv_image_set_src(view->shells[i],
                         motion.belly ? &yaogui_coin_back
                                      : &yaogui_coin_front);
        lv_obj_set_pos(view->shells[i], motion.x, motion.y);
        lv_obj_set_style_transform_rotation(view->shells[i], motion.rotation, 0);
        lv_obj_set_style_transform_scale_x(
            view->shells[i], (motion.scale_x * COIN_SCALE) / 256U, 0);
        lv_obj_set_style_transform_scale_y(
            view->shells[i], (motion.scale_y * COIN_SCALE) / 256U, 0);
        lv_obj_set_pos(view->shadows[i], motion.x + 11, motion.y + 39);
        lv_obj_set_style_transform_scale_x(view->shadows[i],
            (motion.shadow_scale * COIN_SCALE) / 256U, 0);
        lv_obj_set_style_opa(view->shadows[i], motion.shadow_opa, 0);
        set_hidden(view->shadows[i],
                   model->phase != YAOGUI_RESULT && !coins_dancing);
    }

    bool show_center_shell =
        model->phase == YAOGUI_READY ||
        (model->phase == YAOGUI_ROLLING &&
         roll_elapsed < YAOGUI_SHELL_HIDE_MS);
    set_hidden(view->center_shell, !show_center_shell);
    if (show_center_shell) {
        int x = 81;
        int y = 70;
        int16_t rotation = 0;
        uint16_t scale = CENTER_SHELL_SCALE;
        if (model->phase == YAOGUI_ROLLING &&
            roll_elapsed < YAOGUI_SHELL_SHAKE_MS) {
            static const int8_t shake_x[] = { -3, 2, -2, 3, -1, 1 };
            size_t frame = (roll_elapsed / 45U) %
                           (sizeof(shake_x) / sizeof(shake_x[0]));
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
            scale = (uint16_t)(CENTER_SHELL_SCALE -
                               80U * progress / 256U);
        }
        lv_obj_set_pos(view->center_shell, x, y);
        lv_obj_set_style_transform_rotation(view->center_shell, rotation, 0);
        lv_obj_set_style_transform_scale_x(view->center_shell, scale, 0);
        lv_obj_set_style_transform_scale_y(view->center_shell, scale, 0);
    }

    render_hexagram_lines(view, model);
    const bool complete = model->line_count == YAOGUI_LINE_COUNT;
    set_hidden(view->result_name, !complete);
    set_hidden(view->result_text, !complete);
    set_hidden(view->result_hint, !complete);
    set_hidden(view->status, complete);
    if (complete) {
        const yaogui_hexagram_t *primary = NULL;
        const yaogui_hexagram_t *changed = NULL;
        if (yaogui_hexagram_from_lines(model->lines, false, &primary) &&
            yaogui_hexagram_from_lines(model->lines, true, &changed)) {
            if (primary == changed) {
                lv_label_set_text(view->result_name, primary->name);
            } else {
                lv_label_set_text_fmt(view->result_name, "%s之%s",
                                      primary->name, changed->name);
            }
            lv_label_set_text(view->result_text, primary->text);
            lv_label_set_text(view->result_hint, "OK 查看解读");
        }
    }

    switch (model->phase) {
    case YAOGUI_READY:
        {
            char line_number[16];
            format_chinese_number((unsigned)model->line_count + 1U,
                                  line_number, sizeof(line_number));
            lv_label_set_text_fmt(view->status, "第%s爻·共六爻\n按确认键起卦",
                                  line_number);
        }
        break;
    case YAOGUI_ROLLING:
        {
            char line_number[16];
            format_chinese_number((unsigned)model->line_count + 1U,
                                  line_number, sizeof(line_number));
            lv_label_set_text_fmt(view->status, "第%s爻·共六爻\n静候龟甲落定",
                                  line_number);
        }
        break;
    case YAOGUI_RESULT:
        if (!complete) {
            const yaogui_line_t line = model->lines[model->line_count - 1U];
            char line_number[16];
            format_chinese_number((unsigned)model->line_count,
                                  line_number, sizeof(line_number));
            lv_label_set_text_fmt(view->status, "第%s爻·%s\n确认下一爻",
                                  line_number,
                                  yaogui_line_name(line));
        }
        break;
    case YAOGUI_ERROR:
        lv_label_set_text(view->status, error_text(model->error));
        break;
    }
}

bool yaogui_view_reading_scroll(yaogui_view_t *view, int direction)
{
    if (!view || !view->reading_rendered || direction == 0) return false;
    lv_obj_update_layout(view->reading_viewport);
    if (direction > 0) {
        if (lv_obj_get_scroll_bottom(view->reading_viewport) <= 0) return false;
        lv_obj_scroll_by_bounded(
            view->reading_viewport, 0, -72, LV_ANIM_ON);
    } else {
        if (lv_obj_get_scroll_top(view->reading_viewport) <= 0) return false;
        lv_obj_scroll_by_bounded(
            view->reading_viewport, 0, 72, LV_ANIM_ON);
    }
    return true;
}
