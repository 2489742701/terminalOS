# -*- coding: utf-8 -*-
"""拆掉临时诊断（保留 SDCard::selfTest / sdtest 命令）"""
import io

ROOT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"

# ── 1) dom_renderer.cpp: 去掉 [DIAG] 长文本块 ──
P = ROOT + r"\src\browser_engine\src\dom_renderer.cpp"
lines = io.open(P, encoding="utf-8", newline="").read().split("\n")
a = next(k for k, l in enumerate(lines) if "[DIAG] 长文本" in l)
b = next(k for k in range(a, len(lines))
         if "layout_node_create(ELEMENT_SPAN)" in lines[k])
print("dom: drop", a, "..", b - 1)
lines = lines[:a] + lines[b:]
io.open(P, "w", encoding="utf-8", newline="").write("\n".join(lines))

# ── 2) browser_screen.cpp: 去掉 sdPageSave 里的 hash/readback 诊断块 ──
P = ROOT + r"\src\app\browser_screen.cpp"
lines = io.open(P, encoding="utf-8", newline="").read().split("\n")
a = next(k for k, l in enumerate(lines) if "uint32_t hw = 2166136261u;" in l)
b = next(k for k in range(a, len(lines))
         if "firstDiff=%u" in lines[k])          # printf 的最后一行
b = next(k for k in range(b, len(lines)) if lines[k].strip() == "}")  # 收尾的 }
print("app: drop", a, "..", b)
lines = lines[:a] + lines[b + 1:]
io.open(P, "w", encoding="utf-8", newline="").write("\n".join(lines))

print("cleaned")
