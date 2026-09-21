import json
idxPath = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\arduino-data\package_index.json'
with open(idxPath, 'r', encoding='utf-8') as f:
    idx = json.load(f)

deps = []
for pkg in idx.get('packages', []):
    if pkg['name'] != 'esp32':
        continue
    for p in pkg.get('platforms', []):
        if p.get('version') == '3.3.11':
            for d in p.get('toolsDependencies', []):
                deps.append((d['name'], d['version']))
depSet = set(deps)

for pkg in idx.get('packages', []):
    for t in pkg.get('tools', []):
        tname = t.get('name')
        vers = [t] if 'version' in t and 'systems' in t else t.get('versions', [])
        for ver in vers:
            vver = ver.get('version')
            if (tname, vver) not in depSet:
                continue
            for s in ver.get('systems', []):
                h = s.get('host', '')
                if 'mingw' in h.lower() or 'win' in h.lower():
                    print(tname + '@' + vver + ': ' + h + ' -> ' + s.get('url', '').split('/')[-1])