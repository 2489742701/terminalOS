#!/usr/bin/env python3
# 拉 RTS/DTR 复位芯片，然后立即捕获启动输出（能抓到启动横幅）。
import serial, serial.tools.list_ports, sys, time

PORT = None
for p in serial.tools.list_ports.comports():
    s = (p.description + (p.hwid or "")).lower()
    if "ch340" in s or "1a86" in s or "7523" in s:
        PORT = p.device
if not PORT:
    print("NO_CH340_FOUND")
    sys.exit(2)

print("PORT=", PORT)
ser = serial.Serial(PORT, 115200, timeout=0.3)
# 复位：DTR/RTS 拉低再放开（多数 CH340 接法下会触发 EN 复位）
ser.setDTR(False)
ser.setRTS(True)
time.sleep(0.1)
ser.setRTS(False)
ser.setDTR(False)
time.sleep(0.2)
ser.reset_input_buffer()

buf = bytearray()
t0 = time.time()
while time.time() - t0 < 20:
    try:
        b = ser.read(2000)
    except Exception as e:
        print("READ_ERR", e)
        break
    if b:
        buf += b
ser.close()

txt = bytes(buf).decode("utf-8", "replace")
lines = [l.strip() for l in txt.splitlines() if l.strip()]
bootrom = ("ESP-ROM", "rst:0x", "boot:0x", "Saved PC", "SPIWP", "mode:DIO",
           "load:0x", "entry 0x", "ets Jun", "Build:Mar")
seen = []
for l in lines:
    if any(k in l for k in bootrom):
        continue
    if l not in seen:
        seen.append(l)
print("=== TOTAL LINES:", len(lines))
print("=== APP LINES (unique):", len(seen))
for l in seen[:60]:
    print("  >", l[:160])
