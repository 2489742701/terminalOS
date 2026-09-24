"""把图标 canvas 缓冲从 PSRAM 搬进内部 DRAM。

实测（2026-09-24，1bpp 字体已上）：perfx 差分显示桌面 draw 30048us 里
**11 个 canvas 图标吃掉 7962us（26%）**。每个图标只画 22x22=484 px，
换算 1488 ns/px —— 比文字还慢。根因是缓冲分配在 PSRAM
（heap_caps_malloc(MALLOC_CAP_SPIRAM)）：绘制时每像素都要走一次 OPI 片外读。

11 x 22 x 22 x 2B = 968 B，内部 DRAM 完全装得下。
改成优先 MALLOC_CAP_INTERNAL，分配失败才回退 PSRAM（不引入新的失败模式）。
"""
import io
import os
import sys

P = os.path.join(r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040",
                 "geek-terminal", "src", "app", "icons.cpp")

s = io.open(P, "r", encoding="utf-8", newline="").read()

OLD = "void* buf = heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);"
NEW = ("/* 图标缓冲放内部 DRAM：PSRAM 每像素一次片外读，实测 11 个图标吃掉\n"
       "   draw 的 26%（1488 ns/px）。总共才 ~1KB，内部 DRAM 装得下。\n"
       "   分配失败才回退 PSRAM，不引入新的失败模式。 */\n"
       "  void* buf = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL);\n"
       "  if (!buf) buf = heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);")

n = s.count(OLD)
if n == 0:
    print("ALREADY PATCHED or MISS")
    sys.exit(0 if "MALLOC_CAP_INTERNAL" in s else 1)

print("found %d site(s)" % n)
s = s.replace(OLD, NEW)
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("WROTE", P)
