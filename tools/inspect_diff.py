#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""全量 10 条对比：点'下一页'前后到底一不一样？顺带看页码指示器。"""
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
    s = socket.create_connection((host, 443), timeout=timeout)
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
    s.close()
    head, _, body = buf.partition(b"\r\n\r\n")
    st, loc = "", ""
    for line in head.decode("utf-8", "replace").split("\r\n"):
        if line.startswith("HTTP/"):
            st = line.split()[1]
        if line.lower().startswith("location:"):
            loc = line.split(":", 1)[1].strip()
    if st.startswith("3") and loc:
        return get(loc if loc.startswith("http") else "https://" + host + loc,
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


def page_marker(h):
    """找 '第 N 页' / '1-10' 之类"""
    out = []
    for m in re.finditer(r'(第\s*\d+\s*页|aria-label="[^"]*页[^"]*")', h):
        out.append(m.group(1))
    return sorted(set(out))[:6]


def main():
    for q in ("esp32", "python 教程"):
        print("=" * 70)
        print("query =", q)
        url = "https://cn.bing.com/search?q=" + q.replace(" ", "+")
        st, loc, b = get(url)
        h = b.decode("utf-8", "replace")
        t1 = titles(h)
        print("  P1 size=%d 结果=%d marker=%s" % (len(b), len(t1), page_marker(h)))
        for i, t in enumerate(t1, 1):
            print("     %2d %s" % (i, t[:55]))
        nxt = None
        for m in re.finditer(r'href="([^"]*FORM=PORE[^"]*)"', h):
            nxt = m.group(1).replace("&amp;", "&")
        if not nxt:
            print("  无下一页")
            continue
        st, loc, b = get("https://cn.bing.com" + nxt)
        h2 = b.decode("utf-8", "replace")
        t2 = titles(h2)
        print("  P2 size=%d 结果=%d marker=%s" % (len(b), len(t2), page_marker(h2)))
        for i, t in enumerate(t2, 1):
            mark = "  " if t in t1 else "* "
            print("     %s%2d %s" % (mark, i, t[:55]))
        print("  新增条目 =", len([t for t in t2 if t not in t1]), "/", len(t2))


if __name__ == "__main__":
    main()
