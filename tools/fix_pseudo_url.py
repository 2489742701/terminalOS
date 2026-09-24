#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
修"伪链接被当成真网页下载"（2026-09-23 verify_fix 实测发现）。

现象：乐鑫官网的菜单里有 href="javascript: void(0);" 这类空按钮。
充分而是 designate 域名后被拼成
    https://host/path/javascript: void(0);
下载器照抓不误 —— 服务器返回一个 507KB 的 404 页，解析出 4780 个节点，
DRAM 从 207KB 掉到 72KB。多点几下这种就该重启了。

所以两道关：
 1. link_click_cb 开头拦掉一切伪协议/伪链接（含被域名前缀包装的 javascript:）
 2. startFetch 入口再兜一次底 —— 任何漏网的不合法 URL 不许走网络
"""
import io
import os

P = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp'

raw = io.open(P, encoding='utf-8', errors='replace').read()
NL = '\r\n' if '\r\n' in raw else '\n'
S = raw.replace('\r\n', '\n')

OLD = """static void link_click_cb(const char* url) {
  String target = resolveMaybeRelative(url);
  if (!target.length()) {
    Serial.printf("[Browser] link ignored (unresolvable): %s\\n", url);
    return;
  }
  g_linkPending = target;
  g_linkPendingSet = true;
}
"""

NEW = """/* ══ 伪链接识别 ═══════════════════════════════════════════════════════════
 * 网页上大量 href 不是真地址：
 *   javascript: void(0);   —— 纯 JS 空按钮（乐鑫官网菜单里一大堆）
 *   mailto: tel: intent:   —— 唤起别的 App
 *   #anchor                —— 页内锚点
 * 这些一旦被当成 URL 下载，服务器往往回一个几百 KB 的 404/首页，
 * 解析出几千个节点，DRAM 一次掉 130KB —— 连点几下就重启。
 *
 * ⚠️ 难点：这些 href 经常已经被拼上了域名前缀，变成
 *    https://host/path/javascript: void(0);
 * 所以除了看前缀，还得看**子串**里有没有 javascript:。
 * ═════════════════════════════════════════════════════════════════════════ */
static bool isPseudoUrl(const char* u) {
  if (!u || !u[0]) return true;
  if (u[0] == '#') return true;                 /* 页内锚点 */

  static const char* kSchemes[] = {
      "javascript:", "mailto:", "tel:", "data:", "about:", "blob:",
      "sms:", "intent:", "weixin:", "alipays:", "viber:", nullptr};
  for (int i = 0; kSchemes[i]; i++) {
    size_t n = strlen(kSchemes[i]);
    if (strncasecmp(u, kSchemes[i], n) == 0) return true;      /* 前缀 */
  }
  /* 被域名前缀包装过的形态：子串里有 javascript: 也一律不要 */
  if (strcasestr(u, "javascript:") != nullptr) return true;
  if (strcasestr(u, "void(0)") != nullptr) return true;
  return false;
}

static void link_click_cb(const char* url) {
  if (isPseudoUrl(url)) {
    Serial.printf("[Browser] link ignored (pseudo): %.48s\\n", url ? url : "(null)");
    return;
  }
  String target = resolveMaybeRelative(url);
  if (!target.length()) {
    Serial.printf("[Browser] link ignored (unresolvable): %s\\n", url);
    return;
  }
  if (isPseudoUrl(target.c_str())) {
    /* 相对解析也可能把 javascript: 拼进来（宿主 + 伪路径），再拦一次 */
    Serial.printf("[Browser] link ignored (pseudo after resolve): %.48s\\n",
                  target.c_str());
    return;
  }
  g_linkPending = target;
  g_linkPendingSet = true;
}
"""

assert OLD in S, 'link_click_cb anchor missing'
S = S.replace(OLD, NEW, 1)
print('  伪链接过滤 OK')

# ── startFetch 入口兜底 ──────────────────────────────────────────────────
OLD2 = """  ensureEngineInit();
  ensureFetchTask();
  if (!g_fetchTask) return;  // 任务缺失，ensureFetchTask 已打印原因
"""
NEW2 = """  /* 兜底：任何漏网的伪 URL 都不许走到网络去 —— 代价是几百 KB 下载 +
     几千节点解析 + DRAM 一次掉上百 KB（2026-09-23 实测）。 */
  if (isPseudoUrl(url.c_str())) {
    Serial.printf("[Browser] refuse fetch (not a real URL): %.48s\\n", url.c_str());
    return;
  }

  ensureEngineInit();
  ensureFetchTask();
  if (!g_fetchTask) return;  // 任务缺失，ensureFetchTask 已打印原因
"""
assert OLD2 in S, 'startFetch anchor missing'
S = S.replace(OLD2, NEW2, 1)
print('  startFetch 兜底 OK')

io.open(P, 'w', encoding='utf-8', newline='').write(S.replace('\n', NL))
print('written, size =', os.path.getsize(P))
