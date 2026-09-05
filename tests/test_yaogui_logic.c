#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "yaogui_logic.h"

static void test_minimum_roll_and_success(void)
{
    yaogui_model_t model;
    const uint8_t result[YAOGUI_SHELL_COUNT] = { 1, 0, 1 };
    yaogui_model_init(&model);
    assert(model.phase == YAOGUI_READY);
    assert(yaogui_model_start(&model, 1000));
    assert(!yaogui_model_start(&model, 1001));
    yaogui_model_complete(&model, result, YAOGUI_SOURCE_ESP32_RF, 1200);
    yaogui_model_tick(&model, 2499);
    assert(model.phase == YAOGUI_ROLLING);
    yaogui_model_tick(&model, 1000 + YAOGUI_MIN_ROLL_MS +
                              YAOGUI_LANDING_TOTAL_MS - 1);
    assert(model.phase == YAOGUI_ROLLING);
    yaogui_model_tick(&model, 1000 + YAOGUI_MIN_ROLL_MS +
                              YAOGUI_LANDING_TOTAL_MS);
    assert(model.phase == YAOGUI_RESULT);
    assert(memcmp(model.result, result, sizeof(result)) == 0);
    assert(model.result_source == YAOGUI_SOURCE_ESP32_RF);
}

static void test_failure_and_retry(void)
{
    yaogui_model_t model;
    yaogui_model_init(&model);
    assert(yaogui_model_start(&model, 0));
    yaogui_model_fail(&model, YAOGUI_ERROR_RANDOM_SOURCE, 100);
    yaogui_model_tick(&model, YAOGUI_MIN_ROLL_MS - 1);
    assert(model.phase == YAOGUI_ROLLING);
    yaogui_model_tick(&model, YAOGUI_MIN_ROLL_MS +
                              YAOGUI_LANDING_TOTAL_MS);
    assert(model.phase == YAOGUI_ERROR);
    assert(model.error == YAOGUI_ERROR_RANDOM_SOURCE);
    assert(yaogui_model_start(&model, 3000));
    assert(model.phase == YAOGUI_ROLLING);
    assert(model.error == YAOGUI_ERROR_NONE);
}

static void test_animation_is_not_a_result(void)
{
    yaogui_model_t model;
    yaogui_model_init(&model);
    assert(yaogui_model_start(&model, UINT32_MAX - 100));
    assert(yaogui_model_frame(&model, UINT32_MAX - 100, 0) == 0);
    assert(yaogui_model_frame(&model, 19, 0) == 1);
    yaogui_shell_motion_t first;
    yaogui_shell_motion_t second;
    yaogui_model_motion(&model, 79, 0, &first);
    yaogui_model_motion(&model, 79, 1, &second);
    assert(first.x == second.x && first.x == 81);
    assert(first.y == second.y && first.y == 76);
    assert(first.rotation != second.rotation);
    assert(first.scale_x == 256 && first.scale_y == 256);
    assert(first.shadow_opa == 0);
    yaogui_model_motion(&model, YAOGUI_SHELL_HIDE_MS + 300U, 0, &first);
    assert(first.scale_x >= 32 && first.scale_x <= 256);
    assert(first.x >= 4 && first.x <= 140);
    assert(first.scale_x >= 32 && first.scale_x <= 256);
    assert(first.scale_y >= 220 && first.scale_y <= 256);
    assert(first.shadow_opa >= 35 && first.shadow_opa <= 95);
    yaogui_model_tick(&model, 1499);
    assert(model.phase == YAOGUI_ROLLING);
}

static int absolute(int value)
{
    return value < 0 ? -value : value;
}

