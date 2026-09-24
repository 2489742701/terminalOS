#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
平铺诊断 dump 的 60 行上限写死在代码里，结果「下一页」这种排在后面的行
永远打不出来（2026-09-23 排查必应分页时踩到：分页在第 60 行之后，日志里
看不到，误判成"没渲染"）。改成运行时可调：串口 `flatdump <n>`。

改动：
  layout_engine.cpp  s_widgetCount <= 60  ->  s_widgetCount <= s_flatDumpLimit
  layout_engine.cpp  新增 s_flatDumpLimit + layout_set_flat_dump_limit()
  layout_engine.h    声明
  serial_console.cpp 新增 flatdump 命令
按行处理，CRLF 安全。
"""
import io
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
LAYOUT_CPP = os.path.join(ROOT, "src", "browser_engine", "src", "layout_engine.cpp")
LAYOUT_H = os.path.join(ROOT, "src", "browser_engine", "include", "layout_engine.h")
CONSOLE = os.path.join(ROOT, "src", "hal", "serial_console.cpp")


def load(p):
    with io.open(p, "r", encoding="utf-8", newline="") as f:
        return f.read()


def save(p, s):
    with io.open(p, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def main():
    # ---------- layout_engine.cpp ----------
    s = load(LAYOUT_CPP)
    a = "static int s_widgetCount = 0;"
    if a not in s:
        print("[FAIL] s_widgetCount 未找到")
        return 1
    s = s.replace(a, a + "\n"
                  "/* 平铺诊断 dump 的行上限（串口 `flatdump <n>` 可调）。\n"
                  "   默认 60：再多就刷屏，且会拖慢渲染。查「下一页」这类排在\n"
                  "   后面的行时把它调大（实测必应分页在第 60 行之后）。 */\n"
                  "static int s_flatDumpLimit = 60;", 1)

    n1 = s.count("s_widgetCount <= 60")
    if n1 != 2:
        print("[FAIL] 期望 2 处 s_widgetCount <= 60，实际 %d" % n1)
        return 1
    s = s.replace("s_widgetCount <= 60", "s_widgetCount <= s_flatDumpLimit")

    a2 = "void layout_set_flat_mode(bool on) { s_flatMode = on; }"
    if a2 not in s:
        print("[FAIL] layout_set_flat_mode 未找到")
        return 1
    s = s.replace(a2, a2 + "\n"
                  "void layout_set_flat_dump_limit(int n) {\n"
                  "  s_flatDumpLimit = (n < 0) ? 0 : (n > 2000 ? 2000 : n);\n"
                  "}", 1)
    save(LAYOUT_CPP, s)
    b = load(LAYOUT_CPP)
    print("[OK] layout_engine.cpp: dumpLimit=%d, 替换处=%d, setter=%s"
          % (b.count("s_flatDumpLimit"), b.count("s_widgetCount <= s_flatDumpLimit"),
             "layout_set_flat_dump_limit" in b))

    # ---------- layout_engine.h ----------
    h = load(LAYOUT_H)
    a3 = "void layout_set_flat_mode(bool on);"
    if a3 not in h:
        print("[FAIL] 头文件声明锚点未找到")
        return 1
    h = h.replace(a3, a3 + "\nvoid layout_set_flat_dump_limit(int n);", 1)
    save(LAYOUT_H, h)
    print("[OK] layout_engine.h:", "layout_set_flat_dump_limit" in load(LAYOUT_H))

    # ---------- serial_console.cpp ----------
    c = load(CONSOLE)
    lines = c.split("\n")
    idx = None
    for i, l in enumerate(lines):
        if 'strcmp(cmd, "flat")' in l:
            idx = i
            break
    if idx is None:
        print("[FAIL] flat 命令未找到")
        return 1
    # 找到该分支结束（下一个 "} else if" 或 "}"）
    j = idx
    while j < len(lines) and "} else if" not in lines[j]:
        j += 1
    seg = "\n".join(lines[idx:j])
    print("[INFO] flat 分支:\n" + seg)

    # 在 flat 分支后插入新分支
    newbranch = [
        '} else if (strcmp(cmd, "flatdump") == 0) {',
        '    int n = atoi(arg);',
        '    if (n <= 0) n = 60;',
        '    layout_set_flat_dump_limit(n);',
        '    Serial.printf("[Console] flat dump limit = %d\\n", n);',
    ]
    lines[j:j] = newbranch
    c2 = "\n".join(lines)
    save(CONSOLE, c2)
    b3 = load(CONSOLE)
    print("[OK] serial_console.cpp:", 'strcmp(cmd, "flatdump")' in b3)
    return 0


if __name__ == "__main__":
    sys.exit(main())
