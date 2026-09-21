import os

with open(r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\zh_symbols_v2.txt', 'r', encoding='utf-8') as f:
    font_chars = list(f.read().strip())

seen = set(font_chars)
ordered = list(font_chars)

src_dir = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src'
for root, dirs, files in os.walk(src_dir):
    for fn in files:
        if fn.endswith(('.cpp', '.h', '.c')):
            path = os.path.join(root, fn)
            with open(path, 'r', encoding='utf-8', errors='replace') as f:
                content = f.read()
            for ch in content:
                if '\u4e00' <= ch <= '\u9fff' or '\u3000' <= ch <= '\u303f' or '\uff00' <= ch <= '\uffef':
                    if ch not in seen:
                        seen.add(ch)
                        ordered.append(ch)

result = ''.join(ordered)
outpath = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\zh_symbols_v2.txt'
with open(outpath, 'w', encoding='utf-8') as f:
    f.write(result)
print(f'Total {len(result)} chars written to {outpath}')