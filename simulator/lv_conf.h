#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 32
#define LV_MEM_SIZE (2U * 1024U * 1024U)

#define LV_USE_OS LV_OS_NONE
#define LV_USE_SDL 1
#define LV_SDL_RENDER_MODE 0
#define LV_SDL_BUF_COUNT 2
#define LV_SDL_ACCELERATED 1
#define LV_SDL_DIRECT_EXIT 1

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_USE_SNAPSHOT 1

#endif
