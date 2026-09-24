"""查必应 SERP 里的"时间筛选"链接和"下一页"分页链接到底长什么样。"""
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

url = sys.argv[1] if len(sys.argv) > 1 else 'https://cn.bing.com/search?q=esp32'
req = urllib.request.Request(url, headers={'User-Agent': UA, 'Accept': 'text/html'})
d = urllib.request.urlopen(req, timeout=25, context=ctx).read().decode('utf-8', 'replace')
print('URL', url, 'len', len(d))

print('\n=== 关键词出现次数 ===')
for kw in ['下一页', '上一页', '全部', '24 小时', '24小时', '一周内', '一个月内', '去年',
           'b_algo', 'b_pag', 'sb_pag', 'page=']:
    print('%-12s %d' % (kw, d.count(kw)))

print('\n=== 含"小时/周/月/年"的 <a> ===')
for m in list(re.finditer(r'<a[^>]*>(?:(?!</a>).)*?(?:小时|周内|月内|去年)[^<]*</a>', d, re.S))[:8]:
    s = re.sub(r'\s+', ' ', m.group(0))
    print(s[:280])
    print()

print('=== 含"下一页"的 <a> ===')
for m in list(re.finditer(r'<a[^>]*>(?:(?!</a>).)*?下一页[^<]*</a>', d, re.S))[:5]:
    s = re.sub(r'\s+', ' ', m.group(0))
    print(s[:280])
    print()

print('=== 分页容器 b_pag / 含 first= 的链接 ===')
for m in list(re.finditer(r'<a[^>]+href="([^"]*first=[^"]*)"[^>]*>(.*?)</a>', d, re.S))[:8]:
    txt = re.sub(r'<[^>]+>', '', m.group(2)).strip()
    print('href=', m.group(1)[:120], '| text=', txt[:40])
