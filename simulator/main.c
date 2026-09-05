#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mock_backend.h"
#include "yaogui_ambient_sound.h"
#include "yaogui_coin_sound.h"
#include "yaogui_view.h"

#include "lvgl.h"
#include "src/draw/snapshot/lv_snapshot.h"
#include "src/drivers/sdl/lv_sdl_window.h"

typedef enum {
  KEY_NONE = 0,
  KEY_UP = 1 << 0,
  KEY_DOWN = 1 << 1,
  KEY_CONFIRM = 1 << 2,
  KEY_CONFIRM_LONG = 1 << 7,
  KEY_SCALE_UP = 1 << 3,
  KEY_SCALE_DOWN = 1 << 4,
  KEY_RESET = 1 << 5,
  KEY_QUIT = 1 << 6,
} key_mask_t;

static unsigned s_keys;
static bool s_coin_audio_active;

_Static_assert(YAOGUI_AMBIENT_SOUND_RATE == YAOGUI_COIN_SOUND_RATE,
               "背景音与铜钱音效必须使用同一采样率");

static SDL_AudioDeviceID open_audio(void) {
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    fprintf(stderr, "无法初始化 SDL 音频：%s\n", SDL_GetError());
    return 0;
  }
  SDL_AudioSpec wanted = {
      .freq = YAOGUI_COIN_SOUND_RATE,
      .format = AUDIO_S16SYS,
      .channels = 1,
      .samples = 512,
      .callback = NULL,
  };
  SDL_AudioDeviceID device = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
  if (!device) {
    fprintf(stderr, "无法打开 SDL 音频：%s\n", SDL_GetError());
  }
  return device;
}

static void play_coin_sound(SDL_AudioDeviceID device) {
  if (!device) return;
  SDL_ClearQueuedAudio(device);
  if (SDL_QueueAudio(device,
                     yaogui_coin_sound_pcm,
                     yaogui_coin_sound_sample_count *
                         sizeof(yaogui_coin_sound_pcm[0])) != 0) {
    fprintf(stderr, "无法播放铜钱音效：%s\n", SDL_GetError());
    return;
  }
  s_coin_audio_active = true;
  SDL_PauseAudioDevice(device, 0);
}

static void maintain_ambient_sound(SDL_AudioDeviceID device) {
  if (!device) return;
  uint32_t queued = SDL_GetQueuedAudioSize(device);
  if (s_coin_audio_active) {
    if (queued > 0) return;
    s_coin_audio_active = false;
  }
  const uint32_t loop_bytes = (uint32_t)(yaogui_ambient_sound_sample_count *
                                         sizeof(yaogui_ambient_sound_pcm[0]));
  if (queued < loop_bytes / 2U) {
    if (SDL_QueueAudio(device, yaogui_ambient_sound_pcm, loop_bytes) != 0) {
      fprintf(stderr, "无法播放背景音：%s\n", SDL_GetError());
      return;
    }
  }
  SDL_PauseAudioDevice(device, 0);
}

static int watch_sdl_event(void* userdata, SDL_Event* event) {
  (void)userdata;
  if (event->type == SDL_QUIT) s_keys |= KEY_QUIT;
  if (event->type != SDL_KEYDOWN || event->key.repeat) return 1;
  switch (event->key.keysym.sym) {
    case SDLK_UP:
      s_keys |= KEY_UP;
      break;
    case SDLK_DOWN:
      s_keys |= KEY_DOWN;
      break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
      s_keys |= KEY_CONFIRM;
      break;
    case SDLK_l:
      s_keys |= KEY_CONFIRM_LONG;
      break;
    case SDLK_EQUALS:
    case SDLK_PLUS:
      s_keys |= KEY_SCALE_UP;
      break;
    case SDLK_MINUS:
      s_keys |= KEY_SCALE_DOWN;
      break;
    case SDLK_ESCAPE:
      s_keys |= KEY_RESET;
      break;
    case SDLK_q:
      s_keys |= KEY_QUIT;
      break;
    default:
      break;
  }
  return 1;
}

static float parse_scale(int argc, char** argv) {
  float scale = 2.0f;
  for (int i = 1; i + 1 < argc; i++) {
    if (strcmp(argv[i], "--scale") == 0) scale = strtof(argv[i + 1], NULL);
  }
  if (scale < 1.0f) scale = 1.0f;
  if (scale > 4.0f) scale = 4.0f;
  return scale;
}

static bool has_argument(int argc, char** argv, const char* argument) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], argument) == 0) return true;
  }
  return false;
}

