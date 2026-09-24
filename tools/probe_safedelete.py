#!/usr/bin/env python3
"""验证：抬高批量删除阈值后，python 子进程能否正常删除构建中间文件。"""
import os, tempfile, sys

d = tempfile.mkdtemp(prefix="sdtest_")
paths = [os.path.join(d, "t%03d.tmp" % i) for i in range(60)]
for p in paths:
    open(p, "w").write("x")
n = 0
for p in paths:
    try:
        os.remove(p)
        n += 1
    except Exception as e:
        print("blocked at", n, type(e).__name__, e)
        break
print("deleted", n, "of", len(paths))
