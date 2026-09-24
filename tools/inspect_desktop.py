#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
确认：cn.bing.com + 桌面 Chrome120 UA
  - first= 是否真翻页
  - 页面里的分页链接长什么样（文本 / href），我们的平铺渲染器能不能直接拿来用
  - 时间筛选行还在不在（还在的话要继续过滤）
  - 结果条数 / 体积
"""
import re
import ssl
import socket
import io
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DESKTOP = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
           "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36")
KITKAT = ("Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) "
          "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36")


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


def result_titles(h):
    out = []
    for m in re.finditer(r'<li class="b_algo[^"]*"(.*?)</li>', h, re.S):
        t = re.search(r"<h2[^>]*>(.*?)</h2>", m.group(1), re.S)
        if t:
            out.append(strip(t.group(1)))
    return out


def main():
    print("### A. first= 翻页验证（桌面 UA）")
    seen = []
    for f in (1, 11, 21, 31):
        st, loc, b = get("https://cn.bing.com/search?q=esp32&first=%d" % f)
        h = b.decode("utf-8", "replace")
        ts = result_titles(h)
        seen.append(tuple(ts[:3]))
        print("  first=%-3d size=%-7d 结果=%-2d %s"
              % (f, len(b), len(ts), [x[:34] for x in ts[:2]]))
    print("  翻页有效 =", len({s for s in seen if s}) > 1)

    print()
    print("### B. 第 1 页里的分页链接")
    st, loc, b = get("https://cn.bing.com/search?q=esp32")
    h = b.decode("utf-8", "replace")
    io.open(os.path.join(HERE, "bing_desktop.html"), "w", encoding="utf-8").write(h)
    print("  size=%d  b_algo=%d" % (len(b), len(re.findall(r'<li class="b_algo', h))))
    for m in re.finditer(r'<a[^>]*href="([^"]*first=\d+[^"]*)"[^>]*>(.*?)</a>', h, re.S):
        print("   href=%s" % m.group(1)[:110])
        print("      文本=%s" % strip(m.group(2))[:60])
    print("  -- '下一页' 上下文 --")
    i = h.find("下一页")
    print("   ", strip(h[max(0, i - 400):i + 200])[-320:] if i > 0 else "无")

    print()
    print("### C. 时间筛选 / 导航壳子还在吗")
    for kw in ("filters=ex1", "qpvt=", "FORM=HDRSC", "/images/search", "/videos/search"):
        print("   %-16s x%d" % (kw, h.count(kw)))
    print("   文本 时间不限 x%d / 全部 x%d / 搜索工具 x%d"
          % (h.count("时间不限"), h.count("全部"), h.count("搜索工具")))

    print()
    print("### D. 桌面 UA 会不会触发验证码")
    for kw in ("captcha", "Captcha", "验证", "wappass", "unusual"):
        if kw in h:
            print("   ⚠ 命中", kw)
    print("   检查完毕")

    print()
    print("### E. 其它站点用桌面 UA 是否安全（百度会不会 302 wappass）")
    st, loc, b = get("https://m.baidu.com/s?word=esp32", DESKTOP)
    print("   百度(m) 桌面UA: status=%s loc=%s size=%d" % (st, loc[:80], len(b)))
    st, loc, b = get("https://www.baidu.com/s?wd=esp32", DESKTOP)
    print("   百度(www) 桌面UA: status=%s loc=%s size=%d" % (st, loc[:80], len(b)))


if __name__ == "__main__":
    main()
