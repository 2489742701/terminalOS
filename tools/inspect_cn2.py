#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
360 / 搜狗 移动端：真结果数 + 真翻页 + "下一页"链接长相。
结果抽取按各自 DOM，避免之前 h2/h3 噪声导致的误判。
"""
import re
import ssl
import socket
import io
import os

HERE = os.path.dirname(os.path.abspath(__file__))
UA = ("Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) "
      "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36")


def get(url, ua=UA, timeout=15, depth=0):
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
    s.close()
    head, _, body = buf.partition(b"\r\n\r\n")
    st, loc = "", ""
    for line in head.decode("utf-8", "replace").split("\r\n"):
        if line.startswith("HTTP/"):
            st = line.split()[1]
        if line.lower().startswith("location:"):
            loc = line.split(":", 1)[1].strip()
    if st.startswith("3") and loc:
        return get(loc if loc.startswith("http") else
                   ("https" if port == 443 else "http") + "://" + host + loc,
                   ua, timeout, depth + 1)
    return st, loc, body


def strip(t):
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", t)).strip()


def ext_360(h):
    """360 移动结果：<a class="res-list" 里的 <h3> / 或 <div class="res-title">"""
    out = []
    for m in re.finditer(r'<h3[^>]*>(.*?)</h3>', h, re.S):
        t = strip(m.group(1))
        if len(t) > 4:
            out.append(t)
    if not out:
        for m in re.finditer(r'class="[^"]*res-title[^"]*"[^>]*>(.*?)</', h, re.S):
            t = strip(m.group(1))
            if len(t) > 4:
                out.append(t)
    return out


def ext_sogou(h):
    out = []
    for m in re.finditer(r'<h3[^>]*class="[^"]*vr-?title[^"]*"[^>]*>(.*?)</h3>', h, re.S):
        out.append(strip(m.group(1)))
    if not out:
        for m in re.finditer(r'class="[^"]*vr-?title[^"]*"[^>]*>(.*?)</(?:h3|div|a)>', h, re.S):
            t = strip(m.group(1))
            if len(t) > 4:
                out.append(t)
    return out


def report(name, base, pager, vals, ext, dump=None):
    print("=" * 72)
    print(name, " pager=", pager)
    t1 = None
    for v in vals:
        u = base + "&%s=%s" % (pager, v) if pager else base
        try:
            st, loc, b = get(u)
        except Exception as e:
            print("   %s=%s ERROR %s" % (pager, v, e))
            continue
        h = b.decode("utf-8", "replace")
        if t1 is None:
            t1 = set(ext(h))
            if dump:
                io.open(os.path.join(HERE, dump), "w", encoding="utf-8").write(h)
        ts = ext(h)
        new = [t for t in ts if t not in t1]
        nxt = len(re.findall(r"下一页", h))
        print("   %s=%-3s size=%-8d 结果=%-3d 新增=%-3d '下一页'x%d"
              % (pager, v, len(b), len(ts), len(new), nxt))
        for t in new[:3]:
            print("        +", t[:50])
    return h if t1 is not None else ""


def main():
    h = report("360 m.so.com/s", "https://m.so.com/s?q=esp32", "pn", (1, 2, 3),
               ext_360, dump="so360_m.html")
    i = h.find("Next")
    if i > 0:
        print("   'Next' 上下文:", strip(h[max(0, i - 250):i + 150])[-220:])

    h = report("搜狗 m.sogou.com", "https://m.sogou.com/web/searchList.jsp?keyword=esp32",
               "page", (1, 2, 3), ext_sogou, dump="sogou_m.html")
    i = h.find("下一页")
    if i > 0:
        print("   '下一页' 上下文:", strip(h[max(0, i - 300):i + 200])[-260:])
        for m in re.finditer(r'<a[^>]*href="([^"]*)"[^>]*>\s*下一页', h):
            print("   下一页 href =", m.group(1)[:120])


if __name__ == "__main__":
    main()
