# Yaogui macOS LVGL + SDL2 Simulator

[简体中文](README.zh_CN.md)

The simulator builds the repository's `managed_components/lvgl__lvgl` and
shares pixel UI helpers, Chinese font configuration, turtle-shell and coin images,
audio assets, `yaogui_logic`, view creation, and rendering with firmware through
`components/yaogui_view`. It contains no ESP-IDF radio implementation.

## Build and run

```sh
brew install sdl2 pkg-config
./simulator/build.sh
./simulator/run.sh
```

The 240x320 logical canvas starts at 2x. Use
`./simulator/run.sh --scale 1.5` to choose an initial 1x–4x scale.

## Keyboard

- `Up` / `Down`: scroll a reading and switch hexagrams at a document boundary.
- `Enter` / `Space`: confirm and roll.
- `L`: simulate a long OK press and begin a new hexagram.
- `+` / `-`: scale the window.
- `Esc`: reset.
- `Q`: quit.

The mock responds after 0.9–1.8 seconds. The simulator continuously plays the
same ambient loop as firmware, replaces it with the coin sound during a cast,
then resumes the loop.

## Scope

The simulator validates actual LVGL rendering, animation, and keyboard input.
It does not emulate the ESP32-C3 Wi-Fi radio, button ADC, or physical display
color. Firmware uses radio-backed `esp_fill_random` and reports lifecycle
failures without fallback.
