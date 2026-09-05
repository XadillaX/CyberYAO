#include "yaogui_logic.h"

#include <stdio.h>
#include <string.h>

void yaogui_model_init(yaogui_model_t *model)
{
    memset(model, 0, sizeof(*model));
    model->phase = YAOGUI_READY;
}

static uint32_t motion_random(uint32_t *state)
{
    uint32_t value = *state ? *state : UINT32_C(0x6d2b79f5);
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static uint32_t random_range(uint32_t *state, uint32_t minimum,
                             uint32_t maximum)
{
    return minimum + motion_random(state) % (maximum - minimum + 1U);
}

bool yaogui_model_start_seeded(yaogui_model_t *model, uint32_t now_ms,
                               uint32_t seed)
{
    if (model->phase == YAOGUI_ROLLING) return false;
    model->reading.open = false;
    model->reading.current = 0;
    if (model->line_count >= YAOGUI_LINE_COUNT) {
        memset(model->lines, 0, sizeof(model->lines));
        model->line_count = 0;
    }
    model->phase = YAOGUI_ROLLING;
    model->error = YAOGUI_ERROR_NONE;
    model->started_ms = now_ms;
    model->request_finished = false;
    model->request_succeeded = false;
    model->request_finished_ms = 0;
    model->pending_source = YAOGUI_SOURCE_NONE;
    memset(model->pending, 0, sizeof(model->pending));
    model->motion_seed = seed ? seed : UINT32_C(0x59474f55);

    uint32_t random = model->motion_seed;
    /* 三个锚点构成宽三角形；轻微二维抖动后仍能保证龟甲外框不重叠。 */
    static const int16_t anchors[YAOGUI_SHELL_COUNT][2] = {
        { 28, 42 }, { 116, 42 }, { 72, 116 },
    };
    uint8_t slots[YAOGUI_SHELL_COUNT] = { 0, 1, 2 };
    for (size_t i = YAOGUI_SHELL_COUNT - 1U; i > 0; i--) {
        size_t other = random_range(&random, 0, (uint32_t)i);
        uint8_t temporary = slots[i];
        slots[i] = slots[other];
        slots[other] = temporary;
    }
    for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++) {
        model->start_x[i] = (int16_t)(72 +
            (int16_t)random_range(&random, 0, 8) - 4);
        int16_t speed = (int16_t)random_range(&random, 32, 68);
        model->velocity_x[i] = (motion_random(&random) & 1U) ? speed : -speed;
        model->hop_ms[i] = (uint16_t)random_range(&random, 680, 980);
        model->height[i] = (uint8_t)random_range(&random, 32, 58);
        model->angle_start[i] = (int16_t)random_range(&random, 0, 3599);
        int16_t spin = (int16_t)random_range(&random, 17, 35);
        model->angular_velocity[i] =
            (motion_random(&random) & 1U) ? spin : -spin;
        const uint8_t slot = slots[i];
        model->target_x[i] = (int16_t)(anchors[slot][0] +
            (int16_t)random_range(&random, 0, 8) - 4);
        model->target_y[i] = (int16_t)(anchors[slot][1] +
            (int16_t)random_range(&random, 0, 8) - 4);
        model->target_angle[i] =
            (int16_t)random_range(&random, 0, 3599);
    }
    return true;
}

bool yaogui_model_start(yaogui_model_t *model, uint32_t now_ms)
{
    return yaogui_model_start_seeded(
        model, now_ms, now_ms ^ UINT32_C(0xa511e9b3));
}

void yaogui_model_complete(yaogui_model_t *model,
                           const uint8_t values[YAOGUI_SHELL_COUNT],
                           yaogui_random_source_t source, uint32_t now_ms)
{
    if (model->phase != YAOGUI_ROLLING || model->request_finished ||
        source == YAOGUI_SOURCE_NONE) return;
    memcpy(model->pending, values, sizeof(model->pending));
    model->pending_source = source;
    model->request_finished_ms = now_ms;
    model->request_succeeded = true;
    model->request_finished = true;
}

