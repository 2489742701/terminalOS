#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""串口跑 news 命令，验证设备端能不能真拉到热点新闻。

用法：<py> tools/probe_news.py [platform ...]
需要 pyserial：用 C:\\Users\\longyaosi\\python-sdk\\python3.13.2\\python.exe 跑。

两个坑（都踩过）：
  1. 必须显式放开 DTR/RTS —— 否则 CH340 把 EN 拉住，串口一个字节都不吐。
  2. 别用 readline()：它超时会返回**半行**，看着像设备输出了乱码。
     这里整段累积字节，读完再 splitlines。
"""
import sys
import time

import serial

PORT = "COM7"
BAUD = 115200

plats = sys.argv[1:] or ["baidu"]

try:
    ser = serial.Serial(PORT, BAUD, timeout=0.3)
except Exception as e:
    print("open failed:", e)
    sys.exit(1)

ser.setDTR(False)
ser.setRTS(False)
time.sleep(2.0)


def wait_ready(timeout=40.0):
    """等到串口控制台就绪再发命令 —— 开机前几秒发的命令会被丢掉。
       （打开串口时 DTR/RTS 的变化经常会把板子复位一次）"""
    t0 = time.time()
    seen = bytearray()
    while time.time() - t0 < timeout:
        b = ser.read(512)
        if not b:
            continue
        seen += b
        if b"[Console] Serial Console ready" in seen or b"[BOOT]" in seen:
            return True
    return False


def run_cmd(cmd, wait=14.0):
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode())
    buf = bytearray()
    t0 = time.time()
    idle = 0.0
    while time.time() - t0 < wait:
        b = ser.read(512)
        if b:
            buf += b
            idle = 0.0
        else:
            idle += 0.3
            if buf and idle > 2.0:
                break
    return bytes(buf).decode("utf-8", "replace")


if not wait_ready():
    print("WARN: console not ready")

for p in plats:
    out = run_cmd("news " + p)
    print("==== news %s ====" % p)
    started = False
    for line in out.splitlines():
        line = line.strip("\r")
        if line.startswith("[News]"):
            started = True
        if started or "DNS Failed" in line or "connect failed" in line:
            print("   " + line[:150])
    print()

ser.close()
