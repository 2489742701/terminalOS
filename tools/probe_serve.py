#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把设备收到的原始 HTML 拿出来看：
  nav browser → 抓必应 SERP → dl（存 LittleFS）→ wifi（拿 IP）→ ls → serve
跑完用 PC 浏览器/脚本访问 http://<ip>/ 拿文件。
"""
import serial
import time
import sys
import io

port = sys.argv[1] if len(sys.argv) > 1 else 'COM7'
out = sys.argv[2] if len(sys.argv) > 2 else 'probe_serve.txt'

s = serial.Serial(port, 115200, timeout=0.5)
time.sleep(0.2)
s.reset_input_buffer()
buf = []


def dec(b):
    try:
        t = b.decode('utf-8')
        if t.count('\ufffd') == 0:
            return t
    except Exception:
        pass
    try:
        return b.decode('gb18030', 'replace')
    except Exception:
        return b.decode('utf-8', 'replace')


def send(cmd, wait, tag):
    s.write((cmd + '\n').encode('utf-8'))
    s.flush()
    buf.append('>>> ' + cmd)
    end = time.time() + wait
    while time.time() < end:
        line = s.readline()
        if line:
            buf.append('[%s] %s' % (tag, dec(line).rstrip()))


try:
    send('nav browser', 2.0, 'nav')
    send('browser https://cn.bing.com/search?q=esp32', 30.0, 'bing')
    send('dl', 6.0, 'dl')
    send('ls', 4.0, 'ls')
    send('wifi', 4.0, 'wifi')
    send('serve', 4.0, 'serve')
    send('ls', 3.0, 'ls2')
finally:
    s.close()

io.open(out, 'w', encoding='utf-8').write('\n'.join(buf) + '\n')
print('written', out, len(buf), 'lines')
