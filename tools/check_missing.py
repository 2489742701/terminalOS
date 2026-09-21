import os

with open(r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\zh_symbols_v2.txt', 'r', encoding='utf-8') as f:
    font_chars = set(f.read().strip())

used_chars = set()
src_dir = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src'
for root, dirs, files in os.walk(src_dir):
    for fn in files:
        if fn.endswith(('.cpp', '.h', '.c')):
            path = os.path.join(root, fn)
            with open(path, 'r', encoding='utf-8', errors='replace') as f:
                content = f.read()
            for ch in content:
                if '\u4e00' <= ch <= '\u9fff' or '\u3000' <= ch <= '\u303f':
                    used_chars.add(ch)

missing = used_chars - font_chars
print(f'Font has {len(font_chars)} chars')
print(f'Code uses {len(used_chars)} Chinese chars')
print(f'Missing {len(missing)} chars: {"|".join(sorted(missing))}')