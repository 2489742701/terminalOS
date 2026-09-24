#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
两套假设的验证：
  H1 必应的分页要靠 cookie（先落地首页拿 Set-Cookie，再带 cookie 请求）
  H2 换端点：360 / 搜狗 移动端直连（不 302），看 pn/page 是否真翻页
"""
import re
import ssl
import socket

UA = ("Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) "
      "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36")
ANDROID13 = ("Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 "
             "(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36")


def get(url, ua=UA, cookie=None, timeout=20):
    m = re.match(r"https?://([^/]+)(/.*)?$", url)
    host, path = m.group(1), (m.group(2) or "/")
    port = 443 if url.startswith("https") else 80
    s = socket.create_connection((host, port), timeout=timeout)
    if port == 443:
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        s = ctx.wrap_socket(s, server_hostname=host)
    ck = ("Cookie: " + cookie + "\r\n") if cookie else ""
    req = ("GET " + path + " HTTP/1.1\r\n"
           "Host: " + host + "\r\n"
           "User-Agent: " + ua + "\r\n"
           "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
           "Accept-Language: zh-CN,zh;q=0.9\r\n"
           + ck +
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
        if len(buf) > 3 * 1024 * 1024:
            break
    s.close()
    head, _, body = buf.partition(b"\r\n\r\n")
    hs = head.decode("utf-8", "replace")
    st = ""
    cookies = []
    for line in hs.split("\r\n"):
        if line.startswith("HTTP/"):
            st = line.split()[1]
        if line.lower().startswith("set-cookie:"):
            cookies.append(line.split(":", 1)[1].split(";")[0].strip())
    return st, "; ".join(cookies), body


def strip(t):
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", " ", t)).strip()


def titles(h, n=3):
    ts = [strip(x) for x in re.findall(r"<h[23][^>]*>(.*?)</h[23]>", h, re.S)]
    return [t for t in ts if t][:n]


def main():
    print("### H1: 必应 cookie 假设")
    st, ck, _ = get("https://cn.bing.com/", ANDROID13)
    print("  落地首页 status=%s  cookies=%d  [%s...]" % (st, len(ck.split('; ')), ck[:110]))
    for tag, ua, ckuse in (("无cookie", ANDROID13, None), ("有cookie", ANDROID13, ck)):
        st, _, b = get("https://cn.bing.com/search?q=esp32", ua, ckuse)
        h = b.decode("utf-8", "replace")
        print("  %s: size=%d h2=%d 下一页=%d b_pag=%d first=链接=%d"
              % (tag, len(b), len(re.findall(r"<h2", h)), h.count("下一页"),
                 h.count("b_pag"), len(re.findall(r'href="[^"]*first=\d+', h))))
    print("  -- 带 cookie 时 first= 是否翻页 --")
    seen = []
    for f in (1, 11, 21):
        st, _, b = get("https://cn.bing.com/search?q=esp32&first=%d" % f, ANDROID13, ck)
        h = b.decode("utf-8", "replace")
        t = titles(h, 1)
        seen.append(t[0] if t else "?")
        print("     first=%-3d size=%-7d 首条=%s" % (f, len(b), (t[0][:45] if t else "<none>")))
    print("     翻页有效 =", len(set(seen)) > 1)

    print()
    print("### H2a: 360 移动端直连 m.so.com/s?q=&pn=")
    seen = []
    for pn in (1, 2, 3):
        st, _, b = get("https://m.so.com/s?q=esp32&pn=%d" % pn)
        h = b.decode("utf-8", "replace")
        t = titles(h, 2)
        seen.append(tuple(t))
        print("  pn=%d status=%s size=%-8d 结果=%s" % (pn, st, len(b), [x[:32] for x in t]))
    print("  翻页有效 =", len(set(seen)) > 1)
    st, _, b = get("https://m.so.com/s?q=esp32&pn=1")
    h = b.decode("utf-8", "replace")
    print("  下一页 x%d, 页链接 x%d" % (h.count("下一页"), len(re.findall(r'href="[^"]*pn=\d+', h))))

    print()
    print("### H2b: 搜狗移动端直连")
    seen = []
    for pg in (1, 2, 3):
        u = ("https://m.sogou.com/web/searchList.jsp?s_from=pcsearch"
             "&keyword=esp32&page=%d" % pg)
        st, _, b = get(u)
        h = b.decode("utf-8", "replace")
        t = titles(h, 2)
        seen.append(tuple(t))
        print("  page=%d status=%s size=%-8d 结果=%s" % (pg, st, len(b), [x[:32] for x in t]))
    print("  翻页有效 =", len(set(seen)) > 1)

    print()
    print("### H2c: 必应 RSS 翻页")
    for f in (1, 11):
        st, _, b = get("https://cn.bing.com/search?q=esp32&format=rss&first=%d" % f)
        h = b.decode("utf-8", "replace")
        items = re.findall(r"<title>(.*?)</title>", h, re.S)[1:3]
        print("  first=%-3d size=%-6d item前2=%s" % (f, len(b), [strip(x)[:30] for x in items]))
    print("  RSS count 参数:")
    for cnt in (10, 20, 30):
        st, _, b = get("https://cn.bing.com/search?q=esp32&format=rss&count=%d" % cnt)
        h = b.decode("utf-8", "replace")
        n = len(re.findall(r"<item>", h))
        print("     count=%-3d size=%-6d <item>=%d" % (cnt, len(b), n))


if __name__ == "__main__":
    main()
