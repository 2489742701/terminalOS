# -*- coding: utf-8 -*-
"""探测 news.orz.ai 支持哪些平台，重点确认 360 / 豆瓣 能不能拉到、各多少条。"""
import json, urllib.request, ssl

HOST = "https://news.orz.ai"
CANDS = ["360", "so360", "360search", "baidu", "douban", "weibo", "zhihu",
         "36kr", "bilibili", "juejin", "github", "hackernews", "toutiao",
         "qq", "netease", "sina", "itheima", "csdn"]

ctx = ssl.create_default_context()
for p in CANDS:
    url = "%s/api/v1/dailynews/?platform=%s" % (HOST, p)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=20, context=ctx) as r:
            body = r.read()
        try:
            j = json.loads(body)
        except Exception:
            print("%-12s HTTP %s  %6d B  (not json)" % (p, r.status, len(body)))
            continue
        items = j.get("data") or j.get("items") or []
        if isinstance(items, dict):
            items = list(items.values())[0] if items else []
        print("%-12s HTTP %s  %6d B  items=%d" % (p, r.status, len(body), len(items)))
        if items and p in ("360", "so360", "360search", "douban"):
            it = items[0]
            print("     sample keys:", list(it.keys())[:8])
            print("     title:", str(it.get("title"))[:60])
            print("     url  :", str(it.get("url"))[:80])
    except Exception as e:
        print("%-12s FAIL %s" % (p, e))
