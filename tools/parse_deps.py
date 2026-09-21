import json, os, sys, urllib.request, ssl

idxPath = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\arduino-data\package_index.json'
with open(idxPath, 'r', encoding='utf-8') as f:
    idx = json.load(f)

# 3.3.11 依赖
deps = []
for pkg in idx.get('packages', []):
    if pkg['name'] != 'esp32':
        continue
    for p in pkg.get('platforms', []):
        if p.get('version') == '3.3.11':
            for d in p.get('toolsDependencies', []):
                deps.append((d['name'], d['version']))
depSet = set(deps)

urls = {}
for pkg in idx.get('packages', []):
    for t in pkg.get('tools', []):
        tname = t.get('name')
        vers = [t] if 'version' in t and 'systems' in t else t.get('versions', [])
        for ver in vers:
            vver = ver.get('version')
            if (tname, vver) not in depSet or (tname, vver) in urls:
                continue
            systems = ver.get('systems', [])
            # 优先 x86_64-mingw32 (Windows 64-bit)
            for s in systems:
                h = s.get('host', '').lower()
                if 'x86_64-mingw32' in h:
                    urls[(tname, vver)] = s.get('url', '')
                    break
            # 回退 i686-mingw32 (Windows 32-bit)
            if (tname, vver) not in urls:
                for s in systems:
                    h = s.get('host', '').lower()
                    if 'i686-mingw32' in h:
                        urls[(tname, vver)] = s.get('url', '')
                        break

print('=== found ' + str(len(urls)) + ' of ' + str(len(deps)) + ' deps (Windows) ===')
for (n, v), u in urls.items():
    fname = u.split('/')[-1]
    src = 'GITHUB' if 'github.com' in u else 'CDN'
    print(n + '@' + v + ' [' + src + ']: ' + fname)

# 清理旧的不匹配的包（2.0.17 的）
dlDir = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\arduino-data\downloads\packages'
neededNames = set(u.split('/')[-1] for u in urls.values())
for fn in os.listdir(dlDir):
    if fn not in neededNames and not fn.startswith(('ctags','dfu-','mdns','serial')):
        os.remove(os.path.join(dlDir, fn))
        print('RM: ' + fn)

# 下载
os.makedirs(dlDir, exist_ok=True)
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

for (n, v), url in urls.items():
    fname = url.split('/')[-1]
    dest = os.path.join(dlDir, fname)
    if os.path.exists(dest) and os.path.getsize(dest) > 10000:
        print('SKIP: ' + fname)
        continue
    mirrors = ['https://github.xxlab.tech/', 'https://fastgit.cc/', 'https://tvv.tw/', 'https://ghproxy.net/']
    print('DL: ' + fname + ' ...')
    for attempt in range(8):
        try:
            mirror = mirrors[attempt % len(mirrors)]
            dlUrl = mirror + url if 'github.com' in url else url
            req = urllib.request.Request(dlUrl, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req, timeout=600, context=ctx) as resp:
                total = int(resp.headers.get('Content-Length', 0))
                downloaded = 0
                with open(dest, 'wb') as f:
                    while True:
                        chunk = resp.read(65536)
                        if not chunk:
                            break
                        f.write(chunk)
                        downloaded += len(chunk)
                        if total > 0:
                            sys.stdout.write('\r  ' + str(downloaded//1048576) + '/' + str(total//1041) + ' MB')
                            sys.stdout.flush()
                print('')
            print('  OK ' + str(downloaded // 1048576) + ' MB')
            break
        except Exception as e:
            print('  retry ' + str(attempt+1) + ': ' + str(e)[:60])
            import time; time.sleep(3)
    else:
        print('  GIVEUP: ' + fname)
print('\nDone')
