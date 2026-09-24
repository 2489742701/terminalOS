# -*- coding: utf-8 -*-
"""把 15 号文档加进 docs/00 的文档地图 和 README 的文档表"""
import io, os

base = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal'

row00 = '| [`15`](15-项目长期记忆.md) | 项目内长期记忆副本：结论+指针、产品语义约定、待办清单 |'
row_rd = ('| [`docs/15-项目长期记忆.md`](docs/15-项目长期记忆.md) | 项目内长期记忆：'
          '**结论+指针**、产品语义约定（三点键/首页键，改错过）、待办清单 |')


def insert_after(path, needle, row):
    raw = io.open(path, encoding='utf-8', errors='replace').read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    L = raw.replace('\r\n', '\n').split('\n')
    if any(row.strip()[:36] in l for l in L):
        print('SKIP (already):', os.path.basename(path))
        return
    i = next(k for k, l in enumerate(L) if needle in l)
    L[i + 1:i + 1] = [row]
    io.open(path, 'w', encoding='utf-8', newline='').write(nl.join(L))
    print('OK  %-12s inserted after line %d' % (os.path.basename(path), i + 1))


insert_after(os.path.join(base, 'docs', '00-项目上手指南.md'), '14-坑点速查表.md', row00)
insert_after(os.path.join(base, 'README.md'), 'docs/14-坑点速查表.md', row_rd)
