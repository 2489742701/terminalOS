#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
修：必应的分页块 <li class="b_pag"> 只有一个 <nav> 子元素，够不到
li_has_block_children() 的「>=2 个块级子元素」门槛 → 整块文本被抽成一行
「- 分页123下一页」，点起来也不知道点的是什么。

改法：nav / ul / ol / table 这种结构性容器，出现一个就判定为「有块级子元素」
（它们的语义就是"里面还有一层结构"，不适合压成一行文本）。
按行处理，CRLF 安全。
"""
import io
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
P = os.path.join(ROOT, "src", "browser_engine", "src", "dom_renderer.cpp")

ANCHOR = "    if (skip)\n      continue;\n    n++;\n    if (n >= 2)\n      return true;"
NEW = ("    if (skip)\n"
       "      continue;\n"
       "    /* nav/ul/ol/table 是结构性容器：出现一个就说明「里面还有一层」，\n"
       "       不能把整块文本压成一行。必应的分页 <li class=b_pag> 就只有\n"
       "       一个 <nav> 子元素，靠 n>=2 永远判不出来（2026-09-23 实测）。 */\n"
       "    static const char *kContainers[] = {\"nav\", \"ul\", \"ol\", \"table\", NULL};\n"
       "    for (int k = 0; kContainers[k]; k++) {\n"
       "      size_t pl = strlen(kContainers[k]);\n"
       "      if (pl != tl)\n"
       "        continue;\n"
       "      bool eq = true;\n"
       "      for (size_t i = 0; i < tl; i++) {\n"
       "        if (tolower((unsigned char)tag[i]) != kContainers[k][i]) {\n"
       "          eq = false;\n"
       "          break;\n"
       "        }\n"
       "      }\n"
       "      if (eq)\n"
       "        return true;\n"
       "    }\n"
       "    n++;\n"
       "    if (n >= 2)\n"
       "      return true;")


def main():
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        raw = f.read()
    if ANCHOR not in raw:
        print("[FAIL] 锚点未找到")
        return 1
    s = raw.replace(ANCHOR, NEW, 1)
    with io.open(P, "w", encoding="utf-8", newline="") as f:
        f.write(s)
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        back = f.read()
    ok = "kContainers" in back and "n++;" in back
    print("[%s] 回读校验" % ("OK" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
