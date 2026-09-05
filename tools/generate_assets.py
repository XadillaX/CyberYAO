#!/usr/bin/env python3
"""Generate all CyberYAO image, audio, and LVGL font C sources."""

from __future__ import annotations

import argparse
import re
import shutil
import struct
import subprocess
import tempfile
import wave
from collections import deque
from pathlib import Path

from PIL import Image, ImageFilter


ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
GENERATED = ROOT / "generated"
VIEW = ROOT / "components" / "yaogui_view"
FONT_CONV = ROOT / "node_modules" / ".bin" / "lv_font_conv"
UI_SYMBOLS = "·↑↓☰☱☲☳☴☵☶☷（），：；？"
OUTPUTS = (
    "yaogui_font_14.c",
    "yaogui_classic_14.c",
    "yaogui_mifu_18.c",
    "yaogui_coin_sound.c",
    "yaogui_ambient_sound.c",
    "yaogui_shell_images.c",
    "yaogui_coin_images.c",
    "yaogui_table_image.c",
)


def c_bytes(data: bytes, width: int = 16) -> str:
    rows = []
    for offset in range(0, len(data), width):
        values = ", ".join(f"0x{value:02x}" for value in data[offset : offset + width])
        rows.append(f"    {values},")
    return "\n".join(rows)


def c_int16(values: tuple[int, ...], width: int = 16) -> str:
    rows = []
    for offset in range(0, len(values), width):
        rows.append(
            "    " + ", ".join(str(value) for value in values[offset : offset + width]) + ","
        )
    return "\n".join(rows)


def write_audio(source: Path, output: Path, symbol: str, header: str) -> None:
    with wave.open(str(source), "rb") as wav:
        details = (wav.getnchannels(), wav.getsampwidth(), wav.getframerate())
        if details != (1, 2, 32000):
            raise ValueError(f"{source}: expected mono 16-bit 32000 Hz WAV, got {details}")
        frames = wav.readframes(wav.getnframes())
    samples = struct.unpack(f"<{len(frames) // 2}h", frames)
    output.write_text(
        f'#include "{header}"\n\n'
        f"const int16_t {symbol}_pcm[] = {{\n{c_int16(samples)}\n}};\n\n"
        f"const size_t {symbol}_sample_count =\n"
        f"    sizeof({symbol}_pcm) / sizeof({symbol}_pcm[0]);\n",
        encoding="utf-8",
    )


def argb8888(image: Image.Image) -> bytes:
    rgba = image.convert("RGBA")
    data = bytearray()
    for red, green, blue, alpha in rgba.getdata():
        data.extend((blue, green, red, alpha))
    return bytes(data)


def rgb565(image: Image.Image) -> bytes:
    data = bytearray()
    for red, green, blue in image.convert("RGB").getdata():
        value = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        data.extend(struct.pack("<H", value))
    return bytes(data)


def image_descriptor(symbol: str, data: bytes, width: int, height: int, fmt: str) -> str:
    stride = width * (4 if fmt == "ARGB8888" else 2)
    return (
        f"static const uint8_t {symbol}_map[] = {{\n{c_bytes(data)}\n}};\n\n"
        f"const lv_image_dsc_t {symbol} = {{\n"
        "    .header.magic = LV_IMAGE_HEADER_MAGIC,\n"
        f"    .header.cf = LV_COLOR_FORMAT_{fmt},\n"
        "    .header.flags = 0,\n"
        f"    .header.w = {width},\n"
        f"    .header.h = {height},\n"
        f"    .header.stride = {stride},\n"
        f"    .data_size = {len(data)},\n"
        f"    .data = {symbol}_map,\n"
        "};\n"
    )


def transparent_black(image: Image.Image) -> Image.Image:
    image = image.convert("RGBA")
    if image.getchannel("A").getextrema() != (255, 255):
        return image
    alpha = Image.new("L", image.size)
    alpha.putdata([
        max(red, green, blue) if max(red, green, blue) < 48 else 255
        for red, green, blue, _ in image.getdata()
    ])
    image.putalpha(alpha)
    return image


