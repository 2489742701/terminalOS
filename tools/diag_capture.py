#!/usr/bin/env python3
# 纯诊断捕获：自动找 CH340 端口，读 20s 启动日志，不发任何命令、不复位。
import serial, serial.tools.list_ports, sys, time

PORT = None
for p in serial.tools.list_ports.comports():
    s = (p.description + p.hwid).lower()
    if "ch340" in s or "1a86" in s.lower() or "7523" in s.lower():
        PORT = p.device
        break
if not PORT:
    # 兜底：列出所有端口让用户看
    print("NO_CH340_FOUND")
    for p in serial.tools.list_ports.comports():
        print("  ", p.device, p.description)
    sys.exit(2)

print("PORT=", PORT)
ser = serial.Serial(PORT, 115200, timeout=0.3)
time.sleep(0.5)
# 不发命令，纯读
buf = bytearray()
t0 = time.time()
while time.time() - t0 < 20:
    try:
        b = ser.read(2000)
    except Exception as e:
        print("READ_ERR", e); break
    if b:
        buf += b

ser.close()
txt = bytes(buf).decode("utf-8", "replace")
lines = txt.splitlines()
printable = [l for l in lines if l.strip()]
print("=== TOTAL LINES:", len(lines), " printable:", len(printable))
print("=== rst:0x count:", sum(1 for l in lines if "rst:0x" in l))
# 提取有意义行
import re
seen = set()
for l in lines:
    ls = l.strip()
    if not ls: continue
    if re.search(r"ESP-ROM|rst:0x|boot:0x|SPIWP|mode:DIO|load:0x|entry 0x|Saved PC|ets Jun", ls):
        continue
    if ls in seen: continue
    seen.add(ls)
    print("APP>", ls[:200])
print("=== APP-LIKE LINES:", len(seen))
