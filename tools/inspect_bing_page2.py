"""验证：① 时间筛选链接跳过去是否正常 ② 自己拼 &first=11 能不能拿到第 2 页。"""
import urllib.request, ssl, re, sys
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
UA = ('Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) '
      'AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36')


def get(url):
    req = urllib.request.Request(url, headers={'User-Agent': UA, 'Accept': 'text/html'})
    r = urllib.request.urlopen(req, timeout=25, context=ctx)
    return r.status, r.read().decode('utf-8', 'replace')


def titles(d):
    out = []
    for m in re.finditer(r'<h2[^>]*>(.*?)</h2>', d, re.S):
        t = re.sub(r'<[^>]+>', '', m.group(1)).strip()
        if t:
            out.append(t[:46])
    return out


tests = [
    ('第1页(基准)', 'https://cn.bing.com/search?q=esp32'),
    ('时间筛选 24小时', 'https://cn.bing.com/search?q=esp32&filters=ex1%3a%22ez1%22&FORM=000017'),
    ('自拼 first=11', 'https://cn.bing.com/search?q=esp32&first=11'),
    ('自拼 first=21', 'https://cn.bing.com/search?q=esp32&first=21'),
    ('自拼 page=2', 'https://cn.bing.com/search?q=esp32&page=2'),
]
for name, u in tests:
    try:
        st, d = get(u)
        ts = titles(d)
        print('%-16s status=%d len=%d b_algo=%d 下一页=%d' %
              (name, st, len(d), d.count('b_algo'), d.count('下一页')))
        for t in ts[:3]:
            print('    -', t)
        if not ts:
            print('    (无 h2 结果)')
    except Exception as e:
        print('%-16s ERR %s' % (name, e))
    print()
