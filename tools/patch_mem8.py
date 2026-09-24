# -*- coding: utf-8 -*-
"""修正 .workbuddy/memory/MEMORY.md：
   1) 底栏三点键的语义（曾被 master 纠正，旧记录是错的）
   2) 文档指针补 14/15
"""
import io, os

p = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\MEMORY.md'
raw = io.open(p, encoding='utf-8', errors='replace').read()
nl = '\r\n' if '\r\n' in raw else '\n'
L = raw.replace('\r\n', '\n').split('\n')

# --- 1) 修正指针 ---
for k, l in enumerate(L):
    if l.startswith('> **细节落盘 `geek-terminal/docs/`**'):
        L[k] = ('> **细节落盘 `geek-terminal/docs/`**：00 上手指南 · 01 花屏 · 02 板级硬事实/构建烧录 ·\n'
                '> 06 内存与生命周期 · 07 bootloop 方法论 · 08 整页缩放 · 09 字体 · 10 渲染裁剪 ·\n'
                '> 11 悬空指针 · 12 平铺排版 · 13 搜索小引擎 · **14 坑点速查表（按症状反查）** ·\n'
                '> **15 项目长期记忆（本文件的项目内副本）**。**项目里另有 `AGENT.md` = 协作契约。**\n'
                '> 本文件只存**结论+指针**，同类知识优先更新 docs。')
        print('pointer updated at line', k + 1)
        break

# --- 2) 修正底栏语义 ---
i = next((k for k, l in enumerate(L) if '底栏布局' in l), None)
if i is None:
    print('WARN: 底栏 section not found')
else:
    # 找到该 section 的结尾（下一个 '## ' 或 EOF）
    j = i + 1
    while j < len(L) and not L[j].startswith('## '):
        j += 1
    new = [
        '## 浏览器底栏布局（2026-09-23 定稿，含被纠正的语义）',
        '- 两组**互斥**工具栏，靠三点（⋯）切换：',
        '  - 组 A：URL 搜索框 + 刷新 + **退出浏览器**',
        '  - 组 B：后退 / 前进 / 首页 / 刷新 / **下载**',
        '  y=410：URL 栏整行 476×30；y=444：后退(0) 前进(80) 首页(160) 刷新(240) 下载(320) ⋯(404)。',
        '- ⚠️ **三点（⋯）= "更多"**，展开的是**底部搜索框 + 刷新 + 退出浏览器**（就是上面那组互斥切换）。',
        '- ⚠️ **首页图标 = 去我们自己的搜索首页**（`showSearchHome()`），**不是**退出浏览器。',
        '- **退出浏览器**走三点展开后的「退出」，或左滑手势。',
        '- 提示条 `toast()` **不带定时器**（定时器不属于对象树，容易漏删），靠下次导航/回首页隐藏。',
        '',
        '> ⛔ **踩过的错**：曾把三点键改成"回到搜索首页"，被 master 纠正。',
        '> 三点 = 更多（搜索框+刷新+退出）；首页图标才管去搜索首页。改 UI 语义前先确认现有行为。',
    ]
    L[i:j] = new
    print('底栏 section replaced (%d lines -> %d)' % (j - i, len(new)))

io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(L))
print('written size=%d' % os.path.getsize(p))