void yaogui_model_fail(yaogui_model_t *model, yaogui_error_t error,
                       uint32_t now_ms)
{
    if (model->phase != YAOGUI_ROLLING || model->request_finished) return;
    model->error = error == YAOGUI_ERROR_NONE ? YAOGUI_ERROR_INTERNAL : error;
    model->request_finished_ms = now_ms;
    model->request_succeeded = false;
    model->request_finished = true;
}

static uint32_t landing_start_ms(const yaogui_model_t *model)
{
    uint32_t minimum = model->started_ms + YAOGUI_MIN_ROLL_MS;
    return (int32_t)(model->request_finished_ms - minimum) > 0
        ? model->request_finished_ms : minimum;
}

void yaogui_model_tick(yaogui_model_t *model, uint32_t now_ms)
{
    if (model->phase != YAOGUI_ROLLING || !model->request_finished) return;

    // 随机源完成后仍保留完整的错峰落地段；无符号减法兼容毫秒计数器回绕。
    int32_t landing_elapsed = (int32_t)(now_ms - landing_start_ms(model));
    if (landing_elapsed < 0 ||
        (uint32_t)landing_elapsed < YAOGUI_LANDING_TOTAL_MS) return;
    if (model->request_succeeded) {
        memcpy(model->result, model->pending, sizeof(model->result));
        model->result_source = model->pending_source;
        yaogui_line_t line;
        if (model->line_count < YAOGUI_LINE_COUNT &&
            yaogui_coins_to_line(model->result, &line)) {
            model->lines[model->line_count++] = line;
        }
        model->phase = YAOGUI_RESULT;
        if (model->line_count == YAOGUI_LINE_COUNT) {
            (void)yaogui_reading_build(model->lines, &model->reading);
        }
    } else {
        model->phase = YAOGUI_ERROR;
    }
}

uint8_t yaogui_model_frame(const yaogui_model_t *model, uint32_t now_ms,
                           size_t shell_index)
{
    if (model->phase == YAOGUI_RESULT && shell_index < YAOGUI_SHELL_COUNT) {
        return model->result[shell_index];
    }
    if (model->phase != YAOGUI_ROLLING || shell_index >= YAOGUI_SHELL_COUNT) {
        return 0;
    }

    // 此序列只驱动翻滚动画，绝不作为结果；最终值只能由随机源写入。
    uint32_t frame = (uint32_t)(now_ms - model->started_ms) / YAOGUI_FRAME_MS;
    return (uint8_t)((frame + shell_index) & 1U);
}

static int16_t reflected_x(int32_t value)
{
    const int32_t span = YAOGUI_ARENA_WIDTH - YAOGUI_SHELL_WIDTH - 8;
    int32_t folded = value % (span * 2);
    if (folded < 0) folded += span * 2;
    if (folded > span) folded = span * 2 - folded;
    return (int16_t)(4 + folded);
}

static void rolling_motion(const yaogui_model_t *model, uint32_t elapsed,
                           size_t index, yaogui_shell_motion_t *motion)
{
    if (elapsed < YAOGUI_SHELL_HIDE_MS) {
        /* 铜钱始终真实存在于龟壳下方，壳移开后才会自然露出。 */
        motion->x = 81;
        motion->y = 76;
        motion->scale_x = 256;
        motion->scale_y = 256;
        motion->shadow_scale = 0;
        motion->shadow_opa = 0;
        motion->rotation = (int16_t)(index * 160);
        return;
    }
    elapsed -= YAOGUI_SHELL_HIDE_MS;
    uint32_t period = model->hop_ms[index];
    uint32_t local = (elapsed + index * 173U) % period;
    uint32_t height = (4U * model->height[index] * local *
                       (period - local)) / (period * period);
    int32_t travel = (int32_t)model->velocity_x[index] * (int32_t)elapsed / 1000;
    motion->x = reflected_x(model->start_x[index] + travel);
    motion->y = (int16_t)(76 - (int32_t)height);
    motion->rotation = (int16_t)((model->angle_start[index] +
        (int32_t)model->angular_velocity[index] * (int32_t)elapsed / 10) % 3600);

    uint32_t flip = (elapsed * (uint32_t)(7U + index * 2U) / 10U +
                     (uint32_t)model->angle_start[index]) % 512U;
    uint32_t folded = flip <= 256U ? flip : 512U - flip;
    uint16_t perspective = (uint16_t)(220U + height * 36U /
                                      model->height[index]);
    uint16_t compression = (uint16_t)(32U + (folded > 128U
        ? folded - 128U : 128U - folded) * (perspective - 32U) / 128U);
    motion->scale_x = compression;
    motion->scale_y = perspective;
    motion->belly = flip >= 128U && flip < 384U;
    motion->shadow_scale = (uint16_t)(220U - height * 105U /
                                      model->height[index]);
    motion->shadow_opa = (uint8_t)(95U - height * 60U /
                                   model->height[index]);
}

