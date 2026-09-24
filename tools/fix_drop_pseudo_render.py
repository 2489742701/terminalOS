#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
「点了没反应 / 加载不出来」的两项改进（2026-09-23 诊断后）：

1. 伪链接**直接不渲染**（不只是点击时拦）。
   乐鑫官网 197 个"可点链接"里一大半是 javascript: void(0); 这种
   JS 下拉菜单占位按钮 —— 它们点了本来就不该有反应，却把本就只有 200 的
   widget 配额吃光，正文（id="main" 在 54.9% 处）根本排不到号。
   不渲染它们 = 页面清爽 + 配额留给正文。

2. 点击伪链接时给 toast，不再静默无反应（用户会以为又崩了）。
"""
import io
import os

LE = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\layout_engine.cpp'
BS = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp'


def load(p):
    raw = io.open(p, encoding='utf-8', errors='replace').read()
    return raw.replace('\r\n', '\n'), ('\r\n' if '\r\n' in raw else '\n')


def save(p, s, nl):
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))
    print('  saved', os.path.basename(p), os.path.getsize(p))


# ══ layout_engine.cpp ═══════════════════════════════════════════════════
S, NL = load(LE)

FUNC = """/* ── 伪链接判定：这些 href 点了本就不该有反应 ──────────────────────────────
 * 现代站点的导航里塞满了 javascript: void(0);（纯 JS 下拉菜单占位按钮）、
 * mailto:/tel:（唤起别的应用）、#anchor（页内锚点）。
 *
 * 2026-09-23 实测（乐鑫官网）：一页 197 个"可点链接"里大半是这类东西，
 * 而 widget 配额只有 MAX_WIDGETS=200 —— 菜单把配额吃光，正文
 * （id="main" 落在 54.9% 处）永远排不到号，表现就是"页面加载了但什么都看不到"。
 *
 * ⚠️ 难点：这些 href 常被拼上域名前缀，变成
 *    https://host/path/javascript: void(0);
 * 所以除了看前缀，还得看**子串**。
 */
static bool flat_is_pseudo_href(const char* h) {
  if (!h || !h[0]) return true;
  if (h[0] == '#') return true;                 /* 页内锚点 */
  static const char* kSchemes[] = {
      "javascript:", "mailto:", "tel:", "data:", "about:", "blob:",
      "sms:", "intent:", "weixin:", "alipays:", nullptr};
  for (int i = 0; kSchemes[i]; i++) {
    size_t n = strlen(kSchemes[i]);
    if (strncasecmp(h, kSchemes[i], n) == 0) return true;
  }
  if (strstr(h, "javascript:") != NULL) return true;
  if (strstr(h, "void(0)") != NULL) return true;
  return false;
}

static int layout_drop_junk(LayoutNode *node, int depth) {"""

OLD_SIG = "static int layout_drop_junk(LayoutNode *node, int depth) {"
assert OLD_SIG in S, 'drop_junk sig missing'
S = S.replace(OLD_SIG, FUNC, 1)
print('  flat_is_pseudo_href 定义 OK')

OLD_BODY = """  if (flat_is_serp_chrome_link(node)) {
    if (node->text_content) { free(node->text_content); node->text_content = NULL; }
    if (node->href)          { free(node->href);         node->href = NULL; }
    if (node->href_resolved) { free(node->href_resolved); node->href_resolved = NULL; }
    if (node->href_path)     { free(node->href_path);     node->href_path = NULL; }
    n++;
  }
  return n;
}"""
NEW_BODY = """  if (flat_is_serp_chrome_link(node)) {
    if (node->text_content) { free(node->text_content); node->text_content = NULL; }
    if (node->href)          { free(node->href);         node->href = NULL; }
    if (node->href_resolved) { free(node->href_resolved); node->href_resolved = NULL; }
    if (node->href_path)     { free(node->href_path);     node->href_path = NULL; }
    n++;
  }
  /* 伪链接（JS 菜单占位 / 锚点 / mailto:）整块不渲染：
     点了也不会有反应，却占着 MAX_WIDGETS 的配额。清掉 text_content
     渲染时就不再产控件 —— 省下的配额留给正文。 */
  if (node->href && flat_is_pseudo_href(node->href)) {
    if (node->text_content) { free(node->text_content); node->text_content = NULL; }
    if (node->href)          { free(node->href);         node->href = NULL; }
    if (node->href_resolved) { free(node->href_resolved); node->href_resolved = NULL; }
    if (node->href_path)     { free(node->href_path);     node->href_path = NULL; }
    n++;
  }
  return n;
}"""
assert OLD_BODY in S, 'drop_junk body missing'
S = S.replace(OLD_BODY, NEW_BODY, 1)
print('  伪链接不渲染 OK')
save(LE, S, NL)

# ══ browser_screen.cpp：点击时给 toast ═══════════════════════════════════
S2, NL2 = load(BS)
OLD = """  if (isPseudoUrl(url)) {
    Serial.printf("[Browser] link ignored (pseudo): %.48s\\n", url ? url : "(null)");
    return;
  }"""
NEW = """  if (isPseudoUrl(url)) {
    Serial.printf("[Browser] link ignored (pseudo): %.48s\\n", url ? url : "(null)");
    /* 以前是静默丢弃 —— 用户看到"点了没反应"会以为又崩了。
       明确告诉他是 JS 菜单，这类按钮在无 JS 的浏览器里本来就点不动。 */
    toast("此按钮需要 JavaScript");
    return;
  }"""
assert OLD in S2, 'link_click_cb pseudo branch missing'
S2 = S2.replace(OLD, NEW, 1)
print('  toast 提示 OK')
save(BS, S2, NL2)
