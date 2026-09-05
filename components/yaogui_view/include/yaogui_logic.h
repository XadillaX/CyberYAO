#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define YAOGUI_SHELL_COUNT 3
#define YAOGUI_LINE_COUNT 6
#define YAOGUI_MIN_ROLL_MS 1500U
#define YAOGUI_FRAME_MS 90U
#define YAOGUI_SHELL_SHAKE_MS 600U
#define YAOGUI_SHELL_HIDE_MS 900U
#define YAOGUI_SHELL_STAGGER_MS 140U
#define YAOGUI_LANDING_MS 760U
#define YAOGUI_LANDING_TOTAL_MS \
    (YAOGUI_LANDING_MS + (YAOGUI_SHELL_COUNT - 1U) * YAOGUI_SHELL_STAGGER_MS)
#define YAOGUI_ARENA_WIDTH 196
#define YAOGUI_SHELL_WIDTH 52
#define YAOGUI_SHELL_HEIGHT 64
#define YAOGUI_MAX_READING_PAGES 2
#define YAOGUI_READING_TEXT_BYTES 1536
typedef enum {
    YAOGUI_READY = 0,
    YAOGUI_ROLLING,
    YAOGUI_RESULT,
    YAOGUI_ERROR,
} yaogui_phase_t;

typedef enum {
    YAOGUI_ERROR_NONE = 0,
    YAOGUI_ERROR_RANDOM_SOURCE,
    YAOGUI_ERROR_INTERNAL,
} yaogui_error_t;

typedef enum {
    YAOGUI_OLD_YIN = 6,
    YAOGUI_YOUNG_YANG = 7,
    YAOGUI_YOUNG_YIN = 8,
    YAOGUI_OLD_YANG = 9,
} yaogui_line_t;

typedef enum {
    YAOGUI_TRIGRAM_KUN = 0,
    YAOGUI_TRIGRAM_ZHEN,
    YAOGUI_TRIGRAM_KAN,
    YAOGUI_TRIGRAM_DUI,
    YAOGUI_TRIGRAM_GEN,
    YAOGUI_TRIGRAM_LI,
    YAOGUI_TRIGRAM_XUN,
    YAOGUI_TRIGRAM_QIAN,
} yaogui_trigram_t;

typedef enum {
    YAOGUI_SOURCE_NONE = 0,
    YAOGUI_SOURCE_ESP32_RF,
} yaogui_random_source_t;

typedef struct {
    uint8_t number;
    const char *name;
    const char *text;
    const char *lines[YAOGUI_LINE_COUNT];
    const char *special;
} yaogui_hexagram_t;

typedef enum {
    YAOGUI_READING_PRIMARY_GUACI = 0,
    YAOGUI_READING_MOVING_LINE,
    YAOGUI_READING_CHANGED_GUACI,
    YAOGUI_READING_CHANGED_STATIC_LINE,
    YAOGUI_READING_SPECIAL,
} yaogui_reading_page_type_t;

typedef struct {
    yaogui_reading_page_type_t type;
    const yaogui_hexagram_t *hexagram;
    char text[YAOGUI_READING_TEXT_BYTES];
    uint8_t line;
    bool is_primary;
} yaogui_reading_page_t;

typedef struct {
    yaogui_reading_page_t pages[YAOGUI_MAX_READING_PAGES];
    uint8_t count;
    uint8_t current;
    bool open;
} yaogui_reading_plan_t;

typedef enum {
    YAOGUI_KEY_UP = 0,
    YAOGUI_KEY_DOWN,
    YAOGUI_KEY_OK,
    YAOGUI_KEY_OK_LONG,
} yaogui_key_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t rotation;
    uint16_t scale_x;
    uint16_t scale_y;
    uint16_t shadow_scale;
    uint8_t shadow_opa;
    bool belly;
    bool landed;
} yaogui_shell_motion_t;

typedef struct {
    yaogui_phase_t phase;
    yaogui_error_t error;
    uint32_t started_ms;
    bool request_finished;
    bool request_succeeded;
    uint32_t request_finished_ms;
    yaogui_random_source_t pending_source;
    yaogui_random_source_t result_source;
    uint8_t pending[YAOGUI_SHELL_COUNT];
    uint8_t result[YAOGUI_SHELL_COUNT];
    yaogui_line_t lines[YAOGUI_LINE_COUNT];
    uint8_t line_count;
    uint32_t motion_seed;
    int16_t start_x[YAOGUI_SHELL_COUNT];
    int16_t velocity_x[YAOGUI_SHELL_COUNT];
    uint16_t hop_ms[YAOGUI_SHELL_COUNT];
    uint8_t height[YAOGUI_SHELL_COUNT];
    int16_t angle_start[YAOGUI_SHELL_COUNT];
    int16_t angular_velocity[YAOGUI_SHELL_COUNT];
    int16_t target_x[YAOGUI_SHELL_COUNT];
    int16_t target_y[YAOGUI_SHELL_COUNT];
    int16_t target_angle[YAOGUI_SHELL_COUNT];
    yaogui_reading_plan_t reading;
} yaogui_model_t;

extern const yaogui_hexagram_t YAOGUI_HEXAGRAMS[64];

void yaogui_model_init(yaogui_model_t *model);
bool yaogui_model_start(yaogui_model_t *model, uint32_t now_ms);
bool yaogui_model_start_seeded(yaogui_model_t *model, uint32_t now_ms,
                               uint32_t seed);
void yaogui_model_complete(yaogui_model_t *model,
                           const uint8_t values[YAOGUI_SHELL_COUNT],
                           yaogui_random_source_t source, uint32_t now_ms);
void yaogui_model_fail(yaogui_model_t *model, yaogui_error_t error,
                       uint32_t now_ms);
void yaogui_model_tick(yaogui_model_t *model, uint32_t now_ms);
uint8_t yaogui_model_frame(const yaogui_model_t *model, uint32_t now_ms,
                           size_t shell_index);
void yaogui_model_motion(const yaogui_model_t *model, uint32_t now_ms,
                         size_t shell_index, yaogui_shell_motion_t *motion);
bool yaogui_coins_to_line(const uint8_t values[YAOGUI_SHELL_COUNT],
                          yaogui_line_t *line);
bool yaogui_line_is_yang(yaogui_line_t line);
bool yaogui_line_is_old(yaogui_line_t line);
const char *yaogui_line_name(yaogui_line_t line);
const char *yaogui_random_source_name(yaogui_random_source_t source);
bool yaogui_line_changed_is_yang(yaogui_line_t line);
bool yaogui_hexagram_from_lines(
    const yaogui_line_t lines[YAOGUI_LINE_COUNT], bool changed,
    const yaogui_hexagram_t **hexagram);
bool yaogui_reading_build(
    const yaogui_line_t lines[YAOGUI_LINE_COUNT],
    yaogui_reading_plan_t *plan);
bool yaogui_model_key(yaogui_model_t *model, yaogui_key_t key,
                      uint32_t now_ms, uint32_t seed);
bool yaogui_shells_to_trigram(const uint8_t values[YAOGUI_SHELL_COUNT],
                              yaogui_trigram_t *trigram);
bool yaogui_trigram_line_is_yang(yaogui_trigram_t trigram, size_t line);
const char *yaogui_trigram_symbol(yaogui_trigram_t trigram);
const char *yaogui_trigram_name(yaogui_trigram_t trigram);
const char *yaogui_trigram_nature(yaogui_trigram_t trigram);
const char *yaogui_trigram_text(yaogui_trigram_t trigram);
