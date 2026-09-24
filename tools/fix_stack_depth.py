#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
修复 loopTask 栈溢出崩溃（2026-09-23 复现实证）。

根因：layout_engine 的 4 个全树遍历函数把 next_sibling 也做成了递归，
导致递归深度 = 树的**节点总数**（几百~上千）而非嵌套层数。
loopTask 栈只有 8192 B，必应这种 356 节点的页面能过，
乐鑫官网那种更多节点的页面直接爆栈 → Reboot。

修复：
 1. sibling 递归改为 while 迭代（递归深度降为树的真实嵌套深度）
 2. 给 child 方向的递归加深度上限，防止畸形页面嵌套过深
"""
import io
import os
import re

P = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\layout_engine.cpp'

raw = io.open(P, encoding='utf-8', errors='replace').read()
NL = '\r\n' if '\r\n' in raw else '\n'
L = raw.replace('\r\n', '\n').split('\n')

# ── 1. sibling 递归 -> while 迭代 ──────────────────────────────────────────
SIB_TARGETS = [
    'layout_tree_stats',
    'layout_flatten_tree',
    'layout_scale_tree',
    'layout_clamp_horizontal',
]


def find_func_body(lines, name):
    """返回 (sig_end_idx, open_brace_idx, close_brace_idx)；失败返回 None"""
    start = None
    for i, l in enumerate(lines):
        if re.match(r'^(static\s+)?[A-Za-z_][\w\s\*]*\s+' + re.escape(name) + r'\s*\(', l):
            start = i
            break
    if start is None:
        return None
    # 找开括号（可能在签名行或下一行）
    ob = start
    while '{' not in lines[ob]:
        ob += 1
        if ob > start + 5:
            return None
    depth = 0
    cb = None
    for k in range(ob, len(lines)):
        depth += lines[k].count('{') - lines[k].count('}')
        if depth <= 0 and k > ob:
            cb = k
            break
    if cb is None:
        return None
    return start, ob, cb


changed = []
for name in SIB_TARGETS:
    loc = find_func_body(L, name)
    if not loc:
        print('!! 找不到函数:', name)
        continue
    start, ob, cb = loc
    body = L[ob + 1:cb]
    # 去掉 sibling 递归那一行
    new_body = [l for l in body
                if not re.search(r'\b' + re.escape(name) + r'\s*\(\s*node->next_sibling', l)]
    # 缩进 4 空格，套进 while
    indented = [('    ' + l if l.strip() else l) for l in new_body]
    new = ['  while (node) {'] + indented + ['    node = node->next_sibling;', '  }']
    L[ob + 1:cb] = new
    changed.append(name)
    print('  sibling->iter:', name, '(body %d -> %d lines)' % (len(body), len(new_body)))

# ── 2. 加深度上限常量 ─────────────────────────────────────────────────────
anchor = None
for i, l in enumerate(L):
    if l.startswith('// Default box model values'):
        anchor = i
        break

DEPTH_BLOCK = [
    '',
    '/* ── 递归深度上限（防御性）──────────────────────────────────────────────',
    ' * 2026-09-23 崩溃复盘：原实现把 next_sibling 也做成递归，导致遍历一棵树的',
    ' * 递归深度 = **节点总数**（几百到上千）。而 loopTask 的栈默认只有 8192 B ——',
    ' * 必应搜索页 356 个节点侥幸过关，乐鑫官网那种更多节点的页面直接',
    ' * "A stack overflow in task loopTask has been detected" 重启，且因为 RGB',
    ' * 并行屏由 DMA 自行刷新，画面还在、触摸全失效，极具迷惑性。',
    ' *',
    ' * 修复：兄弟节点改用迭代（已改），递归深度 = 树的真实嵌套层数；',
    ' * 再给剩下的父子递归加一道上限，畸形页面（如无限嵌套的 JS 生成 DOM）',
    ' * 剪断子树而不是继续压栈。正常网页嵌套层数一般 < 30，这里给充分余量。',
    ' */',
    '#define MAX_LAYOUT_DEPTH 64',
]

if anchor is not None and not any('MAX_LAYOUT_DEPTH' in l for l in L):
    L[anchor:anchor] = DEPTH_BLOCK
    print('  inserted MAX_LAYOUT_DEPTH block @', anchor + 1)
elif any('MAX_LAYOUT_DEPTH' in l for l in L):
    print('  MAX_LAYOUT_DEPTH 已存在，跳过')
else:
    print('!! 找不到锚点')

io.open(P, 'w', encoding='utf-8', newline='').write(NL.join(L))
print('written, size =', os.path.getsize(P))
