#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""验证「伪链接不渲染」的效果：对比 junkDropped / widgets / clickable links。"""
import io
import sys
import time

try:
    import serial
except ImportError:
    print("ERROR: pyserial missing")
    sys.exit(1)

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
OUT = sys.argv[2] if len(sys.argv) > 2 else "verify_pseudo.txt"
URL = sys.argv[3] if len(sys.argv) > 3 else \
    "https://www.espressif.com.cn/zh-hans/products/socs/esp32"

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


s = serial.Serial(PORT, 115200, timeout=0.6)
time.sleep(0.3)
s.reset_input_buffer()


def send(cmd, wait, tag):
    s.write((cmd + "\n").encode("utf-8"))
    s.flush()
    end = time.time() + wait
    while time.time() < end:
        line = s.readline()
        if line:
            t = dec(line).rstrip("\r\n")
            buf.append("[%s] %s" % (tag, t))
            print("  [%s] %s" % (tag, t[:165]))


send("", 3.0, "boot")
send("flatdump 120", 1.2, "cfg")
send("nav browser", 2.0, "nav")
send("browser " + URL, 50.0, "page")
time.sleep(1.5)
while True:
    line = s.readline()
    if not line:
        break
    buf.append("[tail] " + dec(line).rstrip("\r\n"))
s.close()

io.open(OUT, "w", encoding="utf-8", newline="\r\n").write("\n".join(buf) + "\n")

joined = "\n".join(buf)
print("\n=== 关键指标 ===")
import re
for pat in [r"nodes=\d+ width=\d+ junkDropped=\d+",
            r"widgets created: \d+ \(limit \d+\), clickable links=\d+",
            r"loopTask stack: \d+ B left",
            r"render done\. DRAM free: \d+"]:
    m = re.search(pat, joined)
    print("  " + (m.group(0) if m else "(未找到) " + pat))

# 正文探测：看渲染出来的最后几行是什么（菜单 or 正文）
print("\n=== 渲染尾部 12 行（看是否出现正文）===")
flat = [l for l in buf if "[Flat]" in l]
for l in flat[-12:]:
    print("  " + l[:150])
print("\nwritten ->", OUT)
