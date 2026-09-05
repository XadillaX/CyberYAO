/*******************************************************************************
 * Size: 16 px
 * Bpp: 4
 * Generated with lv_font_conv; source font paths are intentionally omitted.
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef YAOGUI_FONT_16
#define YAOGUI_FONT_16 1
#endif

#if YAOGUI_FONT_16

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+5366 "卦" */
    0x0, 0x0, 0x0, 0x0, 0x7, 0xe0, 0x0, 0x0,
    0x0, 0x75, 0x0, 0x7, 0xd0, 0x0, 0x0, 0x15,
    0xbb, 0x50, 0x7, 0xd0, 0x0, 0x0, 0x3d, 0xed,
    0x60, 0x8, 0xc0, 0x0, 0x0, 0x3, 0xc9, 0x0,
    0xa, 0xe0, 0x0, 0x0, 0x6f, 0xfc, 0x9c, 0x49,
    0xd9, 0x60, 0x2a, 0x97, 0x53, 0x21, 0xa, 0xd2,
    0xd5, 0x0, 0x0, 0x57, 0x0, 0x9, 0xe0, 0x13,
    0x0, 0x4, 0xbd, 0x80, 0xa, 0xe0, 0x0, 0x0,
    0x5, 0x8b, 0x0, 0x9, 0xe0, 0x0, 0x0, 0x0,
    0x7c, 0x3, 0xa, 0xe0, 0x0, 0x0, 0x26, 0xcd,
    0xb7, 0xb, 0xe0, 0x0, 0x8, 0xb6, 0x30, 0x0,
    0xc, 0xe0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xc,
    0xe0, 0x0,

    /* U+95EE "问" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xa, 0x70,
    0x0, 0x0, 0x16, 0x81, 0x7, 0xe0, 0x6, 0x79,
    0xff, 0xf2, 0x11, 0x60, 0x0, 0x0, 0x2e, 0xf3,
    0x85, 0x0, 0x0, 0x0, 0x7, 0xf3, 0x64, 0x14,
    0x46, 0xb1, 0x6, 0xf3, 0x74, 0x39, 0x12, 0xf0,
    0x6, 0xf3, 0x75, 0x28, 0x4, 0xf1, 0x7, 0xf3,
    0x75, 0x1e, 0x9a, 0xf0, 0x7, 0xf3, 0x85, 0x4,
    0x3, 0xe0, 0x7, 0xf2, 0x84, 0x0, 0x0, 0x0,
    0x7, 0xf2, 0x94, 0x0, 0x0, 0x1, 0x46, 0xf2,
    0x31, 0x0, 0x0, 0x0, 0xbe, 0xf2, 0x0, 0x0,
    0x0, 0x0, 0xb, 0xf2, 0x0, 0x0, 0x0, 0x0,
    0x1, 0x30
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 256, .box_w = 14, .box_h = 14, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 98, .adv_w = 256, .box_w = 12, .box_h = 15, .ofs_x = 2, .ofs_y = -3}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x4288
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 21350, .range_length = 17033, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 2, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t yaogui_font_16 = {
#else
lv_font_t yaogui_font_16 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 15,          /*The maximum line height required by the font*/
    .base_line = 3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if YAOGUI_FONT_16*/
