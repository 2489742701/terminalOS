# -*- coding: utf-8 -*-
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"
lines = io.open(P, encoding="utf-8", newline="").read().split("\n")

a = next(k for k, l in enumerate(lines) if '[DiagSD] @%u WR: %s' in l)
b = next(k for k, l in enumerate(lines) if 'raw=%u body=%u want=%u' in l)
print("drop", a, "..", b - 1)
for k in range(a, b):
    print("  -", repr(lines[k]))
lines = lines[:a] + lines[b:]
io.open(P, "w", encoding="utf-8", newline="").write("\n".join(lines))
print("done")
