#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
修「下一页被连坐删掉」：
  <li class="b_pag">（分页容器）会从子树继承 href_resolved = "...&first=11&FORM=PERE"，
  原来无条件按 first= 删除 → 整个分页块连坐消失，「下一页」也没了。
	new 规则：first= 只在**文本是纯数字**（"2"/"3"）时才删，容器文本是
  "分页123下一页" 这种长的就不删。同时 FORM=PORE（下一页）永远保留。
"""
import io
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
P = os.path.join(ROOT, "src", "browser_engine", "src", "layout_engine.cpp")

OLD = '  if (strstr(h, "first=") != NULL) return true;        /* 数字页码「2」「3」 */'

NEW = r"""  if (strstr(h, "first=") != NULL) {
    /* 数字页码「2」「3」：只在**文本是纯数字**时才删。
       ⚠️ 不能无条件删：<li class="b_pag"> 这个分页容器会从子树继承
       href_resolved（dom_renderer.cpp 的 subtree_first_href 只对 li 做），
       它的 href 同样带 first=，无条件删会把整块分页连坐掉，「下一页」
       也就跟着没了（2026-09-23 实测踩到）。容器文本是「分页123下一页」
       这种长的，纯数字判断正好把它排除。 */
    const char *t = n->text_content;
    if (!t || !t[0]) return true;                 /* 无文本：纯壳子，删 */
    size_t len = strlen(t);
    if (len > 3) return false;                    /* 太长 = 容器，放过 */
    for (size_t i = 0; i < len; i++) {
      if (t[i] < '0' || t[i] > '9') return false;
    }
    return true;                                  /* 1~3 位纯数字 = 页码 */
  }"""


def main():
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        s = f.read()
    if OLD not in s:
        print("[FAIL] 没找到目标行")
        return 1
    s = s.replace(OLD, NEW, 1)
    with io.open(P, "w", encoding="utf-8", newline="") as f:
        f.write(s)
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        back = f.read()
    ok = NEW in back and "数字页码" in back
    print("[%s] 回读校验" % ("OK" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
