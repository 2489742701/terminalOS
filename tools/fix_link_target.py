# -*- coding: utf-8 -*-
"""胶囊(chip)分支还在使用旧的 href_path 优先逻辑 —— 那次 Edit 静默失败了。
统一走 flat_link_target()（优先绝对 URL href_resolved）。"""
import sys

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\layout_engine.cpp"

raw = open(P, encoding="utf-8", newline="").read()
nl = "\r\n" if "\r\n" in raw else "\n"
lines = raw.split(nl)

old = [
    "            const char *link_target =",
    "                node->href_path",
    "                    ? node->href_path",
    "                    : (node->href_resolved ? node->href_resolved : node->href);",
]
new = ["            const char *link_target = flat_link_target(node);"]

idx = -1
for i in range(len(lines) - len(old) + 1):
    if lines[i:i + len(old)] == old:
        idx = i
        break
print("found at", idx)
if idx < 0:
    print("PATTERN NOT FOUND")
    sys.exit(1)

lines[idx:idx + len(old)] = new
open(P, "w", encoding="utf-8", newline="").write(nl.join(lines))

chk = open(P, encoding="utf-8", errors="replace").read()
print("href_path-first gone:", "? node->href_path" not in chk)
print("flat_link_target calls:", chk.count("flat_link_target(node)"))

b = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\probe_bing3.txt"
t2 = open(b, encoding="utf-8", errors="replace").read()
print("probe had 'search home shown':", "search home shown" in t2)
