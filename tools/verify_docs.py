# -*- coding: utf-8 -*-
"""最终全量校验：链接有效性、代号一致性、旧名残留、结构总览"""
import io, os, re

root = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal'
docs = os.path.join(root, 'docs')

files = []
for f in sorted(os.listdir(docs)):
    p = os.path.join(docs, f)
    if os.path.isfile(p) and f.endswith('.md'):
        files.append(p)
for f in sorted(os.listdir(os.path.join(docs, 'archive'))):
    p = os.path.join(docs, 'archive', f)
    if f.endswith('.md'):
        files.append(p)
for f in ['README.md', 'AGENT.md']:
    p = os.path.join(root, f)
    if os.path.exists(p):
        files.append(p)

LINK = re.compile(r'\[([^\]]*?)\]\(([^)\s]+\.md)\)')

print('=== 1. 链接有效性 ===')
bad = 0
for p in files:
    base = os.path.dirname(p)
    raw = io.open(p, encoding='utf-8', errors='replace').read().replace('\r\n', '\n')
    for m in LINK.finditer(raw):
        tgt = m.group(2)
        if tgt.startswith('http'):
            continue
        cand = os.path.normpath(os.path.join(root, tgt)) if tgt.startswith('docs/') \
            else os.path.normpath(os.path.join(base, tgt))
        if not os.path.exists(cand):
            print('  ✗ %-30s -> %s' % (os.path.relpath(p, root), tgt))
            bad += 1
print('  坏链接：%d' % bad)

print()
print('=== 2. 代号一致性 ===')
bad2 = 0
for p in files:
    raw = io.open(p, encoding='utf-8', errors='replace').read().replace('\r\n', '\n')
    for m in LINK.finditer(raw):
        text, tgt = m.group(1), m.group(2)
        mm = re.search(r'(?:docs/)?(?:archive/)?(\d{2})-', tgt)
        if not mm:
            continue
        for c in re.findall(r'`(\d{2})`', text):
            if c != mm.group(1):
                print('  ✗ %s: [%s](%s)' % (os.path.relpath(p, root), text[:40], tgt))
                bad2 += 1
print('  不一致：%d' % bad2)

print()
print('=== 3. 旧文件名残留 ===')
OLD = ['14-坑点速查表', '15-项目长期记忆', '13-搜索优先', '12-浏览器排版路线',
       '11-屏销毁', '10-浏览器渲染裁剪', '09-中文字体管理与生成', '08-浏览器UI布局',
       '07-启动循环类问题', '06-浏览器内存管理', '05-浏览器异步架构', '03-参考项目分析']
bad3 = 0
for p in files:
    raw = io.open(p, encoding='utf-8', errors='replace').read().replace('\r\n', '\n')
    for i, l in enumerate(raw.split('\n'), 1):
        for o in OLD:
            if o + '.md' in l and 'archive/' not in l:
                print('  ! %s:%d %s' % (os.path.relpath(p, root), i, l.strip()[:96]))
                bad3 += 1
                break
print('  残留：%d' % bad3)

print()
print('=== 4. 最终结构 ===')
tot = 0
for f in sorted(os.listdir(docs)):
    p = os.path.join(docs, f)
    if os.path.isfile(p) and f.endswith('.md'):
        s = io.open(p, encoding='utf-8', errors='replace').read()
        h1 = next((l for l in s.replace('\r\n', '\n').split('\n') if l.startswith('# ')), '(无)')
        tot += os.path.getsize(p)
        print('  %6d B  %5d 行  %-32s %s' % (os.path.getsize(p), len(s.split('\n')), f, h1[:52]))
for f in sorted(os.listdir(os.path.join(docs, 'archive'))):
    p = os.path.join(docs, 'archive', f)
    print('  %6d B  %5d 行  archive/%-24s (已归档)' % (
        os.path.getsize(p), len(io.open(p, encoding='utf-8', errors='replace').read().split('\n')), f))
print('  主文档合计 %d B' % tot)
print()
print('校验结果：坏链接=%d  代号不一致=%d  旧名残留=%d' % (bad, bad2, bad3))
print('结论：%s' % ('全部通过 ✅' if bad == 0 and bad2 == 0 and bad3 == 0 else '仍有问题 ❌'))
