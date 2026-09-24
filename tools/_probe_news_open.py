# -*- coding: utf-8 -*-
"""实测各新闻源**前几条真实链接**在设备浏览器里能不能打开（看 HTTP 状态 + 体积）。
这决定"金色小星星"该给谁 —— master 的标准是：能正常打开、能看到里面的内容。
"""
import json, urllib.request, ssl, gzip, io

HOST = "https://news.orz.ai"
PLATS = ["baidu", "weibo", "zhihu", "36kr", "bilibili", "juejin",
         "github", "hackernews", "douban"]

ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36")


def get(url, timeout=15):
    req = urllib.request.Request(url, headers={
        "User-Agent": UA,
        "Accept": "text/html,application/xhtml+xml,*/*",
        "Accept-Encoding": "gzip",
    })
    with urllib.request.urlopen(req, timeout=timeout, context=ctx) as r:
        raw = r.read()
        if r.headers.get("Content-Encoding") == "gzip":
            raw = gzip.GzipFile(fileobj=io.BytesIO(raw)).read()
        return r.status, len(raw), r.geturl()


for p in PLATS:
    url = "%s/api/v1/dailynews/?platform=%s" % (HOST, p)
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=20, context=ctx) as r:
        j = json.loads(r.read())
    items = j.get("data") or []
    print("== %s (%d 条)" % (p, len(items)))
    ok = 0
    for it in items[:4]:
        u = it.get("url", "")
        if not u.startswith("http"):
            print("     (no url)", str(it.get("title"))[:40])
            continue
        try:
            st, n, final = get(u)
            flag = "OK " if st == 200 and n > 2000 else "?? "
            if st == 200 and n > 2000:
                ok += 1
            print("     %s %s %7d B  %s" % (flag, st, n, u[:70]))
        except Exception as e:
            print("     ERR %s  %s" % (type(e).__name__, str(e)[:40]))
    print("     -> 前 4 条里能打开 %d 条" % ok)
