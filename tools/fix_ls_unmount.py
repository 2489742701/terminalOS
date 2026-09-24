#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
修：listSavedPages() 结尾无条件 LittleFS.end()，会把正在运行的页面服务器
    （pageServerStart 里 begin 过同一个 LittleFS）卸载掉 → 之后所有请求 404。
    实测（2026-09-23）：serve 之后敲 ls，再访问 http://<ip>/xxx.html 就 404。
    修法：服务器在跑时既不重复 begin 也不 end。
按行替换，CRLF 安全。
"""
import io
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
P = os.path.join(ROOT, "src", "app", "browser_screen.cpp")


def main():
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        raw = f.read()
    lines = raw.split("\n")

    # 1) 定位 listSavedPages 函数体
    start = None
    for i, l in enumerate(lines):
        if "static void listSavedPages()" in l:
            start = i
            break
    if start is None:
        print("[FAIL] 找不到 listSavedPages")
        return 1
    end = None
    for j in range(start, len(lines)):
        if lines[j].rstrip("\r") == "}":
            end = j
            break
    if end is None:
        print("[FAIL] 找不到函数结尾")
        return 1
    print("[INFO] 函数体行 %d..%d" % (start + 1, end + 1))

    body = [l.rstrip("\r") for l in lines[start:end + 1]]
    changed = False

    # begin 行
    for k, l in enumerate(body):
        if "LittleFS.begin(false)" in l and "挂载失败" in l:
            indent = l[:len(l) - len(l.lstrip())]
            body[k:k + 1] = [
                indent + "/* 服务器在跑时 LittleFS 已被 pageServerStart() 挂上：",
                indent + "   这里再 end() 一次会把服务器的文件访问整垮（实测 404）。 */",
                indent + "bool fsOwner = (g_pageSrv == nullptr);",
                indent + "if (fsOwner && !LittleFS.begin(false)) {",
                indent + "  Serial.println(\"[ls] LittleFS 挂载失败\");",
                indent + "  return;",
                indent + "}",
            ]
            changed = True
            break
    if not changed:
        print("[FAIL] 没找到 begin 行")
        return 1

    # end 行
    changed2 = False
    for k in range(len(body) - 1, -1, -1):
        if body[k].strip() == "LittleFS.end();":
            indent = body[k][:len(body[k]) - len(body[k].lstrip())]
            body[k] = indent + "if (fsOwner) LittleFS.end();"
            changed2 = True
            break
    if not changed2:
        print("[FAIL] 没找到 end 行")
        return 1

    lines[start:end + 1] = body
    with io.open(P, "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(lines))
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        back = f.read()
    good = "bool fsOwner" in back and "if (fsOwner) LittleFS.end();" in back
    print("[%s] 回读校验" % ("OK" if good else "FAIL"))
    if good:
        for l in back.split("\n")[start:start + 22]:
            print("   " + l.rstrip("\r"))
    return 0 if good else 1


if __name__ == "__main__":
    sys.exit(main())
