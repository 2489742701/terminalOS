#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""按行替换 lvgl_renderer.cpp 里那段过时的 UA 注释（CRLF 安全）。"""
import io
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
P = os.path.join(ROOT, "src", "browser_engine", "src", "lvgl_renderer.cpp")

ANCHOR = '  /* ── 按域名选 UA ──'
END_MARK = '体积也只有 86KB'   # 旧注释最后一行里的内容

NEW_LINES = [
    "  /* ── 按域名选 UA ──",
    "     默认 KitKat（Android 4.4 / Chrome 30 移动版）：对老机器宽容，百度不会",
    "     302 到 wappass 图形验证码；页面也小。",
    "     ⚠️ 但必应必须换**桌面 Chrome 120**。2026-09-23 在 cn.bing.com 实测：",
    "         移动 UA（KitKat / Android13 / iPhone）→ 只给 5 条 li.b_algo，",
    "         HTML 里 0 个分页标记，first= 参数被完全忽略；",
    "         桌面 UA                              → 9~10 条结果 + 「下一页」链接。",
    "      体积从 60KB 涨到 ~100KB，但平铺模式本来就跳过外部 CSS，不会变成",
    "      几十次 TLS。SERP 壳子（时间筛选 / 数字页码 / 顶部导航）交给",
    "      layout_engine.cpp 的 flat_is_serp_chrome_link() 过滤。",
    "     另注：www.bing.com 会被地域 302 到 cn.bing.com，所以匹配 bing.com",
    "     两个都覆盖。 */",
]


def splitlines_keep(s):
    return s.split("\n")


def main():
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        raw = f.read()
    lines = splitlines_keep(raw)
    crlf = "\r\n" in raw

    start = None
    end = None
    for i, l in enumerate(lines):
        if start is None and ANCHOR in l:
            start = i
        if start is not None and END_MARK in l:
            end = i
            break
    if start is None or end is None:
        print("[FAIL] anchor=%s end=%s" % (start, end))
        return 1
    print("[INFO] 替换行 %d..%d (1-based %d..%d)" % (start, end, start + 1, end + 1))
    lines[start:end + 1] = NEW_LINES
    out = "\n".join(lines)
    with io.open(P, "w", encoding="utf-8", newline="") as f:
        f.write(out)
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        back = f.read()
    ok = ANCHOR in back and END_MARK not in back and "flat_is_serp_chrome_link" in back
    print("[%s] 回读校验 %s" % ("OK" if ok else "FAIL", "CRLF" if crlf else "LF"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
