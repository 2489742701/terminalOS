#!/usr/bin/env python3
"""扫描源码里出现的 CJK 字符，对比字体符号表，列出缺字。"""
import os, re, sys, io

ROOT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src"
SYMS = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\zh_symbols_v2.txt"

with open(SYMS, "r", encoding="utf-8") as f:
    have = set(f.read().strip())

used = {}
cjk = re.compile(r"[\u2e80-\u9fff\uf900-\ufaff\uff00-\uffef]")
for dirpath, dirnames, filenames in os.walk(ROOT):
    for fn in filenames:
        if not fn.endswith((".cpp", ".h", ".c", ".hpp")):
            continue
        p = os.path.join(dirpath, fn)
        try:
            with open(p, "r", encoding="utf-8", errors="ignore") as f:
                txt = f.read()
        except Exception:
            continue
        for ch in cjk.findall(txt):
            used.setdefault(ch, set()).add(os.path.relpath(p, ROOT))

missing = sorted(c for c in used if c not in have)
out = []
out.append("symbols in table: %d" % len(have))
out.append("distinct CJK used in src: %d" % len(used))
out.append("MISSING (%d): %s" % (len(missing), "".join(missing)))
for c in missing:
    out.append("  U+%04X %s   <- %s" % (ord(c), c, ", ".join(sorted(used[c])[:4])))
sys.stdout.write("\n".join(out) + "\n")
