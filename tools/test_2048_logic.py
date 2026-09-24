#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""2048 核心逻辑的离线单测（与 src/app/game2048_screen.cpp 里的 slideLine/moveBoard 同构）。

烧录前先跑它 —— 合并规则写错（典型：一步连并两次，4 4 4 4 变 16）在板子上
只会表现为"分数怪怪的"，很难反查。

    python tools/test_2048_logic.py
"""
N = 4


def slide(inl):
    out = [0] * N
    n = 0
    merged = False
    gain = 0
    for v in inl:
        if v == 0:
            continue
        if n > 0 and not merged and out[n - 1] == v:
            out[n - 1] *= 2
            gain += out[n - 1]
            merged = True
        else:
            out[n] = v
            n += 1
            merged = False
    return out, gain


def move(b, d):
    nb = [row[:] for row in b]
    g = 0
    moved = False
    for k in range(N):
        if d == 0:
            line = [nb[k][i] for i in range(N)]          # 左
        elif d == 1:
            line = [nb[k][N - 1 - i] for i in range(N)]  # 右
        elif d == 2:
            line = [nb[i][k] for i in range(N)]          # 上
        else:
            line = [nb[N - 1 - i][k] for i in range(N)]  # 下
        o, gain = slide(line)
        g += gain
        if o != line:
            moved = True
        for i in range(N):
            if d == 0:
                nb[k][i] = o[i]
            elif d == 1:
                nb[k][N - 1 - i] = o[i]
            elif d == 2:
                nb[i][k] = o[i]
            else:
                nb[N - 1 - i][k] = o[i]
    return nb, g, moved


CASES = [
    ([4, 4, 4, 4], [8, 8, 0, 0], 16),   # 关键：不能一步连并两次
    ([2, 2, 4, 4], [4, 8, 0, 0], 12),
    ([2, 0, 2, 0], [4, 0, 0, 0], 4),
    ([0, 0, 0, 2], [2, 0, 0, 0], 0),
    ([2, 2, 2, 0], [4, 2, 0, 0], 4),    # 靠边优先
    ([8, 0, 0, 8], [16, 0, 0, 0], 16),
    ([0, 0, 0, 0], [0, 0, 0, 0], 0),
    ([1024, 1024, 0, 0], [2048, 0, 0, 0], 2048),
]


def main():
    ok = True
    for inp, exp, expg in CASES:
        out, g = slide(inp)
        good = (out == exp and g == expg)
        ok = ok and good
        print("%s %-16s -> %-16s gain=%-5d (期望 %-16s gain=%d)"
              % ("OK  " if good else "FAIL", inp, out, g, exp, expg))

    # 四方向对称性：同一棋盘，左右镜像、上下镜像应各自成立
    b = [[2, 2, 4, 4], [0, 2, 0, 2], [8, 8, 8, 8], [0, 0, 0, 2]]
    l, gl, ml = move(b, 0)
    r, gr, mr = move(b, 1)
    u, gu, mu = move(b, 2)
    d, gd, md = move(b, 3)
    print("左 gain=%d  右 gain=%d  上 gain=%d  下 gain=%d" % (gl, gr, gu, gd))
    for name, nb in (("左", l), ("右", r), ("上", u), ("下", d)):
        for row in nb:
            print("   %s  %s" % (name, row))
    # 左右必须同分；上下必须同分（合并集合相同，只是贴的边不同）
    if gl != gr or gu != gd:
        print("FAIL: 左右 / 上下得分不对称")
        ok = False
    if not (ml and mr and mu and md):
        print("FAIL: 有方向判定为'没动'")
        ok = False

    print("ALL OK" if ok else "HAS FAILURES")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