static void separate_collision(const yaogui_model_t *model, uint32_t elapsed,
                               size_t shell_index,
                               yaogui_shell_motion_t *motion)
{
    for (size_t other = 0; other < YAOGUI_SHELL_COUNT; other++) {
        if (other == shell_index) continue;
        yaogui_shell_motion_t neighbour = { .scale_x = 256, .scale_y = 256 };
        rolling_motion(model, elapsed, other, &neighbour);
        int16_t dx = (int16_t)(motion->x - neighbour.x);
        int16_t dy = (int16_t)(motion->y - neighbour.y);
        if (dx > -42 && dx < 42 && dy > -24 && dy < 24) {
            int16_t push = (int16_t)((42 - (dx < 0 ? -dx : dx)) / 2);
            motion->x += shell_index < other ? -push : push;
            if (motion->x < 4) motion->x = 4;
            if (motion->x > 140) motion->x = 140;
        }
    }
}

void yaogui_model_motion(const yaogui_model_t *model, uint32_t now_ms,
                         size_t shell_index, yaogui_shell_motion_t *motion)
{
    if (!motion) return;
    *motion = (yaogui_shell_motion_t) {
        .x = 72,
        .y = 76,
        .scale_x = 256,
        .scale_y = 256,
        .shadow_scale = 220,
        .shadow_opa = 72,
        .belly = model && shell_index < YAOGUI_SHELL_COUNT
            ? yaogui_model_frame(model, now_ms, shell_index) != 0 : false,
    };
    if (!model || shell_index >= YAOGUI_SHELL_COUNT) return;
    if (model->phase == YAOGUI_RESULT) {
        motion->x = model->target_x[shell_index];
        motion->y = model->target_y[shell_index];
        motion->rotation = model->target_angle[shell_index];
        motion->belly = model->result[shell_index] != 0;
        motion->landed = true;
        return;
    }
    if (model->phase != YAOGUI_ROLLING) {
        motion->x = 81;
        motion->y = 76;
        motion->rotation = (int16_t)(shell_index * 160);
        motion->shadow_opa = 0;
        return;
    }

    uint32_t elapsed = now_ms - model->started_ms;
    rolling_motion(model, elapsed, shell_index, motion);
    if (elapsed >= YAOGUI_SHELL_HIDE_MS) {
        separate_collision(model, elapsed, shell_index, motion);
    }

    if (!model->request_finished) return;
    uint32_t landing_start = landing_start_ms(model);
    int32_t local = (int32_t)(now_ms - landing_start) -
                    (int32_t)(shell_index * YAOGUI_SHELL_STAGGER_MS);
    if (local < 0) return;
    if ((uint32_t)local >= YAOGUI_LANDING_MS) {
        motion->x = model->target_x[shell_index];
        motion->y = model->target_y[shell_index];
        motion->rotation = model->target_angle[shell_index];
        motion->scale_x = 256;
        motion->scale_y = 256;
        motion->shadow_scale = 220;
        motion->shadow_opa = 72;
        motion->belly = model->request_succeeded
            ? model->pending[shell_index] != 0 : false;
        motion->landed = true;
        return;
    }

    yaogui_shell_motion_t start = { .scale_x = 256, .scale_y = 256 };
    rolling_motion(model, landing_start - model->started_ms, shell_index,
                   &start);
    uint32_t progress = (uint32_t)local * 256U / YAOGUI_LANDING_MS;
    uint32_t eased = 256U - ((256U - progress) * (256U - progress) / 256U);
    motion->x = (int16_t)(start.x +
        ((model->target_x[shell_index] - start.x) * (int32_t)eased) / 256);
    int32_t base_y = start.y +
        ((model->target_y[shell_index] - start.y) * (int32_t)eased) / 256;
    uint32_t bounce = 0;
    if (progress > 150U) {
        uint32_t rebound = progress - 150U;
        bounce = 9U * rebound * (106U - rebound) / (53U * 53U);
    }
    motion->y = (int16_t)(base_y - (int32_t)bounce);
    motion->rotation = (int16_t)(start.rotation +
        ((model->target_angle[shell_index] - start.rotation) *
         (int32_t)eased) / 256);
    motion->scale_x = (uint16_t)(start.scale_x +
        ((256 - start.scale_x) * eased) / 256U);
    motion->scale_y = (uint16_t)(start.scale_y +
        ((256 - start.scale_y) * eased) / 256U);
    motion->shadow_scale = (uint16_t)(start.shadow_scale +
        ((220 - start.shadow_scale) * eased) / 256U);
    motion->shadow_opa = (uint8_t)(start.shadow_opa +
        ((72 - start.shadow_opa) * eased) / 256U);
    if (model->request_succeeded && progress >= 160U) {
        motion->belly = model->pending[shell_index] != 0;
    }
}

