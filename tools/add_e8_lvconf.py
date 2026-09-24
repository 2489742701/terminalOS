# -*- coding: utf-8 -*-
"""在 docs/14 的 E 段末尾追加 E8：两份 lv_conf.h，改错那份不生效"""
import io, os

p = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\docs\14-坑点速查表.md'
raw = io.open(p, encoding='utf-8', errors='replace').read()
nl = '\r\n' if '\r\n' in raw else '\n'
L = raw.replace('\r\n', '\n').split('\n')

if any('### E8' in l for l in L):
    print('E8 already present')
    raise SystemExit(0)

# 插到 F 段标题（## F.）之前，即 E 段末尾
anchor = next(k for k, l in enumerate(L) if l.startswith('## F. 浏览器排版'))
block = [
    '### E8 · ⚠️ LVGL 配置有两份，改错那份不生效（2026-09-23 核实）',
    '编译时真正生效的是 **vendored 库里那份**：',
    '`../4.0inch_ESP32-4848S040/1-Demo/Demo_Arduino/Libraries/Lvgl/lv_conf.h`',
    '（26972 B，`LV_MEM_POOL_IN_PSRAM 1`、`LV_MEM_SIZE (128U*1024U)`）',
    '',
    '`src/config/lv_conf.h`（25690 B，`LV_MEM_SIZE (192U*1024U)`，**没有** `LV_MEM_POOL_IN_PSRAM` 宏）',
    '**不会被编译进去** —— 改它等于白改，而且不会报任何错。',
    '',
    '- **动作**：改 LVGL 行为（内存池、日志、widget 开关）一律改 `Libraries/Lvgl/lv_conf.h`。',
    '- **判据**：改完编译，看设备开机时 LVGL 的 mem 打印是否跟着变。',
    '- 同类提醒：改 lexbor 同理，改 `src/browser_engine/lexbor/`。',
    '',
]
L[anchor:anchor] = block
io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(L))
print('inserted E8, size=%d' % os.path.getsize(p))
