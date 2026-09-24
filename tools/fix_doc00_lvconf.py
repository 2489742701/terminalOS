# -*- coding: utf-8 -*-
"""修正 docs/00 里 lv_conf.h 的路径（src/config 那份不生效）"""
import io, os

p = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\docs\00-项目上手指南.md'
raw = io.open(p, encoding='utf-8', errors='replace').read()
nl = '\r\n' if '\r\n' in raw else '\n'
L = raw.replace('\r\n', '\n').split('\n')

i = next(k for k, l in enumerate(L) if 'lv_conf.h' in l)
L[i] = '│  │   └─ lv_conf.h      # ⚠️ 陷阱：这份**不生效**！真正生效的在 vendored 库里，见 14-E8'
print('line %d ->' % (i + 1), L[i])

anchor = next(k for k, l in enumerate(L) if l.startswith('### 浏览器数据流'))
note = [
    '> ⚠️ **LVGL 配置有两份，改错那份不生效**（2026-09-23 核实）：',
    '> 编译时真正生效的是 `../4.0inch_ESP32-4848S040/1-Demo/Demo_Arduino/Libraries/Lvgl/lv_conf.h`',
    '> （`LV_MEM_POOL_IN_PSRAM 1`、`LV_MEM_SIZE (128U*1024U)`，26972 B）。',
    '> `src/config/lv_conf.h`（25690 B、`LV_MEM_SIZE (192U*1024U)`、无 `LV_MEM_POOL_IN_PSRAM` 宏）',
    '> **不会被用到**。改 LVGL 行为请先确认改的是前者。详见 [`14` E8](14-坑点速查表.md)。',
    '',
]
L[anchor:anchor] = note

io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(L))
print('written size=%d' % os.path.getsize(p))
