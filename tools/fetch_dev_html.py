#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从设备的页面服务器把存的 HTML 拉到 PC 上分析。"""
import serial
import time
import sys
import io
import re
import urllib.request

port = sys.argv[1] if len(sys.argv) > 1 else 'COM7'

s = serial.Serial(port, 115200, timeout=0.5)
time.sleep(0.2)
s.reset_input_buffer()


def send(cmd, wait):
    s.write((cmd + '\n').encode('utf-8'))
    s.flush()
    end = time.time() + wait
    out = []
    while time.time() < end:
        line = s.readline()
        if line:
            out.append(line.decode('utf-8', 'replace').rstrip())
    return out


try:
    send('servestop', 2.0)
    o = send('serve', 3.0)
    for l in o:
        print('[serve]', l)
finally:
    s.close()

try:
    idx = urllib.request.urlopen('http://192.168.50.147/', timeout=15).read()
    print('--- index ---')
    print(idx.decode('utf-8', 'replace')[:1200])
except Exception as e:
    print('index ERROR', e)

name = None
m = re.search(r'href="(/[^"]+\.html)"', idx.decode('utf-8', 'replace'))
if m:
    name = m.group(1)
if not name:
    m = re.search(r'(/p[0-9a-f]+\.html)', idx.decode('utf-8', 'replace'))
    name = m.group(1) if m else '/p2d733c6b.html'

print('--- fetch', name, '---')
try:
    b = urllib.request.urlopen('http://192.168.50.147' + name, timeout=25).read()
except Exception as e:
    print('ERROR', e)
    sys.exit(1)
io.open('dev_bing.html', 'wb').write(b)
h = b.decode('utf-8', 'replace')
print('size', len(b))
for kw in ['下一页', 'FORM=PORE', 'FORM=PERE', 'b_pag', 'filters=ex1',
           'qpvt=', 'FORM=HDRSC', 'b_algo', 'sw_next', 'sb_pag']:
    print('  %-14s x%d' % (kw, h.count(kw)))
