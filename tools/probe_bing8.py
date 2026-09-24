#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
设备实测：cn.bing.com SERP 在新 UA（桌面 Chrome120）+ 新 SERP 壳子过滤下
渲染出来什么。重点看：
  - 体积 / 结果条数（预期 ~100KB、10 条）
  - junkDropped（预期大幅上升：时间筛选 4 + 全部 1 + 导航 6 + 页码 2）
  - 有没有「下一页」胶囊
  - 还有没有「全部 / 24小时 / 图片 / 视频」这类壳子
"""
import serial
import time
import sys
import io

port = sys.argv[1] if len(sys.argv) > 1 else 'COM7'
out = sys.argv[2] if len(sys.argv) > 2 else 'probe_bing8.txt'

s = serial.Serial(port, 115200, timeout=0.5)
time.sleep(0.2)
s.reset_input_buffer()

buf = []


def dec(b):
    """设备串口偶发 GBK 混编：先试 utf-8，替换符太多再退 gb18030。"""
    try:
        t = b.decode('utf-8')
        if t.count('\ufffd') == 0:
            return t
    except Exception:
        t = b.decode('utf-8', 'replace')
    try:
        return b.decode('gb18030', 'replace')
    except Exception:
        return t


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
