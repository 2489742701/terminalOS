#!/usr/bin/env python3
"""解析 LVGL 生成的字体 .c，还原真正烧进固体的码点集合。

坑：LV_FONT_FMT_TXT_CMAP_SPARSE_TINY 的 unicode_list 里存的不是绝对码点，
而是「相对 range_start 的偏移」。直接把 0x4d49 当成 U+4D49 会得出完全错误的
结论（比如「的」被判成缺字）。这里按 cmap 表把偏移还原成真实码点。
"""
import re, sys


def font_codepoints(path):
    s = open(path, "r", encoding="utf-8", errors="ignore").read()

    # 1) 抓出所有 unicode_list_N 的原始数组
    lists = {}
    for m in re.finditer(r"static const (?:uint16_t|uint32_t) unicode_list_(\d+)\[\]\s*=\s*\{(.*?)\}", s, re.S):
        vals = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]+)", m.group(2))]
        lists[m.group(1)] = vals

    # 2) 抓出 cmap 表：range_start / range_length / .unicode_list / list_length / type
    cps = set()
    cmaps = re.findall(
        r"\.range_start\s*=\s*(\d+)\s*,\s*\.range_length\s*=\s*(\d+)\s*,\s*\.glyph_id_start\s*=\s*(\d+)\s*,\s*"
        r"\.unicode_list\s*=\s*(unicode_list_\d+|NULL)\s*,\s*\.glyph_id_ofs_list\s*=\s*(\w+)\s*,\s*"
        r"\.list_length\s*=\s*(\d+)\s*,\s*\.type\s*=\s*(\w+)", s)
    if not cmaps:
        print("!! no cmap parsed from", path)
    for rs, rl, gid, ulist, ofs, ll, typ in cmaps:
        rs, rl, ll = int(rs), int(rl), int(ll)
        if ulist != "NULL":
            key = ulist.split("_")[-1]
            for off in lists.get(key, []):
                cps.add(rs + off)
        else:
            # 连续区间：整段都覆盖
            for c in range(rs, rs + rl):
                cps.add(c)
    return cps, len(cmaps)


if __name__ == "__main__":
    for name in ("font_zh_16.c", "font_zh_24.c"):
        p = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app" + "\\" + name
        cps, n = font_codepoints(p)
        print("===", name, "cmaps=", n, "codepoints=", len(cps))
        for ch in "的了一不是在中渲浏浏览器":
            print("   ", ch, hex(ord(ch)), ord(ch) in cps)