bool yaogui_coins_to_line(const uint8_t values[YAOGUI_SHELL_COUNT],
                          yaogui_line_t *line)
{
    if (!values || !line) return false;

    uint8_t total = 6;
    for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++) {
        if (values[i] > 1) return false;
        total = (uint8_t)(total + values[i]);
    }
    *line = (yaogui_line_t)total;
    return true;
}

bool yaogui_line_is_yang(yaogui_line_t line)
{
    return line == YAOGUI_YOUNG_YANG || line == YAOGUI_OLD_YANG;
}

bool yaogui_line_is_old(yaogui_line_t line)
{
    return line == YAOGUI_OLD_YIN || line == YAOGUI_OLD_YANG;
}

const char *yaogui_line_name(yaogui_line_t line)
{
    switch (line) {
    case YAOGUI_OLD_YIN:
        return "老阴";
    case YAOGUI_YOUNG_YANG:
        return "少阳";
    case YAOGUI_YOUNG_YIN:
        return "少阴";
    case YAOGUI_OLD_YANG:
        return "老阳";
    default:
        return "无效";
    }
}

bool yaogui_line_changed_is_yang(yaogui_line_t line)
{
    if (line == YAOGUI_OLD_YIN) return true;
    if (line == YAOGUI_OLD_YANG) return false;
    return yaogui_line_is_yang(line);
}

/*
 * 索引的低三位为下卦、后三位为上卦，爻位均按初爻到上爻排列。
 * 这种布局让六爻位图可直接索引，避免另维护一份容易错位的上下卦表。
 */
typedef struct {
    uint8_t number;
    const char *name;
    const char *text;
} legacy_hexagram_t;

