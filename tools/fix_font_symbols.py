#!/usr/bin/env python3
"""扫描源码用到的字，对比字体实际码点，生成补全后的符号表。

注意：码点必须从 cmap 还原（见 font_glyphs.py），不能直接读 unicode_list。
"""
import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from font_glyphs import font_codepoints

TOOLS = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools"
SRC = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src"
APP = os.path.join(SRC, "app")


def header_symbols(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        head = f.read(200000)
    end = head.find("*/")
    m = re.search(r"--symbols\s*(.*)", head[:end if end > 0 else 200000], re.S)
    if not m:
        return ""
    return re.sub(r"\n\s*\*\s*", "", m.group(1)).strip()


def src_cjk():
    cjk = re.compile(r"[\u2e80-\u9fff\uf900-\ufaff\uff00-\uffef\u3000-\u303f]")
    used = {}
    for dirpath, _dn, filenames in os.walk(SRC):
        for fn in filenames:
            if not fn.endswith((".cpp", ".h", ".c", ".hpp")):
                continue
            if fn.startswith("font_zh_"):
                continue
            p = os.path.join(dirpath, fn)
            try:
                with open(p, "r", encoding="utf-8", errors="ignore") as f:
                    for ch in cjk.findall(f.read()):
                        used.setdefault(ch, set()).add(os.path.relpath(p, SRC))
            except Exception:
                pass
    return used


used = src_cjk()
base16 = open(os.path.join(TOOLS, "font_symbols.txt"), "r", encoding="utf-8").read().strip()
sym24 = header_symbols(os.path.join(APP, "font_zh_24.c"))

g16, _ = font_codepoints(os.path.join(APP, "font_zh_16.c"))
g24, _ = font_codepoints(os.path.join(APP, "font_zh_24.c"))

miss16 = sorted([c for c in used if ord(c) not in g16])
miss24 = sorted([c for c in used if ord(c) not in g24])

print("src distinct CJK     :", len(used))
print("font_zh_16 codepoints:", len(g16), " 缺:", "".join(miss16) or "(none)")
print("font_zh_24 codepoints:", len(g24), " 缺:", "".join(miss24) or "(none)")
print("base16 file chars    :", len(set(base16)))
print("sym24 header chars   :", len(set(sym24)))

new16 = list(dict.fromkeys(list(base16) + miss16))
new24 = list(dict.fromkeys(list(sym24) + miss24))
open(os.path.join(TOOLS, "font_symbols_16.txt"), "w", encoding="utf-8").write("".join(new16))
open(os.path.join(TOOLS, "font_symbols_24.txt"), "w", encoding="utf-8").write("".join(new24))
print("new16:", len(new16), "(+%d)" % (len(new16) - len(set(base16))))
print("new24:", len(new24), "(+%d)" % (len(new24) - len(set(sym24))))
for c in miss16[:40]:
    print("   补 16px:", hex(ord(c)), c, " <- ", ", ".join(sorted(used[c])[:2]))
