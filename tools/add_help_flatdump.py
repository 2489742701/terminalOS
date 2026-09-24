#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 flatdump / dl / serve / ls 补进串口 help。按行处理，CRLF 安全。"""
import io
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
P = os.path.join(ROOT, "src", "hal", "serial_console.cpp")

ANCHOR = 'Serial.println("flat on|off       - browser 平铺排版 on=不建容器全部平铺 off=还原CSS版面");'
NEW = [
    'Serial.println("flatdump <n>      - 平铺诊断 dump 的行上限（查排在第60行之后的内容，如「下一页」）");',
    'Serial.println("dl                - 把当前页存进 LittleFS；ls = 列出已存页面");',
    'Serial.println("serve|servestop   - 把已存页面用 HTTP 共享出去（PC 浏览器访问设备 IP 看）");',
]


def main():
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        raw = f.read()
    lines = raw.split("\n")
    for i, l in enumerate(lines):
        if ANCHOR in l.rstrip("\r"):
            lines[i + 1:i + 1] = NEW
            print("[OK] 在第 %d 行后插入 %d 行" % (i + 1, len(NEW)))
            break
    else:
        print("[FAIL] 锚点未找到")
        return 1
    with io.open(P, "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(lines))
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        back = f.read()
    ok = all(x in back for x in NEW)
    print("[%s] 回读校验" % ("OK" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
