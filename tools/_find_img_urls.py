# -*- coding: utf-8 -*-
"""找几个真实 JPEG / PNG 直链，给设备 imgtest 用（顺带在 PC 侧先量尺寸）。"""
import re, io, ssl, urllib.request, struct

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


def peek(d):
    if len(d) > 24 and d[:4] == b'\x89PNG':
        w = struct.unpack('>I', d[16:20])[0]
        h = struct.unpack('>I', d[20:24])[0]
        return 'PNG', w, h
    if len(d) > 4 and d[0] == 0xFF and d[1] == 0xD8:
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
                h = (d[i + 5] << 8) | d[i + 6]
                w = (d[i + 7] << 8) | d[i + 8]
                return 'JPEG', w, h
            seg = (d[i + 2] << 8) | d[i + 3]
            if seg < 2:
                return 'JPEG?', 0, 0
            i += 2 + seg
        return 'JPEG?', 0, 0
    return '?', 0, 0


PAGES = ['https://www.ithome.com/', 'https://www.baidu.com/',
         'https://news.qq.com/', 'https://www.163.com/']

for pg in PAGES:
    try:
        html = get(pg).decode('utf-8', 'ignore')
    except Exception as e:
        print('FAIL', pg, e)
        continue
    urls = re.findall(r'<img[^>]+?src=["\']([^"\']+)["\']', html, re.I)
    urls += re.findall(r'data-src=["\']([^"\']+)["\']', html, re.I)
    urls += re.findall(r'data-original=["\']([^"\']+)["\']', html, re.I)
    seen = []
    for x in urls:
        if x.startswith('//'):
            x = 'https:' + x
        if x.startswith('http') and re.search(r'\.(jpg|jpeg|png)$', x, re.I):
            if x not in seen:
                seen.append(x)
    print('==', pg, len(seen))
    for u in seen[:5]:
        try:
            d = get(u, ref=pg, timeout=12)
            k, w, h = peek(d)
            print('   %-5s %5dx%-5d %7d B  %s' % (k, w, h, len(d), u[:110]))
        except Exception as e:
            print('   ERR', type(e).__name__, u[:100])