static void test_seeded_physics_and_non_overlapping_landing(void)
{
    yaogui_model_t first;
    yaogui_model_t second;
    const uint8_t result[YAOGUI_SHELL_COUNT] = { 1, 0, 1 };
    yaogui_model_init(&first);
    yaogui_model_init(&second);
    assert(yaogui_model_start_seeded(&first, 200, UINT32_C(0x12345678)));
    assert(yaogui_model_start_seeded(&second, 200, UINT32_C(0x12345678)));
    assert(memcmp(first.start_x, second.start_x, sizeof(first.start_x)) == 0);
    assert(memcmp(first.target_x, second.target_x, sizeof(first.target_x)) == 0);
    assert(first.start_x[0] != first.start_x[1] ||
           first.velocity_x[0] != first.velocity_x[1]);

    yaogui_shell_motion_t moving;
    yaogui_model_motion(&first, 4200, 0, &moving);
    assert(!moving.landed);
    yaogui_model_complete(&first, result, YAOGUI_SOURCE_ESP32_RF, 4200);
    uint32_t done = 4200 + YAOGUI_LANDING_TOTAL_MS;
    yaogui_model_tick(&first, done);
    assert(first.phase == YAOGUI_RESULT);

    yaogui_shell_motion_t landed[YAOGUI_SHELL_COUNT];
    for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++) {
        yaogui_model_motion(&first, done, i, &landed[i]);
        assert(landed[i].landed);
        assert(landed[i].belly == (result[i] != 0));
        assert(landed[i].rotation >= 0 && landed[i].rotation <= 3599);
    }
    for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++) {
        for (size_t j = i + 1; j < YAOGUI_SHELL_COUNT; j++) {
            const int dx = absolute(landed[i].x - landed[j].x);
            const int dy = absolute(landed[i].y - landed[j].y);
            assert(dx >= YAOGUI_SHELL_WIDTH || dy >= YAOGUI_SHELL_HEIGHT);
        }
    }
}

