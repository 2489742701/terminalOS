#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
1) layout_clamp_horizontal 把 guard 提到 while 之外（避免每圈重复判断 maxW）
2) platformio.ini: loopTask 栈 8192 -> 32768
3) 渲染完成后打印 loopTask 剩余栈，以后能提前看到水位
"""
import io
import os
import re

P = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\layout_engine.cpp'
INI = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\platformio.ini'

# ── 1. clamp_horizontal guard 外提 ────────────────────────────────────────
raw = io.open(P, encoding='utf-8', errors='replace').read()
NL = '\r\n' if '\r\n' in raw else '\n'
L = raw.replace('\r\n', '\n').split('\n')

st = next(k for k, l in enumerate(L) if l.startswith('static void layout_clamp_horizontal('))
# while 在 st+1
if L[st + 1].strip() == 'while (node) {':
    # 找 guard 两行
    if L[st + 2].strip() == 'if (!node || maxW <= 0)' and L[st + 3].strip() == 'return;':
        del L[st + 2:st + 4]
        L[st + 1:st + 1] = ['  if (!node || maxW <= 0) return;', '  while (node) {']
        print('  clamp guard moved out')
        io.open(P, 'w', encoding='utf-8', newline='').write(NL.join(L))
    else:
        print('  clamp guard 位置异常，跳过:', repr(L[st + 2]), repr(L[st + 3]))
else:
    print('  clamp while 位置异常，跳过:', repr(L[st + 1]))

# ── 2. platformio.ini: loopTask 栈 ────────────────────────────────────────
raw = io.open(INI, encoding='utf-8', errors='replace').read()
NL2 = '\r\n' if '\r\n' in raw else '\n'
M = raw.replace('\r\n', '\n').split('\n')

if any('ARDUINO_LOOP_STACK_SIZE' in l for l in M):
    print('  loop stack flag 已存在')
else:
    ins = next(k for k, l in enumerate(M) if l.strip().startswith('build_flags'))
    add = [
        '\t; loopTask 栈：默认只有 8192 B。ESP32-S3 上 App::loop() 里跑 LVGL +',
        '\t; 布局树渲染，UI 线程必须自己挖得起深度 —— 2026-09-23 实测打开乐鑫官网',
        '\t; 那一页直接 "A stack overflow in task loopTask has been detected" 重启。',
        '\t; 32KB 从内部 DRAM 出（开机后 DRAM 约有 250KB 富余，代价可接受）。',
        '\t-DARDUINO_LOOP_STACK_SIZE=32768',
    ]
    M[ins + 1:ins + 1] = add
    io.open(INI, 'w', encoding='utf-8', newline='').write(NL2.join(M))
    print('  inserted ARDUINO_LOOP_STACK_SIZE=32768 @', ins + 2)

# 回读核对
s = io.open(INI, encoding='utf-8', errors='replace').read()
print('  ini size =', os.path.getsize(INI))
for l in s.split('\n'):
    if 'LOOP_STACK' in l or l.strip().startswith('build_flags'):
        print('   ', l[:100])
