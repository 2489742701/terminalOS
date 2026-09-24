#!/usr/bin/env python3
"""进浏览器 -> 加载指定 URL -> 抓一段时间串口输出。

用法: probe_flat.py COM7 "https://m.baidu.com/" 40
"""
import serial, time, sys
# Windows 控制台默认 GBK，网页里的 ‘ › 全角标点会把 print 打崩（UnicodeEncodeError）。
# 直接把 stdout 换成 UTF-8，别再让探测脚本因为一个字符中断。
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

port = sys.argv[1] if len(sys.argv) > 1 else 'COM7'
url = sys.argv[2] if len(sys.argv) > 2 else 'https://m.baidu.com/'
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 40.0

s = serial.Serial(port, 115200, timeout=0.5)
time.sleep(0.2)
s.reset_input_buffer()


def send(cmd, wait=2.0):
    s.write((cmd + '\n').encode('utf-8'))
    s.flush()
    print(">>> " + cmd, flush=True)
    end = time.time() + wait
    while time.time() < end:
        line = s.readline()
        if line:
            print(line.decode('utf-8', 'replace').rstrip(), flush=True)


send('nav browser', 2.0)
send('browser ' + url, secs)
s.close()
