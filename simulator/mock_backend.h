#pragma once

#include "yaogui_logic.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MOCK_RANDOM_OK = 0,
    MOCK_RANDOM_ERROR,
    MOCK_MODE_COUNT,
} mock_mode_t;

typedef struct {
    bool pending;
    uint32_t due_ms;
    uint32_t sequence;
    uint32_t random_state;
    mock_mode_t mode;
} mock_backend_t;

void mock_backend_init(mock_backend_t *backend);
void mock_backend_select(mock_backend_t *backend, int direction);
const char *mock_backend_mode_name(const mock_backend_t *backend);
void mock_backend_request(mock_backend_t *backend, uint32_t now_ms);
bool mock_backend_poll(mock_backend_t *backend, yaogui_model_t *model,
                       uint32_t now_ms);
