"""不同 UA 下必应给不给分页（b_pag / 下一页 / first=）。"""
import urllib.request, ssl, re, sys
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

UAS = {
    'KitKat(当前)': ('Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) '
                     'AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36'),
    'Android13-Chrome': ('Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 '
                         '(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36'),
    'Win-Chrome120': ('Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 '
                      '(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36'),
    'iPhone-Safari': ('Mozilla/5.0 (iPhone; CPU iPhone OS 16_6 like Mac OS X) '
                      'AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1'),
}


def titles(d):
    out = []
    for m in re.finditer(r'<h2[^>]*>(.*?)</h2>', d, re.S):
        t = re.sub(r'<[^>]+>', '', m.group(1)).strip()
        if t:
            out.append(t[:40])
    return out


for name, ua in UAS.items():
    try:
        req = urllib.request.Request('https://cn.bing.com/search?q=esp32',
                                     headers={'User-Agent': ua, 'Accept': 'text/html'})
        d = urllib.request.urlopen(req, timeout=25, context=ctx).read().decode('utf-8', 'replace')
        ts = titles(d)
        print('%-18s len=%-7d b_algo=%-3d b_pag=%-3d 下一页=%-3d 外部CSS=%d' %
              (name, len(d), d.count('b_algo'), d.count('b_pag'),
               d.count('下一页'), len(re.findall(r'<link[^>]+stylesheet', d, re.I))))
        for t in ts[:2]:
            print('     p1:', t)
    except Exception as e:
        print('%-18s ERR %s' % (name, e))
        continue

    # 第 2 页
    try:
        req2 = urllib.request.Request('https://cn.bing.com/search?q=esp32&first=11',
                                      headers={'User-Agent': ua, 'Accept': 'text/html'})
        d2 = urllib.request.urlopen(req2, timeout=25, context=ctx).read().decode('utf-8', 'replace')
        ts2 = titles(d2)
        same = (ts2[:3] == ts[:3])
        print('     first=11 -> 结果%s' % ('与第1页相同(分页无效)' if same else '不同(分页有效!)'))
        for t in ts2[:2]:
            print('     p2:', t)
    except Exception as e:
        print('     p2 ERR', e)
    print()
