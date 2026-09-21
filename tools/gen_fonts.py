import subprocess, sys

node_script = r'C:\Users\longyaosi\AppData\Roaming\npm\node_modules\lv_font_conv\lv_font_conv.js'
ttf = r'C:\Windows\Fonts\simhei.ttf'
symbols = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\zh_symbols_v2.txt'
out16 = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\font_zh_16.c'
out24 = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\font_zh_24.c'

with open(symbols, 'r', encoding='utf-8') as f:
    sym_str = f.read().strip()

for size, outpath in [(16, out16), (24, out24)]:
    cmd = [
        'node', node_script,
        '--font', ttf,
        '--size', str(size),
        '--bpp', '4',
        '--format', 'lvgl',
        '--output', outpath,
        '--symbols', sym_str,
    ]
    print(f'Generating {size}px font ({len(sym_str)} chars)...')
    result = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8')
    if result.returncode != 0:
        print(f'ERROR: {result.stderr}')
        sys.exit(1)
    print(f'  OK -> {outpath}')

print('Done!')