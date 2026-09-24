#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
最后一批候选：
  A. 绕过必应地域跳转（cc / mkt / setmkt），看国际版是否给分页
  B. 服务端渲染的小众引擎：Mojeek / SearXNG / Brave / Marginalia
目标：体积 <150KB、结果 >=8 条、翻页参数真换结果、页面里有"下一页"类文本。
"""
import re
import ssl
import socket

UA = ("Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) "
      "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36")


def get(url, ua=UA, timeout=7, depth=0):
    if depth > 5:
        return "loop", "", b""
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
    st, loc = "", ""
    for line in hs.split("\r\n"):
        if line.startswith("HTTP/"):
            st = line.split()[1]
        if line.lower().startswith("location:"):
            loc = line.split(":", 1)[1].strip()
    if st.startswith("3") and loc:
        if loc.startswith("http"):
            return get(loc, ua, timeout, depth + 1)
        if loc.startswith("/"):
            scheme = "https" if port == 443 else "http"
            return get(scheme + "://" + host + loc, ua, timeout, depth + 1)
    return st, loc, body


def strip(t):
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", " ", t)).strip()


def probe(name, url_tpl, pager, vals):
    print("=" * 72)
    print(name)
    seen = []
    for v in vals:
        u = url_tpl.replace("{p}", str(v))
        try:
            st, loc, b = get(u)
        except Exception as e:
            print("   %s=%s -> ERROR %s" % (pager, v, e))
            continue
        h = b.decode("utf-8", "replace")
        ts = [strip(x) for x in re.findall(r"<h[23][^>]*>(.*?)</h[23]>", h, re.S)]
        ts = [t for t in ts if len(t) > 6][:3]
        seen.append(tuple(ts))
        nres = len(re.findall(r'class="[^"]*(?:result|b_algo|res-list|vrwrap|ob)[^"]*"', h))
        nxt = sum(h.count(k) for k in ("下一页", "下一頁", "Next", "next page"))
        print("   %s=%-3s status=%-4s size=%-8d 块=%-3d 下一页类=%-2d %s"
              % (pager, v, st, len(b), nres, nxt, [x[:28] for x in ts]))
    print("   翻页有效 =", len({s for s in seen if s}) > 1)


def main():
    Q = "esp32"
    print("### A. 绕过必应地域跳转")
    probe("bing cc=US", "https://www.bing.com/search?q=%s&cc=US&{p}" % Q, "first", (1, 11, 21))
    probe("bing mkt=en-US", "https://www.bing.com/search?q=%s&mkt=en-US&{p}" % Q, "first", (1, 11))
    probe("bing setmkt=en-US&setlang=en",
          "https://www.bing.com/search?q=%s&setmkt=en-US&setlang=en&{p}" % Q, "first", (1, 11))

    print()
    print("### B. 服务端渲染的小众引擎")
    probe("mojeek", "https://www.mojeek.com/search?q=%s&{p}" % Q, "s", (0, 10, 20))
    probe("searx.be", "https://searx.be/search?q=%s&{p}" % Q, "pageno", (1, 2, 3))
    probe("searx.tiekoetter.com", "https://searx.tiekoetter.com/search?q=%s&{p}" % Q,
          "pageno", (1, 2))
    probe("priv.au", "https://priv.au/search?q=%s&{p}" % Q, "pageno", (1, 2))
    probe("marginalia", "https://search.marginalia.nu/search?query=%s&{p}" % Q, "page", (1, 2))
    probe("brave", "https://search.brave.com/search?q=%s&{p}" % Q, "offset", (0, 1, 2))


if __name__ == "__main__":
    main()
