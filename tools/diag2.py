#!/usr/bin/env python3
# 细诊断：打印前 50 行原始、复位原因分布、load 行、异常关键字。
import serial, serial.tools.list_ports, sys, time, re
from collections import Counter

PORT = None
for p in serial.tools.list_ports.comports():
    s = (p.description + p.hwid).lower()
    if "ch340" in s or "1a86" in s.lower() or "7523" in s.lower():
        PORT = p.device
if not PORT:
    print("NO_CH340_FOUND"); sys.exit(2)
print("PORT=", PORT)
ser = serial.Serial(PORT, 115200, timeout=0.3)
time.sleep(0.5)
buf = bytearray()
t0 = time.time()
while time.time() - t0 < 18:
    try:
        b = ser.read(2000)
    except Exception as e:
        print("READ_ERR", e); break
    if b: buf += b
ser.close()
txt = bytes(buf).decode("utf-8", "replace")
lines = txt.splitlines()

print("=== FIRST 50 RAW LINES ===")
for l in lines[:50]:
    print("  ", l)

rc = Counter()
loadlines = []
exclines = []
for l in lines:
    m = re.search(r"rst:0x([0-9a-f]+)[^\)]*\)", l)
    if m: rc[m.group(0)] += 1
    if "load:0x" in l: loadlines.append(l.strip())
    if re.search(r"guru|backtrace|abort|panic|invalid|corrupt|watchdog|assert|exception", l, re.I):
        exclines.append(l.strip())

print("=== RESET CAUSE DISTRIBUTION ===")
for k,v in rc.most_common():
    print("  ", k, "x", v)
print("=== LOAD LINES (unique) ===")
for l in dict.fromkeys(loadlines):
    print("  ", l)
print("=== EXCEPTION/KEYWORD LINES ===")
for l in exclines[:20]:
    print("  ", l)
print("=== TOTAL lines:", len(lines))
