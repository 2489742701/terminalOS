#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
候选搜索端点横评：找一个「服务端渲染 + 真分页 + 体积小」的 SERP 端点。

评估维度：
  - 体积（设备 HTTPS 上限 786KB，越小越好；<150KB 才算舒服）
  - 服务端渲染的结果条数（JS 渲染 = 0 条，直接出局）
  - 结果条数（≥8 才有意义）
  - 翻页参数是否真的换结果
  - 页面里有没有可以直接点的"下一页"链接
"""
import re
import ssl
import socket

UA = ("Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) "
      "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36")


def raw_get(url, ua=UA, timeout=20):
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
    st = loc = ""
    for line in hs.split("\r\n"):
        if line.startswith("HTTP/"):
            st = line.split()[1]
        if line.lower().startswith("location:"):
            loc = line.split(":", 1)[1].strip()
    return st, loc, body


def strip(t):
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", " ", t)).strip()


CANDIDATES = [
    ("bing  cn  (现状)", "https://cn.bing.com/search?q=", None),
    ("bing  cn  RSS ", "https://cn.bing.com/search?q=", "rss"),
    ("ddg   html   ", "https://html.duckduckgo.com/html/?q=", "s"),
    ("ddg   lite   ", "https://lite.duckduckgo.com/lite/?q=", "s"),
    ("360   so.com ", "https://www.so.com/s?q=", "pn"),
    ("sogou web    ", "https://www.sogou.com/web?query=", "page"),
    ("baidu m      ", "https://m.baidu.com/s?word=", "pn"),
]

Q = "esp32"


def probe(name, base, pager):
    print("=" * 72)
    print(name, " base=", base)
    if pager == "rss":
        url = base + Q + "&format=rss"
        st, loc, body = raw_get(url)
        h = body.decode("utf-8", "replace")
        items = re.findall(r"<item>(.*?)</item>", h, re.S)
        print("  status=%s size=%d  <item>=%d" % (st, len(body), len(items)))
        for it in items[:3]:
            t = re.search(r"<title>(.*?)</title>", it, re.S)
            print("    -", strip(t.group(1))[:60] if t else "?")
        return
    url = base + Q
    try:
        st, loc, body = raw_get(url)
    except Exception as e:
        print("  ERROR", e)
        return
    h = body.decode("utf-8", "replace")
    if st.startswith("3"):
        print("  status=%s -> %s (size=%d)" % (st, loc[:90], len(body)))
        if loc.startswith("http"):
            try:
                st, loc2, body = raw_get(loc)
            except Exception as e:
                print("    跟随失败", e)
                return
            h = body.decode("utf-8", "replace")
            print("  跟随: status=%s size=%d" % (st, len(body)))
    # 结果条数：各站点不同，用通用 <a href="http 且文本非空> 在结果容器里不好取，
    # 这里退而求其次统计 <h2>/<h3> 与 result 类名
    n_h2 = len(re.findall(r"<h2[^>]*>", h))
    n_h3 = len(re.findall(r"<h3[^>]*>", h))
    n_res_cls = len(re.findall(r'class="[^"]*(?:result|b_algo|res-list|vrwrap)[^"]*"', h))
    print("  size=%d  <h2>=%d <h3>=%d  result-class=%d"
          % (len(body), n_h2, n_h3, n_res_cls))
    for kw in ("下一页", "下一頁", "下页", "Next", "下一张"):
        c = h.count(kw)
        if c:
            print("    %s x%d" % (kw, c))
    if pager:
        seen = []
        for off in ((1, 1), (2, 10 if pager == "s" else 2), (3, 20 if pager == "s" else 3)):
            pg, val = off
            u = "%s%s&%s=%d" % (base, Q, pager, val)
            try:
                st2, l2, b2 = raw_get(u)
            except Exception as e:
                print("    p%s ERROR %s" % (pg, e))
                continue
            h2 = b2.decode("utf-8", "replace")
            ts = [strip(x) for x in re.findall(r"<h[23][^>]*>(.*?)</h[23]>", h2, re.S)][:3]
            seen.append(tuple(ts))
            print("    %s=%-3d size=%-8d 首条=%s" % (pager, val, len(b2), (ts[0][:45] if ts else "<none>")))
        if len(seen) >= 2:
            print("    翻页有效 =", len(set(seen)) > 1)


def main():
    for name, base, pager in CANDIDATES:
        try:
            probe(name, base, pager)
        except Exception as e:
            print(name, "EXCEPTION", e)


if __name__ == "__main__":
    main()
