#!/usr/bin/env python3
"""重新生成中文字体（16px / 24px）。

符号表由 tools/fix_font_symbols.py 生成：
    font_symbols_16.txt  3785 字（原 3780 + 「」浏渲 等补字）
    font_symbols_24.txt   996 字（原 657 + 源码里用到但 24px 缺的 339 字）

坑记录：
1. 码点必须从 cmap 还原（unicode_list 存的是相对 range_start 的偏移），
   直接读会得出"的/了/一都缺字"的荒谬结论 —— 见 tools/font_glyphs.py。
2. node 必须给绝对路径，bash shim 的 PATH 是坏的。
3. ⛔ 必须带 --no-compress（2026-09-24 实测）：
   压缩字体走 lv_font_get_bitmap_fmt_txt 的 RLE 解压分支，每次取字形位图
   都要 decompress() 一遍 —— 实测 132 us/字，占汉字渲染成本的 44%，
   而中文点阵 RLE 几乎压不动（体积只省 ~2%）。
   不压缩 -> bitmap_format=0(PLAIN) -> 直接返回 &glyph_bitmap[bitmap_index]，
   零解压成本，观感完全不变。
4. ⛔ 生成产物里 .fallback 会被重置成 NULL。目前两个字体本来就是 NULL
   （英文/数字走 montserrat 是在别处处理的），若将来重新注入 fallback，
   生成后必须再跑一次注入脚本。
5. --bpp 1（2026-09-24，master 拍板"抗锯齿无所谓"）：
   4bpp 每字要解包成 mask（30us/字），1bpp 直接按位展开；
   且 mask 值只有 0/255，能命中 FILL_NORMAL_MASK_PX 的 COVER 快路径。
   代价：字形无抗锯齿、边缘变硬。
"""

BPP = "1"
import subprocess, sys, os

NODE = r"C:\Users\longyaosi\.workbuddy\binaries\node\versions\22.22.2-3\node.exe"
CONV = os.path.join(os.environ.get("APPDATA", ""), r"npm\node_modules\lv_font_conv\lv_font_conv.js")
TTF = r"C:\Windows\Fonts\simhei.ttf"
BASE = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
TOOLS = os.path.join(BASE, "tools")
APP = os.path.join(BASE, "src", "app")

jobs = [
    (16, "font_symbols_16.txt", os.path.join(APP, "font_zh_16.c")),
    (24, "font_symbols_24.txt", os.path.join(APP, "font_zh_24.c")),
]

for size, symfile, outpath in jobs:
    with open(os.path.join(TOOLS, symfile), "r", encoding="utf-8") as f:
        sym = f.read().strip()
    print("generating %dpx, %d symbols -> %s" % (size, len(sym), os.path.basename(outpath)))
    cmd = [NODE, CONV, "--font", TTF, "--size", str(size), "--bpp", BPP,
           "--format", "lvgl", "--no-compress", "--output", outpath,
           "--symbols", sym]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="ignore")
    if r.returncode != 0:
        print("ERROR:\n", r.stdout[-3000:], "\n", r.stderr[-3000:])
        sys.exit(1)
    print("  OK  %.1f KB" % (os.path.getsize(outpath) / 1024.0))

print("Done!")
