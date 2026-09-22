"""
verify_flow.py - 一次串口连接内按时间线执行多条命令，全程记录输出。

用于验证浏览器 Activity 生命周期改动：
  基线内存 -> 进入浏览器(懒创建) -> 连续 3 次加载 -> 退出 -> 回收后内存

用法：
  python verify_flow.py COM7
"""
import serial, time, sys

# Windows 控制台默认 GBK，串口里的 UTF-8 字符会抛 UnicodeEncodeError
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM7'
URL = sys.argv[2] if len(sys.argv) > 2 else 'https://m.baidu.com'

# (触发时刻秒, 命令, 该步之后的观察时长秒)
# 重点验证：进入 -> 加载 -> 退出回收 -> 再次进入 的完整生命周期
SCRIPT = [
    (1.0,  'mem',             3.0),   # 基线（仅 Launcher）
    (5.0,  f'browser {URL}', 38.0),   # 进入 + 第 1 次加载
    (44.0, 'mem',             3.0),   # 加载后
    (48.0, 'nav launcher',    9.0),   # 退出（应释放浏览器 Activity）
    (58.0, 'mem',             3.0),   # 回收后 —— 关键指标
    (62.0, f'browser {URL}', 38.0),   # 再次进入 + 第 2 次加载
    (101.0, 'mem',            3.0),
    (105.0, 'nav launcher',   9.0),   # 再次退出
    (115.0, 'mem',            3.0),   # 第二次回收后
]
TOTAL = SCRIPT[-1][0] + SCRIPT[-1][2] + 3.0

s = serial.Serial(PORT, 115200, timeout=0.3)
time.sleep(0.2)
s.reset_input_buffer()

start = time.time()
sent = set()
print(f"=== verify_flow start, total ~{TOTAL:.0f}s ===", flush=True)

while time.time() - start < TOTAL:
    el = time.time() - start
    for i, (at, cmd, _) in enumerate(SCRIPT):
        if i not in sent and el >= at:
            sent.add(i)
            s.write((cmd + '\n').encode('utf-8'))
            s.flush()
            print(f"\n>>> [{el:6.1f}s] {cmd}", flush=True)
    line = s.readline()
    if line:
        print(line.decode('utf-8', 'replace').rstrip(), flush=True)

s.close()
print("\n=== verify_flow done ===", flush=True)
