#!/usr/bin/env python3
"""加载一页 -> 串口 dl 把页面存进 LittleFS，抓日志。"""
import serial, time, sys
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

port = sys.argv[1] if len(sys.argv) > 1 else 'COM7'
url = sys.argv[2] if len(sys.argv) > 2 else 'https://cn.bing.com/search?q=esp32'

s = serial.Serial(port, 115200, timeout=0.5)
time.sleep(0.2)
s.reset_input_buffer()


def drain(seconds, tag):
    end = time.time() + seconds
    while time.time() < end:
        line = s.readline()
        if line:
            print('[%s] %s' % (tag, line.decode('utf-8', 'replace').rstrip()), flush=True)


def send(cmd, wait, tag):
    s.write((cmd + '\n').encode('utf-8'))
    s.flush()
    print('>>> ' + cmd, flush=True)
    drain(wait, tag)


send('nav browser', 2.0, 'nav')
send('browser ' + url, 25.0, 'page')
send('dl', 6.0, 'dl')
send('dl', 4.0, 'dl2')
s.close()
