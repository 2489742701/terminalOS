#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
最后一轮：
  1) 必应中国站，换 UA（KitKat / Android13 / 桌面 Chrome120 / iPhone）看结果数与分页
  2) 搜狗移动端：正确抽取结果标题，验证 page= 是否真翻页，并看"下一页"长什么样
"""
import re
import ssl
import socket
import io
import os

HERE = os.path.dirname(os.path.abspath(__file__))

UAS = {
    "KitKat/Chrome30": ("Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) "
                        "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36"),
    "Android13/Ch120": ("Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 "
                        "(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36"),
    "Desktop/Ch120": ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                      "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"),
    "iPhone/Safari17": ("Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) "
                        "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1"),
}


def get(url, ua, timeout=12, depth=0):
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


def main():
    print("### 1) cn.bing.com/search?q=esp32  ×  UA")
    print("   %-18s %-8s %-6s %-6s %-6s %s" % ("UA", "size", "b_algo", "h2", "下一页", "first=链接"))
    for name, ua in UAS.items():
        try:
            st, loc, b = get("https://cn.bing.com/search?q=esp32", ua)
        except Exception as e:
            print("   %-18s ERROR %s" % (name, e))
            continue
        h = b.decode("utf-8", "replace")
        print("   %-18s %-8d %-6d %-6d %-6d %d"
              % (name, len(b), len(re.findall(r'<li class="b_algo', h)),
                 len(re.findall(r"<h2", h)), h.count("下一页"),
                 len(re.findall(r'href="[^"]*first=\d+', h))))

    print()
    print("### 1b) 桌面 UA 下 first= 翻页？")
    ua = UAS["Desktop/Ch120"]
    seen = []
    for f in (1, 11, 21):
        try:
            st, loc, b = get("https://cn.bing.com/search?q=esp32&first=%d" % f, ua)
        except Exception as e:
            print("   first=%d ERROR %s" % (f, e))
            continue
        h = b.decode("utf-8", "replace")
        ts = [strip(x) for x in re.findall(r'<li class="b_algo.*?<h2>(.*?)</h2>', h, re.S)]
        seen.append(tuple(ts[:3]))
        print("   first=%-3d size=%-8d 结果=%-3d 首条=%s"
              % (f, len(b), len(ts), (ts[0][:45] if ts else "<none>")))
    print("   翻页有效 =", len(set(seen)) > 1)

    print()
    print("### 2) 搜狗移动端")
    base = "https://m.sogou.com/web/searchList.jsp?keyword=esp32"
    seen = []
    for pg in (1, 2, 3):
        try:
            st, loc, b = get("%s&page=%d" % (base, pg))
        except Exception as e:
            print("   page=%d ERROR %s" % (pg, e))
            continue
        h = b.decode("utf-8", "replace")
        if pg == 1:
            io.open(os.path.join(HERE, "sogou_m.html"), "w", encoding="utf-8").write(h)
        # 搜狗结果标题常见在 <h3 class="vr-title"> 或 <div class="rwgr-title">
        ts = [strip(x) for x in re.findall(r'<h3[^>]*>(.*?)</h3>', h, re.S)]
        ts = [t for t in ts if len(t) > 4]
        seen.append(tuple(ts[:3]))
        print("   page=%d status=%s size=%-8d h3=%-3d %s"
              % (pg, st, len(b), len(ts), [x[:26] for x in ts[:3]]))
    print("   翻页有效 =", len({s for s in seen if s}) > 1)

    if os.path.exists(os.path.join(HERE, "sogou_m.html")):
        h = io.open(os.path.join(HERE, "sogou_m.html"), encoding="utf-8").read()
        idx = h.find("下一页")
        print("   '下一页' @%d -> %s" % (idx, strip(h[max(0, idx - 300):idx + 200])[:300]))


if __name__ == "__main__":
    main()