static const legacy_hexagram_t HEXAGRAMS[64] = {
    { 2, "坤", "元亨。利牝马之贞。君子有攸往，先迷后得主，利。西南得朋，东北丧朋。安贞，吉。" },
    { 24, "复", "亨。出入无疾，朋来无咎。反复其道，七日来复，利有攸往。" },
    { 7, "师", "贞，丈人吉，无咎。" },
    { 19, "临", "元亨，利贞。至于八月有凶。" },
    { 15, "谦", "亨，君子有终。" },
    { 36, "明夷", "利艰贞。" },
    { 46, "升", "元亨，用见大人，勿恤，南征吉。" },
    { 11, "泰", "小往大来，吉亨。" },
    { 16, "豫", "利建侯行师。" },
    { 51, "震", "亨。震来虩虩，笑言哑哑。震惊百里，不丧匕鬯。" },
    { 40, "解", "利西南，无所往，其来复吉。有攸往，夙吉。" },
    { 54, "归妹", "征凶，无攸利。" },
    { 62, "小过", "亨。利贞。可小事，不可大事。飞鸟遗之音，不宜上宜下，大吉。" },
    { 55, "丰", "亨，王假之。勿忧，宜日中。" },
    { 32, "恒", "亨，无咎，利贞，利有攸往。" },
    { 34, "大壮", "利贞。" },
    { 8, "比", "吉。原筮元永贞，无咎。不宁方来，后夫凶。" },
    { 3, "屯", "元亨，利贞。勿用有攸往，利建侯。" },
    { 29, "坎", "有孚，维心亨，行有尚。" },
    { 60, "节", "亨。苦节不可贞。" },
    { 39, "蹇", "利西南，不利东北。利见大人，贞吉。" },
    { 63, "既济", "亨小，利贞。初吉终乱。" },
    { 48, "井", "改邑不改井，无丧无得。往来井井。汔至，亦未繘井，羸其瓶，凶。" },
    { 5, "需", "有孚，光亨，贞吉。利涉大川。" },
    { 45, "萃", "亨。王假有庙，利见大人，亨。利贞。用大牲吉，利有攸往。" },
    { 17, "随", "元亨，利贞，无咎。" },
    { 47, "困", "亨，贞，大人吉，无咎。有言不信。" },
    { 58, "兑", "亨，利贞。" },
    { 31, "咸", "亨，利贞。取女吉。" },
    { 49, "革", "巳日乃孚，元亨。利贞。悔亡。" },
    { 28, "大过", "栋桡，利有攸往，亨。" },
    { 43, "夬", "扬于王庭，孚号有厉。告自邑，不利即戎，利有攸往。" },
    { 23, "剥", "不利有攸往。" },
    { 27, "颐", "贞吉。观颐，自求口实。" },
    { 4, "蒙", "亨。匪我求童蒙，童蒙求我。初筮告，再三渎，渎则不告。利贞。" },
    { 41, "损", "有孚，元吉。无咎，可贞，利有攸往。曷之用？二簋可用享。" },
    { 52, "艮", "艮其背，不获其身，行其庭，不见其人，无咎。" },
    { 22, "贲", "亨。小利有攸往。" },
    { 18, "蛊", "元亨，利涉大川。先甲三日，后甲三日。" },
    { 26, "大畜", "利贞。不家食吉，利涉大川。" },
    { 35, "晋", "康侯用锡马蕃庶，昼日三接。" },
    { 21, "噬嗑", "亨。利用狱。" },
    { 64, "未济", "亨。小狐汔济，濡其尾，无攸利。" },
    { 38, "睽", "小事吉。" },
    { 56, "旅", "小亨，旅贞吉。" },
    { 30, "离", "利贞，亨；畜牝牛，吉。" },
    { 50, "鼎", "元吉，亨。" },
    { 14, "大有", "元亨。" },
    { 20, "观", "盥而不荐，有孚颙若。" },
    { 42, "益", "利有攸往，利涉大川。" },
    { 59, "涣", "亨。王假有庙，利涉大川，利贞。" },
    { 61, "中孚", "豚鱼吉，利涉大川，利贞。" },
    { 53, "渐", "女归吉，利贞。" },
    { 37, "家人", "利女贞。" },
    { 57, "巽", "小亨，利有攸往，利见大人。" },
    { 9, "小畜", "亨。密云不雨，自我西郊。" },
    { 12, "否", "否之匪人，不利君子贞，大往小来。" },
    { 25, "无妄", "元亨，利贞。其匪正有眚，不利有攸往。" },
    { 6, "讼", "有孚，窒，惕，中吉，终凶。利见大人，不利涉大川。" },
    { 10, "履", "履虎尾，不咥人，亨。" },
    { 33, "遁", "亨，小利贞。" },
    { 13, "同人", "同人于野，亨。利涉大川，利君子贞。" },
    { 44, "姤", "女壮，勿用取女。" },
    { 1, "乾", "元亨，利贞。" },
};

/* 位图低三位为下卦、后三位为上卦；值为文王卦序。 */
static const uint8_t HEXAGRAM_NUMBER_BY_BITS[64] = {
     2, 24,  7, 19, 15, 36, 46, 11, 16, 51, 40, 54, 62, 55, 32, 34,
     8,  3, 29, 60, 39, 63, 48,  5, 45, 17, 47, 58, 31, 49, 28, 43,
    23, 27,  4, 41, 52, 22, 18, 26, 35, 21, 64, 38, 56, 30, 50, 14,
    20, 42, 59, 61, 53, 37, 57,  9, 12, 25,  6, 10, 33, 13, 44,  1,
};

