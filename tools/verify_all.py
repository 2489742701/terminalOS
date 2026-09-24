#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
最终验证（2026-09-23 修复批次）：
 1. boot 正常 + loopTask 栈配置生效
 2. 喂一个伪链接 -> 应当被 startFetch 当场拒绝（不允许走到网络）
 3. 正常页面（乐鑫 977 节点那个）能渲染完
"""
import io
import sys
import time

try:
    import serial
except ImportError:
    print("ERROR: pyserial missing")
    sys.exit(1)

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
OUT = sys.argv[2] if len(sys.argv) > 2 else "verify_all.txt"
buf = []


def dec(b):
    for enc in ("utf-8", "gb18030"):
        try:
            t = b.decode(enc)
            if t.count("\ufffd") == 0:
                return t
        except Exception:
            pass
    return b.decode("utf-8", "replace")


def send(s, cmd, wait, tag):
    s.write((cmd + "\n").encode("utf-8"))
    s.flush()
    buf.append(">>> " + cmd)
    end = time.time() + wait
    while time.time() < end:
        line = s.readline()
        if line:
            t = dec(line).rstrip("\r\n")
            buf.append("[%s] %s" % (tag, t))
            print("  [%s] %s" % (tag, t[:170]))


s = serial.Serial(PORT, 115200, timeout=0.5)
time.sleep(0.3)
s.reset_input_buffer()

print("=== 1. 等待 boot ===")
send(s, "", 6.0, "boot")

print("\n=== 2. 伪链接应当被当场拒绝（不许走网络）===")
send(s, "browser https://www.espressif.com.cn/zh-hans/products/socs//javascript: void(0);",
     6.0, "pseudo")

print("\n=== 3. 正常页面能渲染（乐鑫，977 节点那个）===")
send(s, "browser https://www.espressif.com.cn/zh-hans/products/socs/esp32", 45.0, "real")

print("\n=== 4. 内存 ===")
send(s, "mem", 3.0, "mem")

s.close()
io.open(OUT, "w", encoding="utf-8", newline="\r\n").write("\n".join(buf) + "\n")

# ── 结论判定 ──
joined = "\n".join(buf)
print("\n=== 判定 ===")
checks = [
    ("boot 成功", "[BOOT] Geek Terminal ready" in joined),
    ("伪链接被拦截（未走网络）", "refuse fetch (not a real URL)" in joined),
    ("伪链接没有下载 500KB", "read 507440 bytes" not in joined),
    ("正常页面渲染完成", "render done. visible=" in joined),
    ("栈水位打印存在", "loopTask stack:" in joined),
    ("无崩溃", "stack overflow" not in joined and "Guru Meditation" not in joined),
]
allok = True
for name, ok in checks:
    print("  %s  %s" % ("PASS" if ok else "FAIL", name))
    if not ok:
        allok = False
print("\n总体:", "全部通过" if allok else "有项未通过")
print("written ->", OUT)
