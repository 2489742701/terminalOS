#!/usr/bin/env python3
"""打开浏览器加载一个 URL，并在渲染完成后继续观察若干秒，验证不会崩。

用法: cap_url.py <总秒数> <url> [渲染后再等待秒数]

流程：
  t=2s   下发 browser <url>
  t=2s+N 下发 mem（N 默认 45s，足够跑完下载+解析+渲染，并跨过欢迎屏 3s 定时器）
最后把整段串口输出原样打到 stdout（由调用方重定向落盘）。
"""
import serial
import serial.tools.list_ports
import sys
import time

SECS = float(sys.argv[1]) if len(sys.argv) > 1 else 90.0
URL = sys.argv[2] if len(sys.argv) > 2 else "https://m.baidu.com/"
GAP = float(sys.argv[3]) if len(sys.argv) > 3 else 45.0

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
ser.setRTS(False)          # 不复位
ser.reset_input_buffer()

t0 = time.time()
schedule = [(2.0, "browser " + URL), (2.0 + GAP, "mem")]
i = 0
while time.time() - t0 < SECS:
    if i < len(schedule) and (time.time() - t0) > schedule[i][0]:
        ser.write((schedule[i][1] + "\n").encode())
        print(">>> sent:", schedule[i][1], flush=True)
        i += 1
    try:
        b = ser.read(4096)
    except Exception as e:
        print("READ_ERR", e)
        break
    if b:
        sys.stdout.write(b.decode("utf-8", "replace"))
        sys.stdout.flush()
ser.close()