def remove_connected_background(
    image: Image.Image,
    is_background,
) -> Image.Image:
    image = image.convert("RGBA")
    width, height = image.size
    pixels = image.load()
    outside = bytearray(width * height)
    queue: deque[tuple[int, int]] = deque()
    for x in range(width):
        queue.extend(((x, 0), (x, height - 1)))
    for y in range(height):
        queue.extend(((0, y), (width - 1, y)))
    while queue:
        x, y = queue.popleft()
        index = y * width + x
        if outside[index] or not is_background(pixels[x, y]):
            continue
        outside[index] = 1
        red, green, blue, _ = pixels[x, y]
        pixels[x, y] = (red, green, blue, 0)
        if x:
            queue.append((x - 1, y))
        if x + 1 < width:
            queue.append((x + 1, y))
        if y:
            queue.append((x, y - 1))
        if y + 1 < height:
            queue.append((x, y + 1))
    return image


def crop_shell(source: Image.Image, box: tuple[int, int, int, int]) -> Image.Image:
    crop = remove_connected_background(
        source.crop(box),
        lambda pixel: min(pixel[:3]) > 205 and max(pixel[:3]) - min(pixel[:3]) < 35,
    )
    bounds = crop.getchannel("A").getbbox()
    if not bounds:
        raise ValueError(f"no turtle shell detected in crop {box}")
    crop = crop.crop(bounds)
    crop.thumbnail((50, 62), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (52, 64))
    canvas.alpha_composite(crop, ((52 - crop.width) // 2, (64 - crop.height) // 2))
    return canvas


def write_images(output_dir: Path) -> None:
    coin_source = (
        transparent_black(Image.open(ASSETS / "images" / "coin_front.png")),
        transparent_black(Image.open(ASSETS / "images" / "coin_back.png")),
    )
    coin_text = '#include "yaogui_shell_images.h"\n\n'
    coin_text += image_descriptor("yaogui_coin_front", argb8888(coin_source[0]), 52, 52, "ARGB8888")
    coin_text += "\n" + image_descriptor(
        "yaogui_coin_back", argb8888(coin_source[1]), 52, 52, "ARGB8888"
    )
    (output_dir / "yaogui_coin_images.c").write_text(coin_text, encoding="utf-8")

    source = Image.open(ASSETS / "images" / "yaogui_shell_faces.jpg")
    back = crop_shell(source, (380, 215, 1180, 1225))
    belly = crop_shell(source, (1370, 215, 2185, 1225))
    shell_text = '#include "yaogui_shell_images.h"\n\n'
    shell_text += image_descriptor("yaogui_shell_back", argb8888(back), 52, 64, "ARGB8888")
    shell_text += "\n" + image_descriptor(
        "yaogui_shell_belly", argb8888(belly), 52, 64, "ARGB8888"
    )
    (output_dir / "yaogui_shell_images.c").write_text(shell_text, encoding="utf-8")

    source = remove_connected_background(
        Image.open(ASSETS / "images" / "bagua_table_user.png"),
        lambda pixel: max(pixel[:3]) <= 30,
    )
    source = source.resize((214, 214), Image.Resampling.LANCZOS)
    source.putalpha(source.getchannel("A").filter(ImageFilter.GaussianBlur(0.65)))
    background = Image.new("RGBA", source.size, "#e8d7b4")
    background.alpha_composite(source)
    table_data = rgb565(background)
    table_text = '#include "yaogui_table_image.h"\n\n'
    table_text += image_descriptor("yaogui_table_image", table_data, 214, 214, "RGB565")
    (output_dir / "yaogui_table_image.c").write_text(table_text, encoding="utf-8")


def source_characters() -> str:
    text = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (VIEW / "yaogui_view.c", VIEW / "yaogui_logic.c", VIEW / "yaogui_text_data.c")
    )
    return "".join(sorted(set(re.findall(r"[\u3000-\u9fff]", text))))


def hexagram_names() -> str:
    text = (VIEW / "yaogui_text_data.c").read_text(encoding="utf-8")
    names = re.findall(r'^\s*\{\s*\d+,\s*"([^"]+)"', text, flags=re.MULTILINE)
    if len(names) != 64:
        raise ValueError(f"expected 64 hexagram names, got {len(names)}")
    return "".join(sorted(set("".join(names))))


def write_font(
    output_dir: Path,
    filename: str,
    font: str,
    size: int,
    bpp: int,
    symbols: str,
    fallback_font: str | None = None,
    fallback_symbols: str = "",
) -> None:
    primary_symbols = "".join(char for char in symbols if char not in fallback_symbols)
    command = [
        str(FONT_CONV),
        "--font",
        str(ASSETS / "fonts" / font),
        "--size",
        str(size),
        "--bpp",
        str(bpp),
        "--format",
        "lvgl",
        "--lv-include",
        "lvgl.h",
        "--lv-font-name",
        filename.removesuffix(".c"),
        "--symbols",
        primary_symbols,
    ]
    if fallback_font:
        command.extend(
            [
                "--font",
                str(ASSETS / "fonts" / fallback_font),
                "--symbols",
                fallback_symbols,
            ]
        )
    command.extend(["--output", str(output_dir / filename), "--no-compress"])
    subprocess.run(command, check=True)
    generated = output_dir / filename
    text = generated.read_text(encoding="utf-8")
    encoded = {
        chr(int(codepoint, 16))
        for codepoint in re.findall(r"/\* U\+([0-9A-Fa-f]+)", text)
    }
    missing = sorted(set(symbols) - encoded)
    if missing:
        preview = "".join(missing[:32])
        raise ValueError(
            f"{font}: missing {len(missing)} requested glyphs"
            f" (first characters: {preview})"
        )
    text = re.sub(
        r"(?m)^ \* Opts: .*$",
        f" * Source: assets/fonts/{font}"
        + (f" + assets/fonts/{fallback_font}" if fallback_font else "")
        + f"; glyphs: {len(set(symbols))}",
        text,
        count=1,
    )
    generated.write_text(text, encoding="utf-8")


def generate(output_dir: Path) -> None:
    if not FONT_CONV.is_file():
        raise SystemExit("missing node_modules; run `npm ci` first")
    output_dir.mkdir(parents=True, exist_ok=True)
    write_images(output_dir)
    write_audio(
        ASSETS / "audio" / "coin_ritual.wav",
        output_dir / "yaogui_coin_sound.c",
        "yaogui_coin_sound",
        "yaogui_coin_sound.h",
    )
    write_audio(
        ASSETS / "audio" / "ambient_divination.wav",
        output_dir / "yaogui_ambient_sound.c",
        "yaogui_ambient_sound",
        "yaogui_ambient_sound.h",
    )
    all_chars = (
        "".join(chr(value) for value in range(32, 127))
        + source_characters()
        + UI_SYMBOLS
    )
    names = hexagram_names()
    write_font(
        output_dir,
        "yaogui_font_14.c",
        "fusion-pixel-12px-proportional-zh_hans.otf",
        12,
        1,
        all_chars,
        "source-han-sans-sc-normal.otf",
        "窞簋胏虩鞶頄鼫",
    )
    write_font(
        output_dir,
        "yaogui_classic_14.c",
        "xique-guzidian.ttf",
        14,
        4,
        names + "·一二三四五六七八九十第卦本变爻辞启坛卜",
    )
    write_font(
        output_dir,
        "yaogui_mifu_18.c",
        "xique-zaishanlin.ttf",
        18,
        4,
        names + "之",
    )


def check() -> None:
    with tempfile.TemporaryDirectory(prefix="cyberyao-assets-") as temporary:
        candidate = Path(temporary)
        generate(candidate)
        stale = [
            name
            for name in OUTPUTS
            if not (GENERATED / name).is_file()
            or (GENERATED / name).read_bytes() != (candidate / name).read_bytes()
        ]
    if stale:
        raise SystemExit("generated assets are missing or stale: " + ", ".join(stale))
    print("Generated assets: PASS")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check", action="store_true", help="regenerate in a temporary directory and compare"
    )
    args = parser.parse_args()
    if args.check:
        check()
    else:
        if GENERATED.exists():
            shutil.rmtree(GENERATED)
        generate(GENERATED)
        print(f"Generated {len(OUTPUTS)} C sources in {GENERATED.relative_to(ROOT)}/")


if __name__ == "__main__":
    main()
