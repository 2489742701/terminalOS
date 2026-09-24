#!/usr/bin/env python3
"""让设备自己抓一个会回显 UA 的地址，看它实际发出去的是什么。"""
import serial, time, sys
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

port = sys.argv[1] if len(sys.argv) > 1 else 'COM7'
s = serial.Serial(port, 115200, timeout=0.5)
time.sleep(0.2)
s.reset_input_buffer()


def send(cmd, wait, tag):
    s.write((cmd + '\n').encode('utf-8'))
    s.flush()
    print('>>> ' + cmd, flush=True)
    end = time.time() + wait
    while time.time() < end:
        line = s.readline()
        if line:
            print('[%s] %s' % (tag, line.decode('utf-8', 'replace').rstrip()), flush=True)


send('nav browser', 2.0, 'nav')
for u in ['http://httpbin.org/user-agent', 'https://httpbin.org/user-agent']:
    send('browser ' + u, 20.0, 'ua')
s.close()
