# -*- coding: utf-8 -*-
"""找一张宽度 > 500 的真实 JPEG，用来验证"解码时降采样"那条路径。"""
import re, io, ssl, struct, urllib.request

UA = ("Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) "
      "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36")
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE


def get(u, ref=None, timeout=15):
    h = {'User-Agent': UA}
    if ref:
        h['Referer'] = ref
    return urllib.request.urlopen(urllib.request.Request(u, headers=h),
                                  timeout=timeout, context=ctx).read()


def jpeg_size(d):
    i = 2
    while i + 9 < len(d):
        if d[i] != 0xFF:
            i += 1
            continue
        m = d[i + 1]
        if m in (0xD8, 0x01) or 0xD0 <= m <= 0xD7:
            i += 2
            continue
        if 0xC0 <= m <= 0xCF and m not in (0xC4, 0xC8, 0xCC):
            return (d[i + 7] << 8) | d[i + 8], (d[i + 5] << 8) | d[i + 6]
        seg = (d[i + 2] << 8) | d[i + 3]
        if seg < 2:
            return 0, 0
        i += 2 + seg
    return 0, 0


PAGES = ['https://www.163.com/', 'https://news.163.com/', 'https://www.sohu.com/',
         'https://www.ifeng.com/', 'https://www.ithome.com/',
         'https://baike.baidu.com/', 'https://www.zhihu.com/',
         'https://www.qq.com/', 'https://www.sina.com.cn/',
         'https://www.meituan.com/', 'https://www.douban.com/']

hits = []
for pg in PAGES:
    try:
        html = get(pg, timeout=12).decode('utf-8', 'ignore')
    except Exception as e:
        print('FAIL', pg, type(e).__name__)
        continue
    urls = re.findall(r'<img[^>]+?src=["\']([^"\']+)["\']', html, re.I)
    urls += re.findall(r'data-(?:src|original)=["\']([^"\']+)["\']', html, re.I)
    seen = []
    for x in urls:
        if x.startswith('//'):
            x = 'https:' + x
        if x.startswith('http') and re.search(r'\.jpe?g', x, re.I):
            if x not in seen:
                seen.append(x)
    for u in seen[:4]:
        try:
            d = get(u, ref=pg, timeout=12)
            w, h = jpeg_size(d)
            if w > 500:
                hits.append((w, h, len(d), u))
                print('BIG %5dx%-5d %8d B  %s' % (w, h, len(d), u[:120]))
        except Exception:
            pass
    print('..', pg, 'done (%d jpg urls)' % len(seen))

print('--- big jpegs: %d' % len(hits))
for w, h, n, u in hits[:8]:
    print('%dx%d %dB %s' % (w, h, n, u[:140]))
