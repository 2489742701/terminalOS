#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
改两处：
  1) lvgl_renderer.cpp：bing.com 的 UA 从 Android13 换成桌面 Chrome 120。
     实测（2026-09-23，cn.bing.com）：
       移动 UA(KitKat/Android13/iPhone) → 只给 5 条 li.b_algo，HTML 里 0 个分页标记
       桌面 UA                          → 9~10 条 + 「下一页」/「2」「3」分页链接
  2) layout_engine.cpp：把 flat_is_time_filter_link 升级成 flat_is_serp_chrome_link，
     一次性过滤必应 SERP 的全部页面壳子。

CRLF 安全：整文件读写，替换后立刻回读校验。
"""
import io
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
RENDER = os.path.join(ROOT, "src", "browser_engine", "src", "lvgl_renderer.cpp")
LAYOUT = os.path.join(ROOT, "src", "browser_engine", "src", "layout_engine.cpp")

OLD_UA = ('ua = "Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 '
          '(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36";')
NEW_UA = ('ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 '
          '(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";')

OLD_FUNC = """static bool flat_is_time_filter_link(LayoutNode *n) {
  if (!n) return false;
  const char *h = n->href_resolved ? n->href_resolved
                                   : (n->href ? n->href : n->href_path);
  if (!h) return false;
  return (strstr(h, "filters=ex1") != NULL);
}"""

NEW_FUNC = r"""static bool flat_is_serp_chrome_link(LayoutNode *n) {
  if (!n) return false;
  const char *h = n->href_resolved ? n->href_resolved
                                   : (n->href ? n->href : n->href_path);
  if (!h) return false;
  /* 「下一页」是必应唯一的真翻页入口，必须留（FORM=PORE）。
     ⚠️ 实测提醒：必应对我们这种无 JS、无历史 cookie 的客户端只给一份固定
     ~10 条的语料，即使带 FPIG token + first=11 结果也 90% 重叠。留着它是
     因为这是官方入口，删了用户就彻底没得翻；但别指望它能翻出新东西。 */
  if (strstr(h, "FORM=PORE") != NULL) return false;
  if (strstr(h, "filters=ex1") != NULL) return true;   /* 24小时/一周/一个月/去年 */
  if (strstr(h, "qpvt=") != NULL) return true;         /* 「全部」「时间不限」「网页」 */
  if (strstr(h, "FORM=HDRSC") != NULL) return true;    /* 顶部导航：图片/视频/学术/词典/航班 */
  if (strstr(h, "first=") != NULL) return true;        /* 数字页码「2」「3」 */
  static const char *kSerpVerticals[] = {
      "/images/search", "/videos/search", "/academic/search",
      "/dict/search",   "/travel/search", "/maps/", NULL};
  for (int i = 0; kSerpVerticals[i]; i++) {
    if (strstr(h, kSerpVerticals[i]) != NULL) return true;
  }
  return false;
}"""

OLD_CALL = "  if (flat_is_time_filter_link(node)) {"
NEW_CALL = "  if (flat_is_serp_chrome_link(node)) {"

OLD_PHRASES = '''    "下拉刷新",   "上拉刷新",   "下拉加载",   "上拉加载",   "加载更多",
    NULL,
};'''
NEW_PHRASES = '''    "下拉刷新",   "上拉刷新",   "下拉加载",   "上拉加载",   "加载更多",
    /* 必应 SERP 的壳子文本（有些是 span 不是链接，只能按文本杀） */
    "切换到国际版", "时间不限", "搜索工具",
    NULL,
};'''


def read(p):
    with io.open(p, "r", encoding="utf-8", newline="") as f:
        return f.read()


def write(p, s):
    with io.open(p, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def step(name, path, pairs):
    s = read(path)
    ok = True
    for old, new in pairs:
        if old not in s:
            print("  [FAIL] %s: 没找到片段 -> %s" % (name, old[:60].replace("\n", "\\n")))
            ok = False
            continue
        s = s.replace(old, new, 1)
        print("  [OK]   %s: 替换 %d 处" % (name, 1))
    if not ok:
        return False
    write(path, s)
    # 回读校验
    back = read(path)
    for old, new in pairs:
        if new not in back:
            print("  [FAIL] %s: 回读校验失败！" % name)
            return False
    print("  [OK]   %s: 回读校验通过" % name)
    return True


def main():
    print("=== 1) lvgl_renderer.cpp: bing UA -> 桌面 Chrome120 ===")
    r1 = step("UA", RENDER, [(OLD_UA, NEW_UA)])
    print("=== 2) layout_engine.cpp: SERP 壳子过滤 ===")
    r2 = step("func", LAYOUT, [(OLD_FUNC, NEW_FUNC),
                              (OLD_CALL, NEW_CALL),
                              (OLD_PHRASES, NEW_PHRASES)])
    print()
    print("RESULT:", "OK" if (r1 and r2) else "FAILED")
    return 0 if (r1 and r2) else 1


if __name__ == "__main__":
    sys.exit(main())