bool yaogui_hexagram_from_lines(
    const yaogui_line_t lines[YAOGUI_LINE_COUNT], bool changed,
    const yaogui_hexagram_t **hexagram)
{
    if (!lines || !hexagram) return false;
    uint8_t bits = 0;
    for (size_t i = 0; i < YAOGUI_LINE_COUNT; i++) {
        if (lines[i] < YAOGUI_OLD_YIN || lines[i] > YAOGUI_OLD_YANG) {
            return false;
        }
        bool yang = changed ? yaogui_line_changed_is_yang(lines[i])
                            : yaogui_line_is_yang(lines[i]);
        if (yang) bits |= (uint8_t)(1U << i);
    }
    (void)HEXAGRAMS; /* 保留既有卦序文本仅用于兼容本次迁移的编译单元。 */
    *hexagram = &YAOGUI_HEXAGRAMS[HEXAGRAM_NUMBER_BY_BITS[bits] - 1U];
    return true;
}

static bool append_reading_text(yaogui_reading_page_t *page,
                                const char *heading, const char *text,
                                bool primary)
{
    size_t used = strlen(page->text);
    int written = snprintf(
        page->text + used, sizeof(page->text) - used,
        "%s%s%s%s%s",
        used ? "\n\n" : "",
        primary ? "主读·" : "",
        heading ? heading : "",
        heading ? "\n" : "",
        text);
    if (written < 0 || (size_t)written >= sizeof(page->text) - used) {
        return false;
    }
    page->is_primary = page->is_primary || primary;
    return true;
}

static bool add_full_hexagram_page(
    yaogui_reading_plan_t *plan, yaogui_reading_page_type_t type,
    const yaogui_hexagram_t *hexagram, bool guaci_primary,
    uint8_t primary_line_mask, bool special_primary)
{
    if (plan->count >= YAOGUI_MAX_READING_PAGES) return false;
    yaogui_reading_page_t *page = &plan->pages[plan->count];
    memset(page, 0, sizeof(*page));
    page->type = type;
    page->hexagram = hexagram;
    if (!append_reading_text(page, "卦辞", hexagram->text, guaci_primary)) {
        return false;
    }
    for (uint8_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
        if (!append_reading_text(
                page, NULL, hexagram->lines[line],
                (primary_line_mask & (uint8_t)(1U << line)) != 0)) {
            return false;
        }
    }
    if (hexagram->special &&
        !append_reading_text(
            page, NULL, hexagram->special, special_primary)) {
        return false;
    }
    plan->count++;
    return true;
}

bool yaogui_reading_build(const yaogui_line_t lines[YAOGUI_LINE_COUNT],
                          yaogui_reading_plan_t *plan)
{
    if (!lines || !plan) return false;
    memset(plan, 0, sizeof(*plan));
    const yaogui_hexagram_t *primary = NULL;
    const yaogui_hexagram_t *changed = NULL;
    if (!yaogui_hexagram_from_lines(lines, false, &primary) ||
        !yaogui_hexagram_from_lines(lines, true, &changed)) return false;

    uint8_t moving_count = 0;
    uint8_t highest_moving = 0;
    uint8_t lowest_static = 0;
    bool has_static = false;
    for (uint8_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
        if (yaogui_line_is_old(lines[line])) {
            moving_count++;
            highest_moving = line;
        } else if (!has_static) {
            lowest_static = line;
            has_static = true;
        }
    }

    uint8_t primary_line_mask = 0;
    if (moving_count == 1) {
        primary_line_mask = (uint8_t)(1U << highest_moving);
    } else if (moving_count == 2) {
        primary_line_mask = (uint8_t)(1U << highest_moving);
    }
    if (!add_full_hexagram_page(
            plan, YAOGUI_READING_PRIMARY_GUACI, primary,
            moving_count == 0 || moving_count == 3,
            primary_line_mask,
            moving_count == 6 && primary->special != NULL)) {
        return false;
    }

    if (moving_count > 0) {
        uint8_t changed_line_mask = 0;
        if ((moving_count == 4 || moving_count == 5) && has_static) {
            changed_line_mask = (uint8_t)(1U << lowest_static);
        }
        if (!add_full_hexagram_page(
                plan, YAOGUI_READING_CHANGED_GUACI, changed,
                moving_count == 3 ||
                (moving_count == 6 && primary->special == NULL),
                changed_line_mask, false)) {
            return false;
        }
    }
    return true;
}

