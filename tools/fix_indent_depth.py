#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
清理上一轮脚本改写留下的缩进/guard 问题，并给 UI 任务路径上的两个递归
（layout_drop_junk / layout_render_node）加深度上限。

layout_node_destroy 不能加深 —— 中途截断会内存泄漏。
layout_calculate_dimensions / layout_position_node 只在 Phase 1 后台任务
（16KB 栈，且同样只用 child 递归）里跑，不在本次崩溃路径上，暂不动。
"""
import io
import os
import re

P = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\layout_engine.cpp'

raw = io.open(P, encoding='utf-8', errors='replace').read()
NL = '\r\n' if '\r\n' in raw else '\n'
L = raw.replace('\r\n', '\n').split('\n')

# ── 步骤 1：清理 while 内的 guard + 修正缩进 ───────────────────────────────
# guard 形如：
#       if (!node)          /  if (!node || maxW <= 0)
#         return;
GUARD_KW = ('if (!node)',)


def find_guard(body):
    """返回 guard 占的行数，0 = 没有"""
    for n in (2, 1):
        if len(body) < n:
            continue
        head = ' '.join(body[:n]).strip()
        if head.startswith('if (!node)') and 'return;' in head:
            return n, 'if (!node) return;'
    return 0, None


targets = ['layout_tree_stats', 'layout_flatten_tree', 'layout_scale_tree',
           'layout_clamp_horizontal']

for name in targets:
    st = next(k for k, l in enumerate(L) if l.startswith('static void %s(' % name))
    cb = st
    d = 0
    for k in range(st, len(L)):
        d += L[k].count('{') - L[k].count('}')
        if d <= 0 and k > st:
            cb = k
            break
    # while 行一定是 st+1
    wi = st + 1
    assert L[wi].strip() == 'while (node) {', (name, L[wi])
    # 找 sibling 推进行
    si = next(k for k in range(wi + 1, cb)
              if L[k].strip() == 'node = node->next_sibling;')
    body = L[wi + 1:si]
    n, guard = find_guard(body)
    if n:
        del body[:n]
    # body 缩进 -2（原本多了 2 空格）
    fixed = []
    for l in body:
        if l.startswith('    ') and l.strip():
            fixed.append(l[2:])
        else:
            fixed.append(l)
    # guard 提到 while 之前
    prefix = ['  ' + guard] if n else []
    L[wi:si + 1] = prefix + ['  while (node) {'] + fixed + \
                   ['    node = node->next_sibling;']
    print('  cleaned %-26s guard=%s  body=%d lines' % (name, bool(n), len(fixed)))

# ── 步骤 2：给 layout_drop_junk 加深度上限 ─────────────────────────────────
changed2 = []
for i, l in enumerate(L):
    if l.startswith('static int layout_drop_junk(LayoutNode *node) {'):
        L.insert(i + 1, '  if (depth > MAX_LAYOUT_DEPTH) return 0;')
        L[i] = 'static int layout_drop_junk(LayoutNode *node, int depth) {'
        changed2.append('drop_junk sig')
        break

for i, l in enumerate(L):
    if re.search(r'n \+= layout_drop_junk\(c\);', l):
        L[i] = l.replace('layout_drop_junk(c);', 'layout_drop_junk(c, depth + 1);')
        changed2.append('drop_junk recur')
        break

for i, l in enumerate(L):
    if re.search(r'int dropped = layout_drop_junk\(root\);', l):
        L[i] = l.replace('layout_drop_junk(root);', 'layout_drop_junk(root, 0);')
        changed2.append('drop_junk entry')
        break

# ── 步骤 3：给 layout_render_node 加深度上限 ───────────────────────────────
for i, l in enumerate(L):
    if l.startswith('static void layout_render_node(LayoutNode *node, RenderContext *render_ctx,'):
        L[i + 1] = '                               void *parent_widget, int depth) {'
        L.insert(i + 2, '  if (depth > MAX_LAYOUT_DEPTH) return;')
        changed2.append('render_node sig')
        break

for i, l in enumerate(L):
    if re.search(r'layout_render_node\(child, render_ctx, next_parent\);', l):
        L[i] = l.replace('layout_render_node(child, render_ctx, next_parent);',
                         'layout_render_node(child, render_ctx, next_parent,\n'
                         '                     depth + 1);')
        changed2.append('render_node recur')
        break

for i, l in enumerate(L):
    if re.search(r'layout_render_node\(root, render_ctx, render_ctx->root_container\);', l):
        L[i] = l.replace(
            'layout_render_node(root, render_ctx, render_ctx->root_container);',
            'layout_render_node(root, render_ctx, render_ctx->root_container, 0);')
        changed2.append('render_node entry')
        break

print('  step2/3:', changed2)

io.open(P, 'w', encoding='utf-8', newline='').write(NL.join(L))
print('written, size =', os.path.getsize(P))