static const char* argument_value(int argc, char** argv, const char* argument) {
  for (int i = 1; i + 1 < argc; i++) {
    if (strcmp(argv[i], argument) == 0) return argv[i + 1];
  }
  return NULL;
}

static bool save_snapshot(lv_obj_t* screen, const char* path) {
  lv_draw_buf_t* snapshot = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB888);
  if (!snapshot) return false;
  FILE* file = fopen(path, "wb");
  if (!file) {
    lv_draw_buf_destroy(snapshot);
    return false;
  }
  fprintf(file, "P6\n%u %u\n255\n", snapshot->header.w, snapshot->header.h);
  for (uint32_t y = 0; y < snapshot->header.h; y++) {
    const uint8_t* row = snapshot->data + y * snapshot->header.stride;
    for (uint32_t x = 0; x < snapshot->header.w; x++) {
      const uint8_t* pixel = row + x * 3U;
      fputc(pixel[2], file);
      fputc(pixel[1], file);
      fputc(pixel[0], file);
    }
  }
  fclose(file);
  lv_draw_buf_destroy(snapshot);
  return true;
}

static void set_title(lv_display_t* display, const mock_backend_t* mock) {
  (void)mock;
  lv_sdl_window_set_title(
      display, "摇龟模拟器 | ↑↓翻页 | Enter确认 | L长按确认 | +/-缩放");
}

