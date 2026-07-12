#!/usr/bin/env python3
"""将五笔码表 (编码 词条1 词条2 ...) 转为 Sample IME 词典格式 ("编码"="词条")."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def convert_line(line: str) -> list[str]:
    """解析一行五笔码表, 返回 IME 格式的多行输出."""
    line = line.strip().lstrip("\ufeff")
    if not line:
        return []

    parts = line.split()
    if len(parts) < 2:
        return []

    code = parts[0].upper()
    return [f'"{code}"="{word}"' for word in parts[1:]]


def detect_encoding(path: Path) -> str:
    """根据 BOM 检测词典文件编码 (Sample IME 词典通常为 UTF-16 LE)."""
    with path.open("rb") as f:
        bom = f.read(4)
    if bom.startswith(b"\xff\xfe\x00\x00"):
        return "utf-32-le"
    if bom.startswith(b"\xfe\xff\x00\x00"):
        return "utf-32-be"
    if bom.startswith(b"\xff\xfe"):
        return "utf-16"  # 自动剥离 BOM
    if bom.startswith(b"\xfe\xff"):
        return "utf-16"
    if bom.startswith(b"\xef\xbb\xbf"):
        return "utf-8-sig"
    return "utf-8"


def convert_file(input_path: Path, output_path: Path, encoding: str = "utf-16") -> int:
    """转换整个码表文件, 返回写入的词条数."""
    input_encoding = detect_encoding(input_path)
    count = 0
    with input_path.open(encoding=input_encoding) as fin, output_path.open(
        "w", encoding=encoding, newline="\n"
    ) as fout:
        for line in fin:
            entries = convert_line(line)
            for entry in entries:
                fout.write(entry + "\n")
                count += 1
    return count


def main() -> int:
    parser = argparse.ArgumentParser(
        description="将五笔码表转为 Sample IME 词典格式"
    )
    parser.add_argument(
        "input",
        type=Path,
        nargs="?",
        default=Path("Dictionary/小鸭五笔-所有字词(五笔(98)).txt"),
        help="输入五笔码表路径",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="输出 IME 词典路径 (默认: 输入文件名 + .ime.txt)",
    )
    parser.add_argument(
        "--encoding",
        default="utf-16",
        help="输出文件编码 (默认: utf-16, 与 Sample IME 词典一致)",
    )
    args = parser.parse_args()

    input_path: Path = args.input
    if not input_path.is_file():
        print(f"错误: 找不到输入文件 {input_path}", file=sys.stderr)
        return 1

    output_path = args.output or input_path.with_suffix(input_path.suffix + ".ime.txt")

    count = convert_file(input_path, output_path, encoding=args.encoding)
    print(f"已写入 {count} 条词条 -> {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
