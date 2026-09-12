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
CLASSIC_UI_TEXT = (
    "本卦全文"
    "本卦爻辞"
    "之卦全文"
    "之卦爻辞"
    "用九用六"
    "解读"
    "一二三四五六七八九十第卦变启坛卜"
)
OUTPUTS = (
    "yaogui_font_14.c",
    "yaogui_classic_14.c",
    "yaogui_mifu_18.c",
    "yaogui_clock_28.c",
    "yaogui_standby_display_12.c",
    "yaogui_standby_calendar_10.c",
    "yaogui_standby_pixel_10.c",
    "yaogui_coin_sound.c",
    "yaogui_ambient_sound.c",
    "yaogui_shell_images.c",
    "yaogui_coin_images.c",
    "yaogui_table_image.c",
    "yaogui_standby_images.c",
    "yaogui_calendar_data.c",
)


def validate_generated_manifest() -> None:
    manifest = (
        ROOT / "cmake" / "yaogui_generated_sources.cmake"
    ).read_text(encoding="utf-8")
    cmake_outputs = tuple(
        re.findall(r"^\s+(yaogui_[a-z0-9_]+\.c)\s*$", manifest, re.MULTILINE)
    )
    if cmake_outputs != OUTPUTS:
        raise ValueError(
            "generated source manifest differs from tools/generate_assets.py"
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
    stride = width * (4 if fmt == "ARGB8888" else 1 if fmt == "I8" else 2)
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


def indexed8_shared(images: list[Image.Image]) -> list[bytes]:
    """将同组 RGBA 图像量化到共享的 255 色调色板；索引 0 保留为透明。"""
    images = [image.convert("RGBA") for image in images]
    width = sum(image.width for image in images)
    height = max(image.height for image in images)
    atlas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    offset = 0
    offsets = []
    for image in images:
        atlas.alpha_composite(image, (offset, 0))
        offsets.append(offset)
        offset += image.width

    quantized = atlas.quantize(
        colors=255,
        method=Image.Quantize.FASTOCTREE,
        dither=Image.Dither.NONE,
    )
    rgba_palette = quantized.getpalette("RGBA")
    palette = bytearray(256 * 4)
    for source_index in range(255):
        base = source_index * 4
        red, green, blue, alpha = rgba_palette[base : base + 4]
        target = (source_index + 1) * 4
        palette[target : target + 4] = bytes((blue, green, red, alpha))

    frames = []
    for image, left in zip(images, offsets):
        indexes = bytearray()
        for y in range(image.height):
            for x in range(image.width):
                if image.getpixel((x, y))[3] == 0:
                    indexes.append(0)
                else:
                    indexes.append(quantized.getpixel((left + x, y)) + 1)
        frames.append(bytes(palette + indexes))
    return frames


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
    # 真机不做 LVGL 软件缩放。把 36px 铜钱预先画进 52px 透明帧，
    # 保持原运动坐标和中心点，同时避免每帧 transform_and_recolor。
    coin_frames = []
    for source in coin_source:
        frame = Image.new("RGBA", (52, 52), (0, 0, 0, 0))
        scaled = source.resize((36, 36), Image.Resampling.NEAREST)
        frame.alpha_composite(scaled, (8, 8))
        coin_frames.append(frame)
    coin_indexed = indexed8_shared(coin_frames)
    coin_text = '#include "yaogui_shell_images.h"\n\n'
    coin_text += image_descriptor(
        "yaogui_coin_front", coin_indexed[0], 52, 52, "I8"
    )
    coin_text += "\n" + image_descriptor(
        "yaogui_coin_back", coin_indexed[1], 52, 52, "I8"
    )
    (output_dir / "yaogui_coin_images.c").write_text(coin_text, encoding="utf-8")

    source = Image.open(ASSETS / "images" / "yaogui_shell_faces.jpg")
    # 原运行时比例为 300/256。构建期放大后，真机只移动坐标。
    back = crop_shell(source, (380, 215, 1180, 1225)).resize(
        (61, 75), Image.Resampling.NEAREST
    )
    belly = crop_shell(source, (1370, 215, 2185, 1225)).resize(
        (61, 75), Image.Resampling.NEAREST
    )
    shell_indexed = indexed8_shared([back, belly])
    shell_text = '#include "yaogui_shell_images.h"\n\n'
    shell_text += image_descriptor(
        "yaogui_shell_back", shell_indexed[0], 61, 75, "I8"
    )
    shell_text += "\n" + image_descriptor(
        "yaogui_shell_belly", shell_indexed[1], 61, 75, "I8"
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

    standby_dir = ASSETS / "images" / "standby"
    standby_specs = (
        ("yaogui_sundial_base", "day-sundial-base.png", (167, 184)),
        ("yaogui_sundial_face", "day-sundial-face.png", (167, 184)),
        ("yaogui_sundial_gnomon", "day-sundial-gnomon.png", (167, 184)),
        ("yaogui_clep_pot_ri", "night-clep-pot-ri.png", (30, 27)),
        ("yaogui_clep_pot_yue", "night-clep-pot-yue.png", (33, 27)),
        ("yaogui_clep_pot_xing", "night-clep-pot-xing.png", (34, 24)),
        ("yaogui_clep_pot_shou", "night-clep-pot-shou.png", (39, 40)),
        ("yaogui_clep_arrow", "night-clepsydra-arrow.png", (11, 84)),
    )
    standby_text = '#include "yaogui_standby_images.h"\n\n'
    for symbol, filename, size in standby_specs:
        image = Image.open(standby_dir / filename).convert("RGBA")
        image = image.resize(size, Image.Resampling.NEAREST)
        standby_text += image_descriptor(
            symbol, argb8888(image), image.width, image.height, "ARGB8888"
        )
        standby_text += "\n"

    drop_sheet = Image.open(
        standby_dir / "night-clepsydra-drop-sheet.png"
    ).convert("RGBA")
    for frame in range(2):
        image = drop_sheet.crop((frame * 20, 0, frame * 20 + 20, 28))
        image = image.resize((7, 10), Image.Resampling.NEAREST)
        standby_text += image_descriptor(
            f"yaogui_clep_drop_{frame}",
            argb8888(image),
            image.width,
            image.height,
            "ARGB8888",
        )
        standby_text += "\n"
    (output_dir / "yaogui_standby_images.c").write_text(
        standby_text, encoding="utf-8"
    )


def source_characters() -> str:
    ui_sources = (
        VIEW / "yaogui_view.c",
        VIEW / "yaogui_standby.c",
        VIEW / "yaogui_logic.c",
        VIEW / "yaogui_text_data.c",
        ROOT / "main" / "yaogui_app.c",
        ROOT / "simulator" / "main.c",
    )
    text = "\n".join(
        path.read_text(encoding="utf-8") for path in ui_sources
    )
    return "".join(sorted(set(re.findall(r"[\u3000-\u9fff]", text))))


def standby_characters() -> str:
    sources = (
        VIEW / "yaogui_view.c",
        VIEW / "yaogui_standby.c",
        ROOT / "main" / "yaogui_app.c",
        ROOT / "simulator" / "main.c",
    )
    text = "\n".join(path.read_text(encoding="utf-8") for path in sources)
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
    validate_generated_manifest()
    output_dir.mkdir(parents=True, exist_ok=True)
    write_images(output_dir)
    standby_calendar_charset = output_dir / ".standby-calendar-charset.txt"
    subprocess.run(
        [
            "node",
            str(ROOT / "tools" / "generate_calendar.js"),
            str(output_dir / "yaogui_calendar_data.c"),
            str(standby_calendar_charset),
        ],
        check=True,
    )
    standby_calendar_chars = standby_calendar_charset.read_text(encoding="utf-8")
    standby_calendar_charset.unlink()
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
    calendar_chars = "".join(
        sorted(
            set(
                re.findall(
                    r"[\u3000-\u9fff]",
                    (output_dir / "yaogui_calendar_data.c").read_text(
                        encoding="utf-8"
                    ),
                )
            )
        )
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
        names + "·" + CLASSIC_UI_TEXT,
    )
    write_font(
        output_dir,
        "yaogui_mifu_18.c",
        "xique-zaishanlin.ttf",
        18,
        4,
        names + "之",
    )
    write_font(
        output_dir,
        "yaogui_clock_28.c",
        "xique-wanrenzao.ttf",
        28,
        4,
        "0123456789:-",
    )
    write_font(
        output_dir,
        "yaogui_standby_display_12.c",
        "xique-juzhenti.ttf",
        12,
        4,
        "农历年月闰正一二三四五六七八九十冬腊初廿"
        "甲乙丙丁戊己庚辛壬癸子丑寅卯辰巳午未申酉戌亥等待校时",
    )
    write_font(
        output_dir,
        "yaogui_standby_calendar_10.c",
        "xique-juzhenti.ttf",
        10,
        4,
        standby_calendar_chars,
    )
    write_font(
        output_dir,
        "yaogui_standby_pixel_10.c",
        "fusion-pixel-10px-proportional-zh_hans.otf",
        10,
        1,
        "".join(chr(value) for value in range(32, 127))
        + calendar_chars
        + standby_characters()
        + "·",
        "fusion-pixel-12px-proportional-zh_hans.otf",
        "磉",
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
