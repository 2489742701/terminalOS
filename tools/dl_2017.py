import json, os, subprocess

idxPath = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\arduino-data\package_index.json'
with open(idxPath, 'r', encoding='utf-8') as f:
    idx = json.load(f)

deps = []
for pkg in idx.get('packages', []):
    if pkg['name'] != 'esp32':
        continue
    for p in pkg.get('platforms', []):
        if p.get('version') == '2.0.17':
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
            for s in systems:
                h = s.get('host', '').lower()
                if 'x86_64-mingw32' in h:
                    urls[(tname, vver)] = s.get('url', '')
                    break
            if (tname, vver) not in urls:
                for s in systems:
                    h = s.get('host', '').lower()
                    if 'i686-mingw32' in h:
                        urls[(tname, vver)] = s.get('url', '')
                        break

dlDir = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\arduino-data\downloads\packages'
mirror = 'https://github.xxlab.tech/'

for (n, v), url in urls.items():
    fname = url.split('/')[-1]
    dest = os.path.join(dlDir, fname)
    if os.path.exists(dest) and os.path.getsize(dest) > 10000:
        print('SKIP: ' + fname + ' (' + str(os.path.getsize(dest)//1048576) + ' MB)')
        continue
    print('DL: ' + fname)
    dlUrl = mirror + url if 'github.com' in url else url
    r = subprocess.run(['curl.exe', '-L', '-o', dest, dlUrl, '--connect-timeout', '30', '--max-time', '300', '-s', '-w', '%{http_code} %{size_download}'], capture_output=True, text=True)
    code_size = r.stdout.strip().split()
    if code_size and code_size[0] == '200':
        print('  OK: ' + str(int(code_size[1])//1048576) + ' MB')
    else:
        print('  FAIL: ' + (r.stdout.strip() or r.stderr.strip()[:60]))

print('\nDone')