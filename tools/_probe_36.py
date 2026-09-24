# -*- coding: utf-8 -*-
"""1) 把所有"36 开头"的平台名都试一遍，确认 news.orz.ai 到底认哪个。
   2) 验证 360 搜索（so.com）作为**搜索引擎**能不能打开、结果页长什么样。"""
import json, urllib.request, ssl, gzip, io, urllib.parse

HOST = "https://news.orz.ai"
NAMES = ["36kr", "36krnews", "36", "36dian", "360", "360doc", "360news",
         "so360", "360search", "36ke", "kr36", "36krcom"]

ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36")

print("── news.orz.ai：36 开头的候选 ──")
for p in NAMES:
    url = "%s/api/v1/dailynews/?platform=%s" % (HOST, p)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": UA})
        with urllib.request.urlopen(req, timeout=20, context=ctx) as r:
            j = json.loads(r.read())
        items = j.get("data") or []
        print("  %-12s %6d B  items=%d" % (p, len(r.read()) if False else 0, len(items)))
    except Exception as e:
        print("  %-12s FAIL %s" % (p, e))

print("\n── 360 搜索作为搜索引擎 ──")
for base in ["https://www.so.com/s?q=%s", "https://www.so.com/s?ie=utf-8&q=%s",
             "https://m.so.com/s?q=%s"]:
    u = base % urllib.parse.quote("天气预报")
    try:
        req = urllib.request.Request(u, headers={
            "User-Agent": UA, "Accept": "text/html,*/*", "Accept-Encoding": "gzip"})
        with urllib.request.urlopen(req, timeout=20, context=ctx) as r:
            raw = r.read()
            if r.headers.get("Content-Encoding") == "gzip":
                raw = gzip.GzipFile(fileobj=io.BytesIO(raw)).read()
        print("  %-46s -> %s  %7d B" % (u[:46], r.status, len(raw)))
    except Exception as e:
        print("  %-46s -> FAIL %s" % (u[:46], e))
