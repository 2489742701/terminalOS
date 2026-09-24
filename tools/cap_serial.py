#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
纯被动串口抓日志 —— 用于"路上崩了，串口应该有记录"这类现场。

关键：不拨 DTR/RTS（ESP32 的 EN/BOOT 就挂在这两根上，一拨就会复位/进下载模式，
现场就没了）。PySerial 打开时保持默认电平即可。
"""
import io
import sys
import time

try:
    import serial
except ImportError:
    print("ERROR: pyserial missing")
    sys.exit(1)

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
OUT = sys.argv[2] if len(sys.argv) > 2 else "cap_serial.txt"
SECONDS = float(sys.argv[3]) if len(sys.argv) > 3 else 25.0
PROBE = (len(sys.argv) > 4 and sys.argv[4] == "probe")

lines = []


def dec(b):
    for enc in ("utf-8", "gb18030"):
        try:
            t = b.decode(enc)
            if t.count("\ufffd") == 0:
                return t
        except Exception:
            pass
    return b.decode("utf-8", "replace")


try:
    s = serial.Serial(PORT, 115200, timeout=0.4)
except Exception as e:
    print("串口打开失败: %r" % (e,))
    sys.exit(1)

time.sleep(0.2)
s.reset_input_buffer()
print("=== 被动监听 %s %.0f 秒（不打扰现场）===" % (PORT, SECONDS))
end = time.time() + SECONDS
got = 0
while time.time() < end:
    line = s.readline()
    if line:
        t = dec(line).rstrip("\r\n")
        lines.append(t)
        got += 1
        print("  " + t[:190])
    # 读到 EOF 也不会 break：一直听到时间到

# 完全没有输出 -> 探一下活着没（这时才敢发东西）
if got == 0 and PROBE:
    print("--- 无输出，试探设备是否存活 ---")
    s.write(b"\n")
    s.flush()
    time.sleep(1.5)
    end2 = time.time() + 3
    while time.time() < end2:
        line = s.readline()
        if line:
            t = dec(line).rstrip("\r\n")
            lines.append(t)
            got += 1
            print("  " + t[:190])
    if got == 0:
        print("  !! 设备无响应（可能卡死或串口异常）")

s.close()
io.open(OUT, "w", encoding="utf-8", newline="\r\n").write("\n".join(lines) + "\n")
print("\n共 %d 行 -> %s" % (len(lines), OUT))
