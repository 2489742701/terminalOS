# -*- coding: utf-8 -*-
"""把所有屏的 swipe_detect(...) 换成统一的 swipe_back_to(e, st, target)。
settings.cpp 单独处理（它要返回上一级，用 swipe_back_act）。"""
import io, re, glob

pat = re.compile(r'swipe_detect\((\w+),\s*(\w+),\s*([^;]+?)\);')

def fix(m):
    ev, st, rest = m.group(1), m.group(2), m.group(3)
    tgt = rest.split(',')[0].strip()          # 只保留 target，丢掉 ", SWIPE_H" / ", false, 40"
    return 'swipe_back_to(%s, %s, %s);' % (ev, st, tgt)

for f in sorted(glob.glob('src/app/*.cpp')):
    if f.endswith('settings.cpp'):
        continue
    t = io.open(f, encoding='utf-8', newline='').read()
    if 'swipe_detect' not in t:
        continue
    n = len(pat.findall(t))
    t2 = pat.sub(fix, t)
    if t2 != t:
        io.open(f, 'w', encoding='utf-8', newline='').write(t2)
        print('%-30s %d call(s) -> swipe_back_to' % (f.split('/')[-1], n))
        for m in pat.finditer(io.open(f, encoding='utf-8').read()):
            pass
        for l in io.open(f, encoding='utf-8').read().split('\n'):
            if 'swipe_back_to' in l:
                print('      ', l.strip()[:90])
