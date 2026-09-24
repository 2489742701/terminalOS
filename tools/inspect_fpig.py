#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
验证：必应的分页必须带上上一页返回的 FPIG token，否则 first= 被忽略。
做法：真的"点"一次"下一页" —— 抓第1页 → 抽出 下一页 href → 带 FPIG 请求 → 比对结果。
"""
import re
import ssl
import socket

DESKTOP = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
           "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36")


def get(url, ua=DESKTOP, timeout=15, depth=0):
    if depth > 4:
        return "", "", b""
    m = re.match(r"https?://([^/]+)(/.*)?$", url)
    host, path = m.group(1), (m.group(2) or "/")
    port = 443 if url.startswith("https") else 80
    s = socket.create_connection((host, port), timeout=timeout)
    if port == 443:
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        s = ctx.wrap_socket(s, server_hostname=host)
    req = ("GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\n"
           "User-Agent: " + ua + "\r\n"
           "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
           "Accept-Language: zh-CN,zh;q=0.9\r\nConnection: close\r\n\r\n")
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
        if len(buf) > 3 * 1024 * 1024:
            break
    s.close()
    head, _, body = buf.partition(b"\r\n\r\n")
    st, loc = "", ""
    for line in head.decode("utf-8", "replace").split("\r\n"):
        if line.startswith("HTTP/"):
            st = line.split()[1]
        if line.lower().startswith("location:"):
            loc = line.split(":", 1)[1].strip()
    if st.startswith("3") and loc:
        if loc.startswith("http"):
            return get(loc, ua, timeout, depth + 1)
        if loc.startswith("/"):
            return get(("https" if port == 443 else "http") + "://" + host + loc,
                       ua, timeout, depth + 1)
    return st, loc, body


def strip(t):
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", t)).strip()


def titles(h):
    out = []
    for m in re.finditer(r'<li class="b_algo[^"]*"(.*?)</li>', h, re.S):
        t = re.search(r"<h2[^>]*>(.*?)</h2>", m.group(1), re.S)
        if t:
            out.append(strip(t.group(1)))
    return out


def unesc(u):
    return u.replace("&amp;", "&")


def main():
    print("### 模拟真实点击：第1页 → 抽'下一页' → 请求 → 第2页 → 再抽 → 第3页")
    url = "https://cn.bing.com/search?q=esp32"
    prev = []
    for step in range(1, 5):
        st, loc, b = get(url)
        h = b.decode("utf-8", "replace")
        ts = titles(h)
        print("  第%d页  size=%-7d 结果=%-2d" % (step, len(b), len(ts)))
        for t in ts[:3]:
            print("        -", t[:52])
        if prev:
            print("       与上一页不同 =", tuple(ts[:3]) != tuple(prev[:3]))
        prev = ts
        nxt = None
        for m in re.finditer(r'<a[^>]*href="([^"]*)"[^>]*>\s*下一页\s*</a>', h, re.S):
            nxt = unesc(m.group(1))
        if not nxt:
            for m in re.finditer(r'href="([^"]*FORM=PORE[^"]*)"', h):
                nxt = unesc(m.group(1))
        if not nxt:
            print("       ⚠ 没有'下一页'了，停止")
            break
        print("      下一页 href =", nxt[:110])
        url = "https://cn.bing.com" + nxt if nxt.startswith("/") else nxt
    print()
    print("### 对照：手工拼 first=11 不带 FPIG")
    st, loc, b = get("https://cn.bing.com/search?q=esp32&first=11")
    ts = titles(b.decode("utf-8", "replace"))
    print("  结果=%d 首条=%s" % (len(ts), ts[0][:45] if ts else "<none>"))


if __name__ == "__main__":
    main()