bool yaogui_model_key(yaogui_model_t *model, yaogui_key_t key,
                      uint32_t now_ms, uint32_t seed)
{
    if (!model) return false;
    if (key == YAOGUI_KEY_OK_LONG && model->phase != YAOGUI_ROLLING) {
        yaogui_model_init(model);
        return yaogui_model_start_seeded(model, now_ms, seed);
    }
    if (model->reading.open) {
        if (key == YAOGUI_KEY_UP && model->reading.current > 0) {
            model->reading.current--;
        } else if (key == YAOGUI_KEY_DOWN &&
                   model->reading.current + 1U < model->reading.count) {
            model->reading.current++;
        } else if (key == YAOGUI_KEY_OK) {
            model->reading.open = false;
        }
        return false;
    }
    if (key == YAOGUI_KEY_OK && model->phase == YAOGUI_RESULT &&
        model->line_count == YAOGUI_LINE_COUNT) {
        model->reading.open = true;
        model->reading.current = 0;
        return false;
    }
    return key == YAOGUI_KEY_OK &&
           yaogui_model_start_seeded(model, now_ms, seed);
}

const char *yaogui_random_source_name(yaogui_random_source_t source)
{
    switch (source) {
    case YAOGUI_SOURCE_ESP32_RF:
        return "ESP32-C3 射频真随机";
    default:
        return "未知来源";
    }
}

bool yaogui_shells_to_trigram(const uint8_t values[YAOGUI_SHELL_COUNT],
                              yaogui_trigram_t *trigram)
{
    if (!values || !trigram) return false;
    uint8_t bits = 0;
    for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++) {
        if (values[i] > 1) return false;
        bits |= (uint8_t)(values[i] << i);
    }
    *trigram = (yaogui_trigram_t)bits;
    return true;
}

bool yaogui_trigram_line_is_yang(yaogui_trigram_t trigram, size_t line)
{
    return trigram <= YAOGUI_TRIGRAM_QIAN && line < YAOGUI_SHELL_COUNT &&
           (((uint8_t)trigram >> line) & 1U) != 0;
}

const char *yaogui_trigram_symbol(yaogui_trigram_t trigram)
{
    static const char *const symbols[] = {
        "☷", "☳", "☵", "☱", "☶", "☲", "☴", "☰",
    };
    return trigram <= YAOGUI_TRIGRAM_QIAN ? symbols[trigram] : "";
}

const char *yaogui_trigram_name(yaogui_trigram_t trigram)
{
    static const char *const names[] = {
        "坤", "震", "坎", "兑", "艮", "离", "巽", "乾",
    };
    return trigram <= YAOGUI_TRIGRAM_QIAN ? names[trigram] : "";
}

const char *yaogui_trigram_nature(yaogui_trigram_t trigram)
{
    static const char *const natures[] = {
        "地", "雷", "水", "泽", "山", "火", "风", "天",
    };
    return trigram <= YAOGUI_TRIGRAM_QIAN ? natures[trigram] : "";
}

const char *yaogui_trigram_text(yaogui_trigram_t trigram)
{
    static const char *const texts[] = {
        "元亨，利牝马之贞。",
        "亨。震来虩虩，笑言哑哑。",
        "习坎，有孚，维心亨，行有尚。",
        "亨，利贞。",
        "艮其背，不获其身；行其庭，不见其人。",
        "利贞，亨；畜牝牛，吉。",
        "小亨，利有攸往，利见大人。",
        "元亨，利贞。",
    };
    return trigram <= YAOGUI_TRIGRAM_QIAN ? texts[trigram] : "";
}