static void test_shell_to_trigram_mapping(void)
{
    static const struct {
        uint8_t shells[YAOGUI_SHELL_COUNT];
        yaogui_trigram_t trigram;
        const char *symbol;
        const char *name;
        const char *text;
    } cases[] = {
        { { 0, 0, 0 }, YAOGUI_TRIGRAM_KUN, "☷", "坤",
          "元亨，利牝马之贞。" },
        { { 1, 0, 0 }, YAOGUI_TRIGRAM_ZHEN, "☳", "震",
          "亨。震来虩虩，笑言哑哑。" },
        { { 0, 1, 0 }, YAOGUI_TRIGRAM_KAN, "☵", "坎",
          "习坎，有孚，维心亨，行有尚。" },
        { { 1, 1, 0 }, YAOGUI_TRIGRAM_DUI, "☱", "兑", "亨，利贞。" },
        { { 0, 0, 1 }, YAOGUI_TRIGRAM_GEN, "☶", "艮",
          "艮其背，不获其身；行其庭，不见其人。" },
        { { 1, 0, 1 }, YAOGUI_TRIGRAM_LI, "☲", "离",
          "利贞，亨；畜牝牛，吉。" },
        { { 0, 1, 1 }, YAOGUI_TRIGRAM_XUN, "☴", "巽",
          "小亨，利有攸往，利见大人。" },
        { { 1, 1, 1 }, YAOGUI_TRIGRAM_QIAN, "☰", "乾",
          "元亨，利贞。" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        yaogui_trigram_t trigram = YAOGUI_TRIGRAM_KUN;
        assert(yaogui_shells_to_trigram(cases[i].shells, &trigram));
        assert(trigram == cases[i].trigram);
        assert(strcmp(yaogui_trigram_symbol(trigram), cases[i].symbol) == 0);
        assert(strcmp(yaogui_trigram_name(trigram), cases[i].name) == 0);
        assert(strcmp(yaogui_trigram_text(trigram), cases[i].text) == 0);
        for (size_t line = 0; line < YAOGUI_SHELL_COUNT; line++) {
            assert(yaogui_trigram_line_is_yang(trigram, line) ==
                   (cases[i].shells[line] != 0));
        }
    }
    const uint8_t invalid[YAOGUI_SHELL_COUNT] = { 0, 2, 0 };
    yaogui_trigram_t trigram = YAOGUI_TRIGRAM_KUN;
    assert(!yaogui_shells_to_trigram(invalid, &trigram));
    assert(!yaogui_shells_to_trigram(NULL, &trigram));
    assert(!yaogui_shells_to_trigram(cases[0].shells, NULL));
}

static void test_traditional_three_coin_mapping(void)
{
    static const struct {
        uint8_t coins[YAOGUI_SHELL_COUNT];
        yaogui_line_t line;
        bool yang;
        bool old;
        const char *name;
    } cases[] = {
        { { 0, 0, 0 }, YAOGUI_OLD_YIN, false, true, "老阴" },
        { { 1, 0, 0 }, YAOGUI_YOUNG_YANG, true, false, "少阳" },
        { { 0, 1, 0 }, YAOGUI_YOUNG_YANG, true, false, "少阳" },
        { { 0, 0, 1 }, YAOGUI_YOUNG_YANG, true, false, "少阳" },
        { { 1, 1, 0 }, YAOGUI_YOUNG_YIN, false, false, "少阴" },
        { { 1, 0, 1 }, YAOGUI_YOUNG_YIN, false, false, "少阴" },
        { { 0, 1, 1 }, YAOGUI_YOUNG_YIN, false, false, "少阴" },
        { { 1, 1, 1 }, YAOGUI_OLD_YANG, true, true, "老阳" },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        yaogui_line_t line = 0;
        assert(yaogui_coins_to_line(cases[i].coins, &line));
        assert(line == cases[i].line);
        assert(yaogui_line_is_yang(line) == cases[i].yang);
        assert(yaogui_line_is_old(line) == cases[i].old);
        assert(strcmp(yaogui_line_name(line), cases[i].name) == 0);
    }

    const uint8_t invalid[YAOGUI_SHELL_COUNT] = { 0, 2, 1 };
    yaogui_line_t line = YAOGUI_OLD_YIN;
    assert(!yaogui_coins_to_line(invalid, &line));
    assert(!yaogui_coins_to_line(NULL, &line));
    assert(!yaogui_coins_to_line(cases[0].coins, NULL));
    assert(strcmp(yaogui_line_name((yaogui_line_t)5), "无效") == 0);
    assert(strcmp(yaogui_random_source_name(YAOGUI_SOURCE_ESP32_RF),
                  "ESP32-C3 射频真随机") == 0);
}

static void test_six_line_flow_and_change(void)
{
    static const uint8_t coins[4][YAOGUI_SHELL_COUNT] = {
        { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 },
    };
    static const yaogui_line_t expected[YAOGUI_LINE_COUNT] = {
        YAOGUI_OLD_YIN, YAOGUI_YOUNG_YANG, YAOGUI_YOUNG_YIN,
        YAOGUI_OLD_YANG, YAOGUI_OLD_YIN, YAOGUI_YOUNG_YANG,
    };
    yaogui_model_t model;
    yaogui_model_init(&model);

    uint32_t now = 100;
    for (size_t i = 0; i < YAOGUI_LINE_COUNT; i++) {
        assert(yaogui_model_start_seeded(&model, now, (uint32_t)i + 1U));
        yaogui_model_complete(&model, coins[i % 4U], YAOGUI_SOURCE_ESP32_RF,
                              now + 1U);
        now += YAOGUI_MIN_ROLL_MS + YAOGUI_LANDING_TOTAL_MS;
        yaogui_model_tick(&model, now);
        assert(model.phase == YAOGUI_RESULT);
        assert(model.line_count == i + 1U);
        assert(model.lines[i] == expected[i]);
        now++;
    }

    const yaogui_hexagram_t *primary = NULL;
    const yaogui_hexagram_t *changed = NULL;
    assert(yaogui_hexagram_from_lines(model.lines, false, &primary));
    assert(yaogui_hexagram_from_lines(model.lines, true, &changed));
    assert(primary->number == 64 && strcmp(primary->name, "未济") == 0);
    assert(changed->number == 61 && strcmp(changed->name, "中孚") == 0);

    assert(yaogui_model_start(&model, now));
    assert(model.line_count == 0);
}

static void test_all_hexagram_mappings(void)
{
    static const uint8_t king_wen_number_by_bits[64] = {
         2, 24,  7, 19, 15, 36, 46, 11,
        16, 51, 40, 54, 62, 55, 32, 34,
         8,  3, 29, 60, 39, 63, 48,  5,
        45, 17, 47, 58, 31, 49, 28, 43,
        23, 27,  4, 41, 52, 22, 18, 26,
        35, 21, 64, 38, 56, 30, 50, 14,
        20, 42, 59, 61, 53, 37, 57,  9,
        12, 25,  6, 10, 33, 13, 44,  1,
    };
    static const char *const name_by_bits[64] = {
        "坤", "复", "师", "临", "谦", "明夷", "升", "泰",
        "豫", "震", "解", "归妹", "小过", "丰", "恒", "大壮",
        "比", "屯", "坎", "节", "蹇", "既济", "井", "需",
        "萃", "随", "困", "兑", "咸", "革", "大过", "夬",
        "剥", "颐", "蒙", "损", "艮", "贲", "蛊", "大畜",
        "晋", "噬嗑", "未济", "睽", "旅", "离", "鼎", "大有",
        "观", "益", "涣", "中孚", "渐", "家人", "巽", "小畜",
        "否", "无妄", "讼", "履", "遁", "同人", "姤", "乾",
    };
    for (uint8_t bits = 0; bits < 64; bits++) {
        yaogui_line_t lines[YAOGUI_LINE_COUNT];
        for (size_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
            lines[line] = (bits & (1U << line))
                ? YAOGUI_YOUNG_YANG : YAOGUI_YOUNG_YIN;
        }
        const yaogui_hexagram_t *hexagram = NULL;
        assert(yaogui_hexagram_from_lines(lines, false, &hexagram));
        assert(hexagram->number == king_wen_number_by_bits[bits]);
        assert(strcmp(hexagram->name, name_by_bits[bits]) == 0);
        assert(hexagram->text[0] != '\0');

        for (uint8_t moving = 0; moving < 64; moving++) {
            for (size_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
                const bool yang = (bits & (1U << line)) != 0;
                const bool changes = (moving & (1U << line)) != 0;
                lines[line] = yang
                    ? (changes ? YAOGUI_OLD_YANG : YAOGUI_YOUNG_YANG)
                    : (changes ? YAOGUI_OLD_YIN : YAOGUI_YOUNG_YIN);
            }
            const yaogui_hexagram_t *primary = NULL;
            const yaogui_hexagram_t *changed = NULL;
            assert(yaogui_hexagram_from_lines(lines, false, &primary));
            assert(yaogui_hexagram_from_lines(lines, true, &changed));
            assert(primary->number == king_wen_number_by_bits[bits]);
            assert(changed->number ==
                   king_wen_number_by_bits[bits ^ moving]);
        }
    }

    yaogui_line_t invalid[YAOGUI_LINE_COUNT] = {
        YAOGUI_YOUNG_YIN, YAOGUI_YOUNG_YIN, YAOGUI_YOUNG_YIN,
        YAOGUI_YOUNG_YIN, YAOGUI_YOUNG_YIN, (yaogui_line_t)5,
    };
    const yaogui_hexagram_t *hexagram = NULL;
    assert(!yaogui_hexagram_from_lines(invalid, false, &hexagram));
    assert(!yaogui_hexagram_from_lines(NULL, false, &hexagram));
    assert(!yaogui_hexagram_from_lines(invalid, false, NULL));
}

static void test_text_data_completeness(void)
{
    size_t line_count = 0;
    for (size_t i = 0; i < 64; i++) {
        const yaogui_hexagram_t *hexagram = &YAOGUI_HEXAGRAMS[i];
        assert(hexagram->number == i + 1U);
        assert(hexagram->name && hexagram->name[0]);
        assert(hexagram->text && hexagram->text[0]);
        for (size_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
            assert(hexagram->lines[line] && hexagram->lines[line][0]);
            line_count++;
        }
        assert((hexagram->special != NULL) == (i < 2));
    }
    assert(line_count == 384);
    assert(strcmp(YAOGUI_HEXAGRAMS[0].special,
                  "用九：见群龙无首，吉。") == 0);
    assert(strcmp(YAOGUI_HEXAGRAMS[1].special, "用六：利永贞。") == 0);
}

static void test_reading_pagination_and_zhu_xi_primary(void)
{
    for (uint8_t moving = 0; moving <= YAOGUI_LINE_COUNT; moving++) {
        yaogui_line_t lines[YAOGUI_LINE_COUNT];
        for (uint8_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
            lines[line] = line < moving
                ? YAOGUI_OLD_YANG : YAOGUI_YOUNG_YANG;
        }
        yaogui_reading_plan_t plan;
        assert(yaogui_reading_build(lines, &plan));
        assert(plan.count == (uint8_t)(moving > 0 ? 2 : 1));
        assert(plan.pages[0].type == YAOGUI_READING_PRIMARY_GUACI);
        for (uint8_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
            assert(strstr(plan.pages[0].text,
                          plan.pages[0].hexagram->lines[line]) != NULL);
        }
        assert(strstr(plan.pages[0].text,
                      plan.pages[0].hexagram->text) != NULL);
        assert(strstr(plan.pages[0].text,
                      plan.pages[0].hexagram->special) != NULL);
        if (moving > 0) {
            assert(plan.pages[1].type ==
                   YAOGUI_READING_CHANGED_GUACI);
            for (uint8_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
                assert(strstr(plan.pages[1].text,
                              plan.pages[1].hexagram->lines[line]) != NULL);
            }
        }
        size_t primary_count = 0;
        size_t primary_index = 0;
        for (size_t page = 0; page < plan.count; page++) {
            assert(plan.pages[page].text[0]);
            if (plan.pages[page].is_primary) {
                primary_count++;
                primary_index = page;
            }
        }
        assert(primary_count == (moving == 3 ? 2U : 1U));
        if (moving == 0) assert(primary_index == 0);
        if (moving == 3) {
            assert(plan.pages[0].is_primary);
            assert(plan.pages[1].is_primary);
        }
        if (moving == 1 || moving == 2) assert(primary_index == 0U);
        if (moving == 4 || moving == 5) {
            assert(plan.pages[primary_index].type ==
                   YAOGUI_READING_CHANGED_GUACI);
            assert(primary_index == 1U);
        }
        if (moving == 6) {
            assert(plan.pages[primary_index].type ==
                   YAOGUI_READING_PRIMARY_GUACI);
            assert(primary_index == 0U);
        }
    }
}

static void test_reading_key_flow(void)
{
    yaogui_model_t model;
    yaogui_model_init(&model);
    model.phase = YAOGUI_RESULT;
    model.line_count = YAOGUI_LINE_COUNT;
    for (size_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
        model.lines[line] = line == 0 ? YAOGUI_OLD_YANG
                                     : YAOGUI_YOUNG_YANG;
    }
    assert(yaogui_reading_build(model.lines, &model.reading));
    assert(!yaogui_model_key(&model, YAOGUI_KEY_OK, 10, 1));
    assert(model.reading.open && model.reading.current == 0);
    assert(!yaogui_model_key(&model, YAOGUI_KEY_UP, 11, 1));
    assert(model.reading.current == 0);
    assert(!yaogui_model_key(&model, YAOGUI_KEY_DOWN, 12, 1));
    assert(model.reading.current == 1);
    while (model.reading.current + 1U < model.reading.count) {
        (void)yaogui_model_key(&model, YAOGUI_KEY_DOWN, 13, 1);
    }
    uint8_t last = model.reading.current;
    (void)yaogui_model_key(&model, YAOGUI_KEY_DOWN, 14, 1);
    assert(model.reading.current == last);
    assert(!yaogui_model_key(&model, YAOGUI_KEY_OK, 15, 1));
    assert(!model.reading.open && model.phase == YAOGUI_RESULT);

    assert(yaogui_model_key(&model, YAOGUI_KEY_OK_LONG, 20, 2));
    assert(model.phase == YAOGUI_ROLLING);
    assert(model.line_count == 0);
}

int main(void)
{
    test_minimum_roll_and_success();
    test_failure_and_retry();
    test_animation_is_not_a_result();
    test_seeded_physics_and_non_overlapping_landing();
    test_traditional_three_coin_mapping();
    test_six_line_flow_and_change();
    test_all_hexagram_mappings();
    test_shell_to_trigram_mapping();
    test_text_data_completeness();
    test_reading_pagination_and_zhu_xi_primary();
    test_reading_key_flow();
    return 0;
}
