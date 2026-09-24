#!/usr/bin/env python3
"""不复位，直接连上已在运行的设备，下发若干串口命令并捕获输出。

用法: cap_cmd.py <秒数> "<命令1>" "<命令2>" ...
命令之间自动间隔 CMD_GAP 秒。用于验证浏览器加载（耗时几十秒）。
"""
import serial, serial.tools.list_ports, sys, time

SECS = float(sys.argv[1]) if len(sys.argv) > 1 else 90.0
CMDS = sys.argv[2:]
CMD_AT = 2.0
CMD_GAP = 3.0

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
ser.setRTS(False)     # 不复位
ser.reset_input_buffer()

t0 = time.time()
next_cmd = CMD_AT
queue = list(CMDS)
while time.time() - t0 < SECS:
    if queue and (time.time() - t0) > next_cmd:
        c = queue.pop(0)
        ser.write((c + "\n").encode())
        print(">>> sent:", c, flush=True)
        next_cmd = (time.time() - t0) + CMD_GAP
    try:
        b = ser.read(4096)
    except Exception as e:
        print("READ_ERR", e)
        break
    if b:
        sys.stdout.write(b.decode("utf-8", "replace"))
        sys.stdout.flush()
ser.close()
