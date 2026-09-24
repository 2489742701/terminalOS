#!/usr/bin/env python3
"""连续翻多个页面，复现"第二次加载就崩"。

用法: probe_nav2.py COM7 40 "url1" "url2" ["url3" ...]
每次导航后抓 secs 秒串口；任何一行含 panic/abort/Guru/assert/Backtrace 立刻标红停下。
"""
import serial, time, sys
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

port = sys.argv[1] if len(sys.argv) > 1 else 'COM7'
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 25.0
urls = sys.argv[3:] or ['https://cn.bing.com/search?q=esp32']

s = serial.Serial(port, 115200, timeout=0.5)
time.sleep(0.2)
s.reset_input_buffer()

BAD = ('panic', 'abort', 'Guru', 'assert failed', 'Backtrace',
       'LoadProhibited', 'StoreProhibited', 'IllegalInstruction',
       'Stack canary', 'rst:0x', 'SavePC', 'Core 1 panic')

crashed = [False]


def drain(seconds, tag):
    end = time.time() + seconds
    while time.time() < end:
        line = s.readline()
        if not line:
            continue
        txt = line.decode('utf-8', 'replace').rstrip()
        mark = ''
        for b in BAD:
            if b.lower() in txt.lower():
                mark = '   <<<< CRASH'
                crashed[0] = True
                break
        print('[%s] %s%s' % (tag, txt, mark), flush=True)
        if crashed[0]:
            # 崩了之后再多抓 3 秒回溯
            e2 = time.time() + 3
            while time.time() < e2:
                l2 = s.readline()
                if l2:
                    print('[%s] %s' % (tag, l2.decode('utf-8', 'replace').rstrip()), flush=True)
            return False
    return True


def send(cmd, wait, tag):
    s.write((cmd + '\n').encode('utf-8'))
    s.flush()
    print('>>> ' + cmd, flush=True)
    return drain(wait, tag)


print('### DRAM/PSRAM 基线', flush=True)
send('nav browser', 2.0, 'nav')
for i, u in enumerate(urls):
    ok = send('browser ' + u, secs, 'page%d' % (i + 1))
    print('### page%d done, crashed=%s' % (i + 1, crashed[0]), flush=True)
    if not ok:
        break
s.close()
print('### FINAL crashed=%s' % crashed[0], flush=True)
