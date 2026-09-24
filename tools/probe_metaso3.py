#!/usr/bin/env python3
"""用现代桌面 UA 再抓一次 /search/<q>，看 SSR HTML 里有没有 __NEXT_DATA__ /
AI 回答正文。用来判断"裸抓网页"这条路对 metaso 是否可行。"""
import urllib.request
import urllib.parse
import ssl
import re
import json
import sys

ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36")

q = sys.argv[1] if len(sys.argv) > 1 else "esp32是什么"
url = "https://metaso.cn/search/" + urllib.parse.quote(q)
print("URL:", url)

req = urllib.request.Request(url, headers={
    "User-Agent": UA,
    "Accept": "text/html,application/xhtml+xml",
    "Accept-Language": "zh-CN,zh;q=0.9",
})
r = urllib.request.urlopen(req, timeout=25, context=ctx)
data = r.read()
html = data.decode("utf-8", "replace")
print("status=%s len=%d" % (r.status, len(data)))

for pat in ['<script id="__NEXT_DATA__"', 'self.__next_f', 'application/json',
            'stream', 'event-stream', 'text/event-stream']:
    print("  contains %-28s : %s" % (pat, pat in html))

body = re.sub(r"<script[^>]*>.*?</script>", " ", html, flags=re.S)
body = re.sub(r"<style[^>]*>.*?</style>", " ", body, flags=re.S)
text = re.sub(r"<[^>]+>", "\n", body)
paras = [p.strip() for p in text.split("\n") if p.strip()]
cn = [p for p in paras if re.search(r"[\u4e00-\u9fff]", p)]
print("\n[TEXT] 段落=%d 中文段落=%d" % (len(paras), len(cn)))
for p in cn[:25]:
    print("   >", p[:130])
