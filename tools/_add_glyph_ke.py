# -*- coding: utf-8 -*-
"""把「氪」补进 16px 字表（36氪 的"氪"原来是方框 —— 字库里根本没有这个字）。

只补 16px：新闻 chip 用的就是 16px。24px 那份缺的字更多（金/土/海/微…），
但 24px 只用在天气页的大标题和描述上，暂不一起动 —— 加字会涨 Flash。
"""
import io, os, subprocess, sys

BASE = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
SYM = os.path.join(BASE, "tools", "font_symbols_16.txt")

s = io.open(SYM, encoding="utf-8").read().strip()
if "氪" not in s:
    io.open(SYM, "w", encoding="utf-8", newline="").write(s + "氪")
    print("added 氪 ->", len(s) + 1, "symbols")
else:
    print("already has 氪")

# 顺手把同类的气体用字一起补上（都是 1 个字的事，免得下次又缺）
for c in "氙氡氩":
    if c not in s:
        s2 = io.open(SYM, encoding="utf-8").read().strip()
        io.open(SYM, "w", encoding="utf-8", newline="").write(s2 + c)
        print("added", c)

print("now:", len(io.open(SYM, encoding="utf-8").read().strip()))