int main(int argc, char** argv) {
  const bool smoke_test = has_argument(argc, argv, "--smoke-test");
  const char* snapshot_path = argument_value(argc, argv, "--snapshot");
  const char* snapshot_mode = argument_value(argc, argv, "--snapshot-mode");
  const bool interactive_audio = !smoke_test && !snapshot_path;
  uint32_t snapshot_now_ms = UINT32_MAX;
  lv_init();
  lv_display_t* display = lv_sdl_window_create(240, 320);
  if (!display) {
    fprintf(stderr, "无法创建 SDL2 窗口\n");
    return EXIT_FAILURE;
  }
  float scale = parse_scale(argc, argv);
  lv_sdl_window_set_zoom(display, scale);
  lv_sdl_window_set_resizeable(display, false);
  SDL_AddEventWatch(watch_sdl_event, NULL);
  SDL_AudioDeviceID audio_device = interactive_audio ? open_audio() : 0;
  maintain_ambient_sound(audio_device);
  lv_obj_t* loading_screen = NULL;
  const bool snapshot_loading =
      snapshot_path && snapshot_mode && strcmp(snapshot_mode, "loading") == 0;
  if (interactive_audio || snapshot_loading) {
    loading_screen = yaogui_loading_screen_create();
    if (loading_screen) {
      lv_screen_load(loading_screen);
      uint32_t loading_started = SDL_GetTicks();
      uint32_t loading_duration = snapshot_loading ? 200U : 1200U;
      while (SDL_GetTicks() - loading_started < loading_duration) {
        maintain_ambient_sound(audio_device);
        lv_timer_handler();
        SDL_Delay(10);
      }
      if (snapshot_loading) {
        bool saved = save_snapshot(loading_screen, snapshot_path);
        lv_obj_delete(loading_screen);
        SDL_DelEventWatch(watch_sdl_event, NULL);
        return saved ? EXIT_SUCCESS : EXIT_FAILURE;
      }
    }
  }

  yaogui_model_t model;
  mock_backend_t mock;
  yaogui_model_init(&model);
  if (snapshot_path && snapshot_mode && strcmp(snapshot_mode, "ready") == 0) {
    snapshot_now_ms = 0;
  } else if (snapshot_path && snapshot_mode &&
             strcmp(snapshot_mode, "shake") == 0) {
    (void)yaogui_model_start_seeded(&model, 0, UINT32_C(0x51a7cafe));
    snapshot_now_ms = 300;
  } else if (snapshot_path && snapshot_mode &&
             strcmp(snapshot_mode, "reveal") == 0) {
    (void)yaogui_model_start_seeded(&model, 0, UINT32_C(0x51a7cafe));
    snapshot_now_ms = 750;
  } else if (snapshot_path && snapshot_mode &&
             strcmp(snapshot_mode, "dance") == 0) {
    static const uint8_t preview[YAOGUI_SHELL_COUNT] = {0, 1, 0};
    (void)yaogui_model_start_seeded(&model, 0, UINT32_C(0x51a7cafe));
    yaogui_model_complete(&model, preview, YAOGUI_SOURCE_ESP32_RF, 0);
    snapshot_now_ms = 1150;
  } else if (snapshot_path) {
    static const uint8_t preview[YAOGUI_LINE_COUNT][YAOGUI_SHELL_COUNT] = {
        {0, 0, 0},
        {1, 0, 0},
        {1, 1, 0},
        {1, 1, 1},
        {1, 0, 0},
        {1, 1, 0},
    };
    uint32_t preview_ms = 0;
    for (size_t line = 0; line < YAOGUI_LINE_COUNT; line++) {
      (void)yaogui_model_start_seeded(
          &model, preview_ms, UINT32_C(0x51a7cafe) + (uint32_t)line);
      yaogui_model_complete(
          &model, preview[line], YAOGUI_SOURCE_ESP32_RF, preview_ms);
      preview_ms += YAOGUI_MIN_ROLL_MS + YAOGUI_LANDING_TOTAL_MS;
      yaogui_model_tick(&model, preview_ms);
    }
    if (snapshot_mode && strcmp(snapshot_mode, "reading") == 0) {
      (void)yaogui_model_key(
          &model, YAOGUI_KEY_OK, preview_ms, UINT32_C(0x51a7cafe));
    }
  }
  mock_backend_init(&mock);
  yaogui_view_t* view = yaogui_view_create();
  if (!view) {
    fprintf(stderr, "无法创建摇龟视图\n");
    return EXIT_FAILURE;
  }
  lv_screen_load(yaogui_view_screen(view));
  if (loading_screen) lv_obj_delete(loading_screen);
  set_title(display, &mock);

  bool running = true;
  unsigned frame_count = 0;
  while (running) {
    const uint32_t now_ms =
        snapshot_now_ms == UINT32_MAX ? SDL_GetTicks() : snapshot_now_ms;
    const unsigned keys = s_keys;
    s_keys = 0;

    if (keys & KEY_QUIT) {
      running = false;
      break;
    }
    if (keys & KEY_RESET) {
      mock.pending = false;
      yaogui_model_init(&model);
    }
    if (keys & KEY_UP) {
      if (!model.reading.open || !yaogui_view_reading_scroll(view, -1)) {
        (void)yaogui_model_key(&model, YAOGUI_KEY_UP, now_ms, now_ms);
      }
    }
    if (keys & KEY_DOWN) {
      if (!model.reading.open || !yaogui_view_reading_scroll(view, 1)) {
        (void)yaogui_model_key(&model, YAOGUI_KEY_DOWN, now_ms, now_ms);
      }
    }
    if ((keys & KEY_CONFIRM) &&
        yaogui_model_key(&model,
                         YAOGUI_KEY_OK,
                         now_ms,
                         now_ms ^ mock.sequence * 2654435761U)) {
      mock_backend_request(&mock, now_ms);
      play_coin_sound(audio_device);
    }
    if ((keys & KEY_CONFIRM_LONG) &&
        yaogui_model_key(&model,
                         YAOGUI_KEY_OK_LONG,
                         now_ms,
                         now_ms ^ mock.sequence * 2654435761U)) {
      mock_backend_request(&mock, now_ms);
      play_coin_sound(audio_device);
    }
    if (keys & KEY_SCALE_UP) {
      scale += 0.5f;
      if (scale > 4.0f) scale = 4.0f;
      lv_sdl_window_set_zoom(display, scale);
    }
    if (keys & KEY_SCALE_DOWN) {
      scale -= 0.5f;
      if (scale < 1.0f) scale = 1.0f;
      lv_sdl_window_set_zoom(display, scale);
    }

    mock_backend_poll(&mock, &model, now_ms);
    yaogui_model_tick(&model, now_ms);
    maintain_ambient_sound(audio_device);
    const yaogui_view_state_t state = {
        .model = &model,
        .now_ms = now_ms,
        .battery_percent = 86,
    };
    yaogui_view_render(view, &state);
    lv_timer_handler();
    frame_count++;
    if (snapshot_path && frame_count >= 20) {
      if (!save_snapshot(yaogui_view_screen(view), snapshot_path)) {
        fprintf(stderr, "无法保存 LVGL 截图：%s\n", snapshot_path);
        return EXIT_FAILURE;
      }
      snapshot_path = NULL;
      running = false;
    } else if (smoke_test && frame_count >= 20) {
      running = false;
    }
    SDL_Delay(5);
  }

  SDL_DelEventWatch(watch_sdl_event, NULL);
  if (audio_device) SDL_CloseAudioDevice(audio_device);
  yaogui_view_destroy(view);
  lv_deinit();
  return EXIT_SUCCESS;
}
