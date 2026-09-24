"""
perf_sweep.py - 逐个屏跑 `perf` 串口命令，收集 draw/flush 拆分耗时

用法：
  python perf_sweep.py                 # 默认 COM7，每个屏 perf 30
  python perf_sweep.py COM7 40

输出同时写到 tools/perf_sweep.out.txt（方便用 Read 看，躲开 PowerShell 不回显）
"""
import serial, time, sys, io, os

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM7'
ROUNDS = sys.argv[2] if len(sys.argv) > 2 else '30'

SCREENS = [
    ('launcher', '桌面(磁贴)'),
    ('clock',    '时钟'),
    ('settings', '设置'),
    ('wifi',     '无线网络'),
    ('games',    '游戏栏目'),
    ('browser',  '浏览器(搜索首页)'),
    ('draw',     '画板'),
    ('memory',   '记忆卡牌'),
    ('sysinfo',  '系统信息'),
    ('weather',  '天气'),
    ('desktop',  '桌面图标'),
]

out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        'perf_sweep.out.txt')
buf = []


def log(s):
    print(s, flush=True)
    buf.append(s)


s = serial.Serial(PORT, 115200, timeout=0.4)
time.sleep(0.2)
s.reset_input_buffer()


def send(cmd, wait=2.0, quiet=False):
    s.reset_input_buffer()
    s.write((cmd + '\n').encode('utf-8'))
    s.flush()
    end = time.time() + wait
    lines = []
    while time.time() < end:
        try:
            line = s.readline()
        except Exception:
            break
        if line:
            t = line.decode('utf-8', 'replace').rstrip()
            lines.append(t)
            if not quiet:
                log('   ' + t)
    return lines


log('=== perf sweep  port=%s  rounds=%s ===' % (PORT, ROUNDS))
send('help', 1.5, quiet=True)          # 唤醒，确认串口在线

for name, desc in SCREENS:
    log('')
    log('--- %s (%s)' % (name, desc))
    send('nav ' + name, 2.5, quiet=True)   # 导航本身刷屏太吵，静默
    time.sleep(0.6)                        # 等屏建好 + 首帧
    send('perf ' + ROUNDS, 6.0)

log('')
log('--- 回桌面')
send('nav launcher', 2.5, quiet=True)

s.close()

with io.open(out_path, 'w', encoding='utf-8') as f:
    f.write('\n'.join(buf))
print('\nWROTE ' + out_path)
