#!/usr/bin/env python3
"""复位并捕获 90 秒串口输出，中途下发 `bat` 命令做电源 IC 探测。

抓的是完整流水（不去重、不截断），落盘由调用方重定向。
浏览器首屏加载要几十秒，所以默认窗口给到 90s。
"""
import serial, serial.tools.list_ports, sys, time

SECS = float(sys.argv[1]) if len(sys.argv) > 1 else 90.0
CMD_AT = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0   # 上电后多久发命令

PORT = None
for p in serial.tools.list_ports.comports():
    s = (p.description + (p.hwid or "")).lower()
    if "ch340" in s or "1a86" in s or "7523" in s:
        PORT = p.device
if not PORT:
    print("NO_CH340_FOUND")
    sys.exit(2)
print("PORT=", PORT, flush=True)

ser = serial.Serial(PORT, 115200, timeout=0.3)
ser.setDTR(False)
ser.setRTS(True)
time.sleep(0.1)
ser.setRTS(False)
ser.setDTR(False)
time.sleep(0.2)
ser.reset_input_buffer()

t0 = time.time()
sent = False
while time.time() - t0 < SECS:
    if not sent and (time.time() - t0) > CMD_AT:
        ser.write(b"bat\n")
        print(">>> sent: bat", flush=True)
        sent = True
    try:
        b = ser.read(4096)
    except Exception as e:
        print("READ_ERR", e)
        break
    if b:
        sys.stdout.write(b.decode("utf-8", "replace"))
        sys.stdout.flush()
ser.close()
