#!/usr/bin/env python3
"""在 PC 上探 metaso.cn 有哪些端点能裸请求（不带 key）。

目的：判断 ESP32 端能不能"直接把请求给过去"拿到结果，
还是必须走官方 API（要 key）。
"""
import urllib.request
import urllib.error
import ssl
import json

ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

UA = ("Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) "
      "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36")


def get(url, headers=None):
    h = {"User-Agent": UA, "Accept": "*/*"}
    if headers:
        h.update(headers)
    req = urllib.request.Request(url, headers=h)
    try:
        r = urllib.request.urlopen(req, timeout=20, context=ctx)
        data = r.read()
        return r.status, dict(r.headers), data
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()
    except Exception as e:
        return -1, {}, str(e).encode()


def post(url, body, headers=None):
    h = {"User-Agent": UA, "Accept": "application/json",
         "Content-Type": "application/json"}
    if headers:
        h.update(headers)
    req = urllib.request.Request(url, data=body.encode(), headers=h,
                                 method="POST")
    try:
        r = urllib.request.urlopen(req, timeout=25, context=ctx)
        return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()
    except Exception as e:
        return -1, {}, str(e).encode()


def show(tag, url, st, hdr, data, n=500):
    print("=" * 70)
    print("[%s] %s" % (tag, url))
    print("  status=%s len=%s ctype=%s enc=%s" %
          (st, len(data), hdr.get("Content-Type"), hdr.get("Content-Encoding")))
    body = data[:n]
    try:
        print("  body:", body.decode("utf-8", "replace").replace("\n", " ")[:n])
    except Exception:
        print("  body(raw):", body[:n])


# 1. 首页：看是不是 SPA（HTML 里有没有内容）
for u in ["https://metaso.cn/", "https://metaso.cn/search?q=esp32",
          "https://metaso.cn/s/esp32", "https://metaso.cn/search/esp32"]:
    st, h, d = get(u)
    show("GET", u, st, h, d, 300)

# 2. 官方 API v1（无 key）
st, h, d = post("https://metaso.cn/api/v1/search",
                json.dumps({"q": "esp32", "scope": "webpage", "size": "5"}))
show("POST no-key", "https://metaso.cn/api/v1/search", st, h, d, 400)

# 3. MCP 端点（无 key）
st, h, d = post("https://metaso.cn/api/mcp",
                json.dumps({"q": "esp32"}))
show("POST no-key", "https://metaso.cn/api/mcp", st, h, d, 400)
