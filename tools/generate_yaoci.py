#!/usr/bin/env python3
"""从保存的 64 个《周易》页面生成固件只读 C 数据；不补写缺失内容。"""

from __future__ import annotations

import argparse
import hashlib
import html
import re
from pathlib import Path

NAMES = (
    "乾 坤 屯 蒙 需 讼 师 比 小畜 履 泰 否 同人 大有 谦 豫 随 蛊 临 观 "
    "噬嗑 贲 剥 复 无妄 大畜 颐 大过 坎 离 咸 恒 遁 大壮 晋 明夷 家人 "
    "睽 蹇 解 损 益 夬 姤 萃 升 困 井 革 鼎 震 艮 渐 归妹 丰 旅 巽 兑 "
    "涣 节 中孚 小过 既济 未济"
).split()


def plain(fragment: str) -> str:
    return html.unescape(re.sub(r"<[^>]+>", "", fragment)).strip()


def one(pattern: str, source: str, label: str) -> str:
    matches = re.findall(pattern, source)
    if len(matches) != 1:
        raise ValueError(f"{label}: 期望 1 项，实际 {len(matches)} 项")
    return plain(matches[0])


def c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def parse_page(path: Path, number: int) -> tuple[str, list[str], str | None]:
    source = path.read_text(encoding="utf-8")
    guaci = one(
        r'<section class="zhouyi-card__primary"><div><h3>卦辞</h3><p>(.*?)</p>',
        source, f"{path.name} 卦辞")
    lines = [
        plain(value) for value in re.findall(
            r'<p class="zhouyi-card__yao-text">(.*?)</p>', source)
    ]
    if len(lines) != 6:
        raise ValueError(f"{path.name} 爻辞: 期望 6 项，实际 {len(lines)} 项")
    specials = re.findall(
        r'<div class="zhouyi-card__special-line"><strong>(.*?)</strong>', source)
    expected = 1 if number in (1, 2) else 0
    if len(specials) != expected:
        raise ValueError(
            f"{path.name} 用九/用六: 期望 {expected} 项，实际 {len(specials)} 项")
    return guaci, lines, plain(specials[0]) if specials else None


def generate(source_dir: Path) -> str:
    pages = []
    digest = hashlib.sha256()
    for number in range(1, 65):
        path = source_dir / f"{number}.html"
        raw = path.read_bytes()
        digest.update(number.to_bytes(1, "big"))
        digest.update(raw)
        pages.append(parse_page(path, number))

    out = [
        "/* 由 tools/generate_yaoci.py 从 guaci-pages/1.html..64.html 生成。 */",
        f"/* 源文件有序 SHA-256: {digest.hexdigest()} */",
        '#include "yaogui_logic.h"',
        "",
        "const yaogui_hexagram_t YAOGUI_HEXAGRAMS[64] = {",
    ]
    for index, (guaci, lines, special) in enumerate(pages):
        out.append(f"    {{ {index + 1}, {c_string(NAMES[index])},")
        out.append(f"      {c_string(guaci)},")
        out.append("      {")
        out.extend(f"          {c_string(line)}," for line in lines)
        out.append("      },")
        out.append(f"      {c_string(special) if special else 'NULL'} }},")
    out.extend(("};", ""))
    return "\n".join(out)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_text(generate(args.source_dir), encoding="utf-8")


if __name__ == "__main__":
    main()
