#!/usr/bin/env python3
"""检查 font_zh_16 / font_zh_24 是否覆盖 ASCII 数字、标点、常用特殊符号。

背景：font_symbols_*.txt 当初只扫了 CJK，ASCII 从来没验过。
LVGL 不会自动回退到别的字体（除非配了 fallback 链），缺的字就是豆腐块。
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from font_glyphs import font_codepoints

BASE = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app"

DIGITS = "0123456789"
PUNCT = ".,:;!?()[]{}<>@#$%^&*+-=/|~_\\\"'"
CJK_PUNCT = "，。、；：！？（）《》【】“”‘’——…·"
SYMBOL = "℃￥°±×÷§†‡•‰′″※①®™€£±≈≤≥√∞"

for name in ("font_zh_16.c", "font_zh_24.c"):
    cps, n = font_codepoints(os.path.join(BASE, name))
    print("===", name, "cmaps=", n, "codepoints=", len(cps))

    def miss(s):
        return "".join(c for c in s if ord(c) not in cps)

    full = [chr(c) for c in range(0x20, 0x7F) if c not in cps]
    print("   ASCII 32..126 缺失 %d 个: %s" % (len(full), "".join(full) if full else "(无)"))
    for label, s in (("数字", DIGITS), ("英标点", PUNCT), ("中文标点", CJK_PUNCT), ("特殊符号", SYMBOL)):
        m = miss(s)
        print("   %-6s 缺失: %s" % (label, m if m else "(无)"))
