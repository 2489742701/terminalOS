#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
最后一次尝试让必应真翻页：桌面 UA + cookie 会话 + Referer 连续 + FPIG 链。
对照三组：
  A 裸 GET first=11&FORM=PORE
  B 带 FPIG    + Referer
  C 带 FPIG + cookie + Referer
看谁的结果集与第 1 页真正不同。
"""
import re
import ssl
import socket

DESKTOP = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
           "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36")
HOST = "cn.bing.com"


def get(path, ua=DESKTOP, cookie=None, referer=None, timeout=15):
    s = socket.create_connection((HOST, 443), timeout=timeout)
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    s = ctx.wrap_socket(s, server_hostname=HOST)
    extra = ""
    if cookie:
        extra += "Cookie: " + cookie + "\r\n"
    if referer:
        extra += "Referer: " + referer + "\r\n"
    req = ("GET " + path + " HTTP/1.1\r\nHost: " + HOST + "\r\n"
           "User-Agent: " + ua + "\r\n"
           "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
           "Accept-Language: zh-CN,zh;q=0.9\r\n" + extra +
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
    s.close()
    head, _, body = buf.partition(b"\r\n\r\n")
    st = ""
    cks = []
    for line in head.decode("utf-8", "replace").split("\r\n"):
        if line.startswith("HTTP/"):
            st = line.split()[1]
        if line.lower().startswith("set-cookie:"):
            cks.append(line.split(":", 1)[1].split(";")[0].strip())
    return st, "; ".join(cks), body


def strip(t):
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", t)).strip()


def titles(h):
    out = []
    for m in re.finditer(r'<li class="b_algo[^"]*"(.*?)</li>', h, re.S):
        t = re.search(r"<h2[^>]*>(.*?)</h2>", m.group(1), re.S)
        if t:
            out.append(strip(t.group(1)))
    return out


def main():
    q = "esp32"
    # 先落地首页拿 cookie
    st, ck, _ = get("/")
    print("落地首页 cookie 数 =", len(ck.split("; ")))

    st, ck2, b = get("/search?q=%s" % q, cookie=ck)
    h = b.decode("utf-8", "replace")
    t1 = titles(h)
    jar = ck2 if ck2 else ck
    nxt = None
    for m in re.finditer(r'href="([^"]*FORM=PORE[^"]*)"', h):
        nxt = m.group(1).replace("&amp;", "&")
    print("P1 size=%d 结果=%d" % (len(b), len(t1)))
    if not nxt:
        print("无下一页，停止")
        return
    print("下一页 href =", nxt[:120])

    tests = [
        ("A 裸 first=11", "/search?q=%s&first=11&FORM=PORE" % q, None, None),
        ("B FPIG+Referer", nxt, None, "https://cn.bing.com/search?q=%s" % q),
        ("C FPIG+ck+Referer", nxt, jar, "https://cn.bing.com/search?q=%s" % q),
    ]
    for name, path, ckuse, ref in tests:
        st, _, bb = get(path, cookie=ckuse, referer=ref)
        t2 = titles(bb.decode("utf-8", "replace"))
        new = [t for t in t2 if t not in t1]
        print("  %-18s status=%s size=%-7d 结果=%-2d 新增=%d"
              % (name, st, len(bb), len(t2), len(new)))
        for t in new[:4]:
            print("        + ", t[:52])


if __name__ == "__main__":
    main()
