<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# CyberYAO

CyberYAO is an offline six-line I Ching application for the FoloToy AI
Passport. It casts three coins six times, builds the hexagram from the bottom
line upward, identifies changing lines, and presents the resulting hexagram.
The application includes all 64 judgments, 384 line texts, Qian's Use of Nine,
and Kun's Use of Six.

<p align="center">
  <img src="docs/assets/screenshots/cyberyao-ready.png" width="220" alt="Ready screen">
  <img src="docs/assets/screenshots/cyberyao-cast.png" width="220" alt="Coin animation">
  <img src="docs/assets/screenshots/cyberyao-reading.png" width="220" alt="Full reading">
</p>

## Features

- Fully offline randomness from `esp_fill_random()` with ESP32-C3 radio entropy.
- No network interface, scan, connection, credentials, or remote fallback.
- Traditional three-coin values: inscribed side is two, reverse side is three.
- Six casts arranged from the initial line upward.
- Pseudo-3D shell and coin motion with randomized paths, landing points, and rotation.
- Original and changed hexagrams with complete judgments and all six line texts.
- Primary-reading markers based on Zhu Xi's moving-line rules.
- One scrollable document per hexagram instead of one page per line.
- A quiet ambient divination loop that yields to synchronized coin sounds.
- Shared C, LVGL, font, image, and audio resources between firmware and simulator.

## Controls

| Input | Action |
| --- | --- |
| Short `OK` | Cast the next line; open the full reading after six casts |
| `UP` / `DOWN` | Scroll a reading; switch hexagrams at a document boundary |
| Short `OK` in reading | Return to the result overview |
| Long `OK` | Clear the result and begin a new hexagram |

## Randomness

Each cast obtains three independent random bits. The inscribed side contributes
two and the reverse side contributes three.

| Total | Line | Change |
| --- | --- | --- |
| 6 | Old Yin | Yin to Yang |
| 7 | Young Yang | None |
| 8 | Young Yin | None |
| 9 | Old Yang | Yang to Yin |

Each cast briefly starts the Wi-Fi radio, waits for entropy stabilization, reads
the hardware RNG, then stops and deinitializes the radio. Any lifecycle failure
opens an error screen; the application never falls back to pseudo-random or
online services.

## Repository layout

```text
components/bsp/          AI Passport board support
components/yaogui_view/  Divination logic, LVGL UI, and generated resources
main/                    Firmware entry point and hardware tasks
simulator/               Native macOS LVGL + SDL2 simulator
assets/                  Editable image and audio sources
tests/                   Host logic and firmware verification
tools/                   Resource generation and validation scripts
docs/                    Development, hardware, and release documentation
```

## Build

The firmware uses ESP-IDF `v5.5.3`:

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/cu.usbmodem2101 flash monitor
```

The macOS simulator requires SDL2:

```bash
brew install sdl2 pkg-config
./simulator/build.sh
./simulator/run.sh
```

Simulator controls are the arrow keys, `Enter`, `L` for long OK, `Esc` to reset,
and `Q` to quit.

## Validation

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
./simulator/run.sh --smoke-test
```

Host tests cover coin-to-line mapping, six-line state transitions, all 64
hexagram mappings, Zhu Xi primary-reading rules, deterministic motion, and full
reading documents. Radio entropy and audio hardware behavior still require
validation on an AI Passport device.

## Upstream and license

CyberYAO is based on
[FoloToy/ai-passport](https://github.com/folotoy/ai-passport) and retains its
board support, build pipeline, and hardware documentation. The code is released
under the repository's [LICENSE](LICENSE).
