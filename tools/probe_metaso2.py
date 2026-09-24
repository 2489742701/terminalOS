#!/usr/bin/env python3
"""抓 metaso.cn/search/<q> 的 SSR HTML，看里面有没有可解析的正文 / 结构化数据。

ESP32 端能不能用，取决于：
  1) HTML 里是否直接带 AI 回答的正文（不用跑 JS）；
  2) 有没有 __NEXT_DATA__ 之类的 JSON 岛可以直接抠。
"""
import urllib.request
import urllib.parse
import ssl
import re
import json
import sys

ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

UA = ("Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) "
      "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36")

q = sys.argv[1] if len(sys.argv) > 1 else "esp32"
url = "https://metaso.cn/search/" + urllib.parse.quote(q)
print("URL:", url)

req = urllib.request.Request(url, headers={"User-Agent": UA,
                                           "Accept": "text/html"})
r = urllib.request.urlopen(req, timeout=25, context=ctx)
data = r.read()
print("status=%s len=%d ctype=%s" % (r.status, len(data), r.headers.get("Content-Type")))
html = data.decode("utf-8", "replace")
print("charset-ok, html chars=%d" % len(html))

# 1) __NEXT_DATA__ 岛
m = re.search(r'<script id="__NEXT_DATA__"[^>]*>(.*?)</script>', html, re.S)
if m:
    blob = m.group(1)
    print("\n[__NEXT_DATA__] len=%d" % len(blob))
    try:
        j = json.loads(blob)
        def walk(o, path="", depth=0):
            if depth > 4:
                return
            if isinstance(o, dict):
                for k, v in o.items():
                    if isinstance(v, (dict, list)):
                        print("  " * depth + "  %s.%s (%s)" % (path, k, type(v).__name__))
                        walk(v, "%s.%s" % (path, k), depth + 1)
                    else:
                        s = str(v)
                        print("  " * depth + "  %s.%s = %s" % (path, k, s[:90]))
            elif isinstance(o, list):
                print("  " * depth + "  %s[ ] len=%d" % (path, len(o)))
                if o:
                    walk(o[0], "%s[0]" % path, depth + 1)
        walk(j.get("props", {}), "props")
    except Exception as e:
        print("  json parse fail:", e)
else:
    print("\n[__NEXT_DATA__] NOT FOUND")

# 2) 正文里有没有成段的中文（去掉 script/style 后）
body = re.sub(r"<script[^>]*>.*?</script>", " ", html, flags=re.S)
body = re.sub(r"<style[^>]*>.*?</style>", " ", body, flags=re.S)
text = re.sub(r"<[^>]+>", "\n", body)
text = re.sub(r"\n{2,}", "\n", text)
paras = [p.strip() for p in text.split("\n") if p.strip()]
print("\n[TEXT] 段落数=%d  中文段落数=%d" %
      (len(paras), sum(1 for p in paras if re.search(r"[\u4e00-\u9fff]", p))))
for p in paras[:40]:
    if re.search(r"[\u4e00-\u9fff]", p):
        print("   >", p[:120])
