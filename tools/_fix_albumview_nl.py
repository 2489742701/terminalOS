# -*- coding: utf-8 -*-
"""修 albumview 日志里被吃掉的转义：那里应该是字面的 \\n，不是真换行。"""
import io
B = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp'
s = io.open(B, encoding='utf-8').read()

bad = 'Serial.printf("[Album] usage: albumview 1..%d' + '\n' + '", g_albumN);'
good = 'Serial.printf("[Album] usage: albumview 1..%d' + '\\' + 'n", g_albumN);'
assert s.count(bad) == 1, s.count(bad)
s = s.replace(bad, good, 1)
io.open(B, 'w', encoding='utf-8', newline='').write(s)
print('fixed')
