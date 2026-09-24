#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
诊断"乐鑫官网点了首页/硬件/配件都加载不出来"。

思路（master 提的）：在 PC 上把这页按钮背后的真实 href 抓出来，
再逐个喂给 ESP32，区分是"网络拿不到"还是"拿到了但渲不出来"。
"""
import io
import re
import ssl
import sys
import urllib.request

UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36")

out = []


def say(s=""):
    print(s)
    out.append(s)


def fetch(url, timeout=30):
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    req = urllib.request.Request(url, headers={
        "User-Agent": UA,
        "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9",
        "Accept-Language": "zh-CN,zh;q=0.9",
    })
    return urllib.request.urlopen(req, timeout=timeout, context=ctx).read()


BASE = "https://www.espressif.com.cn"
PAGES = [
    ("/zh-hans/products/socs/esp32", "ESP32 产品页"),
    ("/zh-hans/products/modules", "模组页"),
    ("/zh-hans/", "首页"),
]

for path, name in PAGES:
    url = BASE + path
    say("=" * 78)
    say("[PC] %s  %s" % (name, url))
    try:
        raw = fetch(url)
        h = raw.decode("utf-8", "replace")
    except Exception as e:
        say("  抓取失败: %r" % (e,))
        continue

    say("  字节数 = %d   (设备下载上限 786432 B)" % len(raw))
    say("  是否被设备截断 = %s" % ("是！" if len(raw) > 786432 else "否"))
    if len(raw) > 786432:
        say("  -> 超出 %d B，设备只能拿到前 786KB，末尾正文丢失" % (len(raw) - 786432))

    # 统计 script 标签：JS 驱动程度
    nscript = len(re.findall(r"<script", h, re.I))
    say("  <script> 数量 = %d" % nscript)
    # 是否 SPA（正文靠 JS 注入）
    for kw in ["__NEXT_DATA__", "window.__NUXT__", "id=\"__nuxt\"",
               "id=\"app\"", "data-reactroot", "ng-app"]:
        if kw in h:
            say("  发现框架标记: %s  -> 正文可能靠 JS 渲染" % kw)

    # 提取导航链接（首页 / 硬件 / 产品概览 / 配件 / 模组 / 开发板 ...）
    say("")
    say("  --- 页面里的 <a> href（前 25 个，去重）---")
    seen = set()
    rows = []
    for m in re.finditer(r'<a\s[^>]*href="([^"]+)"[^>]*>(.*?)</a>', h,
                         re.I | re.S):
        href, text = m.group(1), m.group(2)
        t = re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", text)).strip()
        if not t:
            continue
        key = (href, t)
        if key in seen:
            continue
        seen.add(key)
        rows.append((href, t))
        if len(rows) >= 25:
            break
    for href, t in rows:
        say("    %-58s  %s" % (t[:24], href[:70]))
    say("  页面链接总数(去重前) = %d" % len(re.findall(r"<a\s[^>]*href=", h, re.I)))

    # 关键：正文位置 —— 正文在第几个字节？
    say("")
    say("  --- 正文位置探测 ---")
    for kw in ["<main", "<article", "id=\"main\"", "class=\"main",
               "产品概览", "配件", "ESP32-WROOM"]:
        idx = h.find(kw)
        say("    %-22s @ %s" % (kw, idx if idx >= 0 else "未出现"))

    say("")

io.open("diag_espressif_links.txt", "w", encoding="utf-8", newline="\r\n").write(
    "\n".join(out) + "\n")
print("\nwritten -> diag_espressif_links.txt")
