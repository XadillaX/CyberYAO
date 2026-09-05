#include "mock_backend.h"

#include <stddef.h>
#include <time.h>

static uint32_t mock_random(mock_backend_t* backend) {
  uint32_t value = backend->random_state;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  backend->random_state = value;
  return value;
}

void mock_backend_init(mock_backend_t* backend) {
  if (!backend) return;
  struct timespec now = {0};
  timespec_get(&now, TIME_UTC);
  uint32_t seed = (uint32_t)now.tv_sec ^ (uint32_t)now.tv_nsec;
  if (!seed) seed = UINT32_C(0x6d2b79f5);
  *backend = (mock_backend_t){
      .random_state = seed,
      .mode = MOCK_RANDOM_OK,
  };
}

void mock_backend_select(mock_backend_t* backend, int direction) {
  if (!backend || backend->pending) return;
  int mode = (int)backend->mode + direction;
  if (mode < 0) mode = MOCK_MODE_COUNT - 1;
  if (mode >= MOCK_MODE_COUNT) mode = 0;
  backend->mode = (mock_mode_t)mode;
}

const char* mock_backend_mode_name(const mock_backend_t* backend) {
  if (!backend) return "未知";
  switch (backend->mode) {
    case MOCK_RANDOM_OK:
      return "Mock 真随机成功";
    case MOCK_RANDOM_ERROR:
      return "Mock 随机源失败";
    default:
      return "未知";
  }
}

void mock_backend_request(mock_backend_t* backend, uint32_t now_ms) {
  if (!backend) return;
  backend->pending = true;
  backend->sequence++;
  /* 模拟真机射频取样耗时，确保结果在最短摇动阶段内返回。 */
  backend->due_ms = now_ms + 350U + (backend->sequence * 347U) % 350U;
}

bool mock_backend_poll(mock_backend_t* backend,
                       yaogui_model_t* model,
                       uint32_t now_ms) {
  if (!backend || !model || !backend->pending ||
      (int32_t)(now_ms - backend->due_ms) < 0) {
    return false;
  }
  backend->pending = false;
  if (backend->mode == MOCK_RANDOM_ERROR) {
    yaogui_model_fail(model, YAOGUI_ERROR_RANDOM_SOURCE, now_ms);
  } else {
    uint32_t bits = mock_random(backend);
    uint8_t values[YAOGUI_SHELL_COUNT];
    for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++) {
      values[i] = (uint8_t)((bits >> i) & 1U);
    }
    yaogui_model_complete(model, values, YAOGUI_SOURCE_ESP32_RF, now_ms);
  }
  return true;
}
