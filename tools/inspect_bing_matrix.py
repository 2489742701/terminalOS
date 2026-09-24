#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
在 PC 上把「必应域名 × UA」矩阵全测一遍，回答一个问题：
    "下一页" / b_pag 到底由什么决定 —— 域名(cn vs www) 还是 UA？

同时验证 first=11 是否真的翻页（比对第 1 页与第 2 页的首条结果标题）。
"""
import re
import ssl
import socket
import sys

KITKAT = ("Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) "
          "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36")
ANDROID13 = ("Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 "
             "(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36")


def raw_get(url, ua, timeout=20):
    """用裸 socket 发 GET，尽量贴近设备行为（不跟 3xx，只看首包）。"""
    m = re.match(r"https?://([^/]+)(/.*)?$", url)
    host = m.group(1)
    path = m.group(2) or "/"
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
           "Connection: close\r\n\r\n")
    s.sendall(req.encode("utf-8"))

    buf = b""
    while True:
        try:
            chunk = s.recv(65536)
        except socket.timeout:
            break
        if not chunk:
            break
        buf += chunk
        if len(buf) > 4 * 1024 * 1024:
            break
    s.close()

    head, _, body = buf.partition(b"\r\n\r\n")
    head_s = head.decode("utf-8", "replace")
    status = 0
    loc = ""
    for line in head_s.split("\r\n"):
        if line.startswith("HTTP/"):
            status = int(line.split()[1])
        if line.lower().startswith("location:"):
            loc = line.split(":", 1)[1].strip()
    return status, loc, body


def analyze(tag, body):
    html = body.decode("utf-8", "replace")
    out = []
    out.append("  size      = %d B" % len(body))
    out.append("  b_pag     = %d" % html.count("b_pag"))
    out.append("  下一页    = %d" % html.count("下一页"))
    out.append("  b_algo    = %d" % len(re.findall(r'class="[^"]*b_algo', html)))
    out.append("  filters=  = %d" % html.count("filters="))
    out.append("  sb_form   = %d" % html.count("sb_form"))
    # 第 1 条结果的标题，用来判断 first=11 是否真的翻页
    m = re.search(r'<li class="b_algo".*?<h2>.*?<a[^>]*href="([^"]+)"[^>]*>(.*?)</a>',
                  html, re.S)
    if m:
        title = re.sub(r"<[^>]+>", "", m.group(2)).strip()[:60]
        out.append("  first_hit = %s" % title)
    else:
        out.append("  first_hit = <none>")
    print(tag)
    for l in out:
        print(l)
    return html


def main():
    q = "esp32"
    combos = [
        ("cn.bing.com  + KitKat   ", "https://cn.bing.com/search?q=" + q, KITKAT),
        ("cn.bing.com  + Android13", "https://cn.bing.com/search?q=" + q, ANDROID13),
        ("www.bing.com + KitKat   ", "https://www.bing.com/search?q=" + q, KITKAT),
        ("www.bing.com + Android13", "https://www.bing.com/search?q=" + q, ANDROID13),
    ]
    for tag, url, ua in combos:
        try:
            st, loc, body = raw_get(url, ua)
        except Exception as e:
            print(tag, "-> ERROR", e)
            continue
        print("=" * 70)
        print(tag, "  url=", url)
        print("  status=%d location=%s" % (st, loc[:100]))
        analyze(tag, body)

    # first=11 / 21 是否真的翻页（用最优组合）
    print("=" * 70)
    print("### 翻页验证 (www.bing.com + Android13)")
    seen = []
    for first in (1, 11, 21):
        url = "https://www.bing.com/search?q=%s&first=%d" % (q, first)
        try:
            st, loc, body = raw_get(url, ANDROID13)
        except Exception as e:
            print("first=%d ERROR %s" % (first, e))
            continue
        h = body.decode("utf-8", "replace")
        m = re.search(r'<li class="b_algo".*?<h2>.*?<a[^>]*>(.*?)</a>', h, re.S)
        t = re.sub(r"<[^>]+>", "", m.group(1)).strip()[:50] if m else "<none>"
        seen.append(t)
        print("  first=%-3d size=%-8d b_algo=%-3d 首条=%s"
              % (first, len(body), len(re.findall(r'class="[^"]*b_algo', h)), t))
    if len(seen) == 3:
        print("  翻页有效 =", (seen[0] != seen[1] and seen[1] != seen[2]))


if __name__ == "__main__":
    main()
