#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""必应分页块里有个 <h4 class="b_hide">分页</h4>（视觉隐藏），平铺后单独占一行。加进垃圾短语。"""
import io
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
P = os.path.join(ROOT, "src", "browser_engine", "src", "layout_engine.cpp")

OLD_LINE = '    "切换到国际版", "时间不限", "搜索工具",'
NEW_LINE = '    "切换到国际版", "时间不限", "搜索工具", "分页",'


def main():
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        raw = f.read()
    if OLD_LINE not in raw:
        print("[FAIL] 锚点未找到")
        return 1
    s = raw.replace(OLD_LINE, NEW_LINE, 1)
    with io.open(P, "w", encoding="utf-8", newline="") as f:
        f.write(s)
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        back = f.read()
    ok = NEW_LINE in back
    print("[%s] 回读校验" % ("OK" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
