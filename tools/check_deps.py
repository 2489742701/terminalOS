import json
idxPath = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\arduino-data\package_index.json'
with open(idxPath, 'r', encoding='utf-8') as f:
    idx = json.load(f)

for pkg in idx.get('packages', []):
    if pkg['name'] != 'esp32':
        continue
    for p in pkg.get('platforms', []):
        if p.get('version') == '2.0.17':
            print('=== esp32:esp32@2.0.17 dependencies ===')
            for d in p.get('toolsDependencies', []):
                print(d['name'] + '@' + d['version'])