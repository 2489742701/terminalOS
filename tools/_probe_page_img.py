# -*- coding: utf-8 -*-
"""用设备同款 KitKat UA 抓页面，数一数到底有多少 <img>（以及它们长什么样）。"""
import re, ssl, sys, urllib.request

UA = ("Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) "
      "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36")
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

url = sys.argv[1] if len(sys.argv) > 1 else 'https://www.163.com/'
html = urllib.request.urlopen(
    urllib.request.Request(url, headers={'User-Agent': UA}),
    timeout=20, context=ctx).read().decode('utf-8', 'ignore')

print('len', len(html))
print('count "<img"      :', html.count('<img'))
print('count "<IMG"      :', len(re.findall(r'<img', html, re.I)))
print('count "src="      :', len(re.findall(r'\bsrc\s*=', html, re.I)))
print('count "data-src"  :', len(re.findall(r'data-src\s*=', html, re.I)))
print('count "noscript"  :', len(re.findall(r'<noscript', html, re.I)))
print('count ".jpg/.png" :', len(re.findall(r'\.(?:jpg|jpeg|png)', html, re.I)))

tags = re.findall(r'<img[^>]*>', html, re.I)
print('--- first 8 <img> tags ---')
for t in tags[:8]:
    print('  ', t[:170])

lazy = re.findall(r'data-(?:src|original|echo|url)=["\']([^"\']{10,120})', html, re.I)
print('--- lazy attrs:', len(lazy), '---')
for x in lazy[:8]:
    print('  ', x[:140])
