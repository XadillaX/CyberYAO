<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

`fonts/` contains only the four source fonts used by the UI:
`fusion-pixel-12px-proportional-zh_hans.otf`, `xique-guzidian.ttf`, and
`xique-zaishanlin.ttf`, plus `source-han-sans-sc-normal.otf` for the seven
historical glyphs missing from Fusion Pixel.

Fusion Pixel is distributed under the SIL Open Font License 1.1. Its license
and the upstream component-font licenses are retained in
`fonts/licenses/fusion-pixel/`; the Source Han Sans license is retained in
`fonts/licenses/source-han-sans-sc/`. The two Xique fonts are original design
assets provided to this project; downstream distributors must verify their
own redistribution rights.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

## Images

Store only reusable source images in `images/`; generated display assets belong
in the ignored top-level `generated/` directory.

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `audio/`. The current assets
include a looping divination ambience and the shell-shaking, coin-motion, and
landing sound.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- The current BSP audio path uses 32 kHz, 16-bit mono PCM.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## Current assets

| Source | Purpose | Firmware resource |
| --- | --- | --- |
| `images/bagua_table_user.png` | User-selected table artwork | `yaogui_table_image.c` |
| `images/coin_front.png`, `coin_back.png` | Coin faces | `yaogui_coin_images.c` |
| `images/yaogui_shell_faces.jpg` | Turtle-shell source image | `yaogui_shell_images.c` |
| `audio/coin_ritual.wav` | Shaking, rolling, and landing sound | `yaogui_coin_sound.c` |
| `audio/ambient_divination.wav` | Six-second ambient loop | `yaogui_ambient_sound.c` |
| `fonts/fusion-pixel-12px-proportional-zh_hans.otf` | Body text | `yaogui_font_14.c` |
| `fonts/source-han-sans-sc-normal.otf` | Seven historical body-text fallbacks | `yaogui_font_14.c` |
| `fonts/xique-guzidian.ttf` | Classical headings | `yaogui_classic_14.c` |
| `fonts/xique-zaishanlin.ttf` | Hexagram result names | `yaogui_mifu_18.c` |

Install the versions pinned in `package-lock.json` and
`requirements-assets.txt`, then run `python3 tools/generate_assets.py`.
Generated C arrays are written to ignored `generated/` and compiled directly
into firmware and simulator builds. Use `./tools/validate.sh --generated` to
verify that regeneration is deterministic.
