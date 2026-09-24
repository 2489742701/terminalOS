#!/usr/bin/env python3
"""给字体符号表补齐 ASCII / 数字 / 标点 / 常用符号。

背景（2026-09-23 实锤）：
    font_symbols_16.txt 当初是 fix_font_symbols.py 扫描**源码里的 CJK**生成的，
    ASCII 一个都没扫。结果 font_zh_16 的 0x20..0x7E 全缺 95 个 —— 网页里的
    数字、英文、@ % . / - 全是豆腐块。LVGL 没配 fallback 链，缺字就不会显示。
    font_zh_24 反而有 ASCII（它当初的符号表更全），但缺中文标点 —— 两边互补。

本脚本按"必收集合"求差集后追加，已有的字不会重复。
"""
import os

BASE = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
TOOLS = os.path.join(BASE, "tools")

# 必收：ASCII 可打印区（含数字与英标点）
ASCII = "".join(chr(c) for c in range(0x20, 0x7F))
# 必收：CJK 标点与常用符号
CJK_PUNCT = "，。、；：！？（）《》【】“”‘’——…·　￥"
SYMBOLS = "℃°±×÷§†‡•‰′″※①②③④⑤⑥⑦⑧⑨⑩®©™€£≈≤≥√∞←→↑↓★☆◆●○■□▲▼"

PLAN = [
    ("font_symbols_16.txt", ASCII + CJK_PUNCT + SYMBOLS),
    ("font_symbols_24.txt", ASCII + CJK_PUNCT + SYMBOLS),
]

for fname, required in PLAN:
    path = os.path.join(TOOLS, fname)
    with open(path, "r", encoding="utf-8") as f:
        cur = f.read().strip()
    have = set(cur)
    add = [c for c in required if c not in have]
    if add:
        with open(path, "w", encoding="utf-8") as f:
            f.write(cur + "".join(add))
    print("%-22s %d -> %d  (+%d)" % (fname, len(have), len(have) + len(add), len(add)))
    if add:
        print("   补入: %s" % "".join(add))
