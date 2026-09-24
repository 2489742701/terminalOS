#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把 cn.bing.com 的 SERP 抓下来落盘，然后回答三个问题：
  Q1 分页控件到底存不存在？以什么文本/类名出现？
  Q2 那排"全部/24小时/一周内/一个月内/去年"从哪来？
  Q3 &first=11 在 cn.bing.com 上到底翻不翻页？
"""
import re
import ssl
import socket
import io
import os

KITKAT = ("Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) "
          "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36")
ANDROID13 = ("Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 "
             "(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36")

HERE = os.path.dirname(os.path.abspath(__file__))


def raw_get(url, ua, extra="", timeout=20):
    m = re.match(r"https?://([^/]+)(/.*)?$", url)
    host, path = m.group(1), (m.group(2) or "/")
    port = 443 if url.startswith("https") else 80
    s = socket.create_connection((host, port), timeout=timeout)
    if port == 443:
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        s = ctx.wrap_socket(s, server_hostname=host)
    req = ("GET " + path + " HTTP/1.1\r\n"
           "Host: " + host + "\r\n"
           "User-Agent: " + ua + "\r\n"
           "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
           "Accept-Language: zh-CN,zh;q=0.9\r\n"
           + extra +
           "Connection: close\r\n\r\n")
    s.sendall(req.encode("utf-8"))
    buf = b""
    while True:
        try:
            c = s.recv(65536)
        except socket.timeout:
            break
        if not c:
            break
        buf += c
        if len(buf) > 4 * 1024 * 1024:
            break
    s.close()
    head, _, body = buf.partition(b"\r\n\r\n")
    return body


def strip_tags(t):
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", t)).strip()


def hits(html):
    return [strip_tags(x) for x in re.findall(r"<h2[^>]*>(.*?)</h2>", html, re.S)]


def main():
    q = "esp32"
    base = "https://cn.bing.com/search?q=" + q

    print("### Q1/Q2: 抓 cn.bing.com (Android13 UA)")
    h = raw_get(base, ANDROID13).decode("utf-8", "replace")
    io.open(os.path.join(HERE, "bing_cn.html"), "w", encoding="utf-8").write(h)
    print("  size =", len(h))

    print("  -- 所有含 'filter' 的片段 --")
    for m in re.finditer(r'.{80}filters=.{120}', h):
        print("   ", strip_tags(m.group(0))[:180])

    print("  -- 所有 class 含 b_ 的 nav/分页候选 --")
    for cls in sorted(set(re.findall(r'class="([^"]*b_[a-zA-Z_]+[^"]*)"', h))):
        if any(k in cls for k in ("pag", "nav", "page", "next", "no", "last")):
            print("    class=", cls)

    print("  -- 正文里的 '页' / '下一' / 'Next' --")
    for kw in ("下一页", "下一頁", "下页", "Next", "next", "上一页"):
        print("    %-6s x%d" % (kw, h.count(kw)))

    print("  -- 所有 <a> 里带 first= 的 href --")
    for m in sorted(set(re.findall(r'href="([^"]*first=\d+[^"]*)"', h))):
        print("    ", m[:160])

    print("  -- 结果标题 --")
    for i, t in enumerate(hits(h)[:12], 1):
        print("    %2d %s" % (i, t[:60]))

    print()
    print("### Q3: cn.bing.com 上 first= 到底翻不翻页")
    seen = {}
    for first in (1, 11, 21, 31):
        url = "%s&first=%d" % (base, first)
        hh = raw_get(url, ANDROID13).decode("utf-8", "replace")
        hs = hits(hh)
        seen[first] = hs
        print("  first=%-3d size=%-7d 结果数=%-3d 首条=%s"
              % (first, len(hh), len(hs), (hs[0][:45] if hs else "<none>")))
    uniq = {tuple(v[:3]) for v in seen.values()}
    print("  不同结果集数 =", len(uniq), "-> 翻页有效 =" , len(uniq) > 1)

    print()
    print("### Q3b: 换 KitKat UA 再试一次 first=")
    seen2 = {}
    for first in (1, 11):
        url = "%s&first=%d" % (base, first)
        hh = raw_get(url, KITKAT).decode("utf-8", "replace")
        hs = hits(hh)
        seen2[first] = hs
        print("  first=%-3d size=%-7d 结果数=%-3d 首条=%s"
              % (first, len(hh), len(hs), (hs[0][:45] if hs else "<none>")))
    print("  翻页有效 =", tuple(seen2.get(1, [])[:3]) != tuple(seen2.get(11, [])[:3]))


if __name__ == "__main__":
    main()
