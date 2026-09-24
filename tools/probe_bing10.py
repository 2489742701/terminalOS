#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
设备实测必应 SERP（dump 上限调到 200，保证能看到排在后面的分页行）。
"""
import serial
import time
import sys
import io

port = sys.argv[1] if len(sys.argv) > 1 else 'COM7'
out = sys.argv[2] if len(sys.argv) > 2 else 'probe_bing10.txt'
q = sys.argv[3] if len(sys.argv) > 3 else 'https://cn.bing.com/search?q=esp32'

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
    send('flatdump 200', 1.5, 'cfg')
    send('nav browser', 2.0, 'nav')
    send('browser ' + q, 35.0, 'bing')
    time.sleep(2.0)
    while True:
        line = s.readline()
        if not line:
            break
        buf.append('[tail] ' + dec(line).rstrip())
finally:
    s.close()

io.open(out, 'w', encoding='utf-8').write('\n'.join(buf) + '\n')
print('written', out, len(buf), 'lines')
