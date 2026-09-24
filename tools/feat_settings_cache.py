#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
浏览器功能改动第三批：
 1. browser_screen: clearSavedPages() 抽出来，并暴露 3 个外部接口给设置屏
 2. settings.cpp: 加「浏览器缓存 / 已下载页面」两行 + 清理缓存 / 清理下载 两个按钮
"""
import io
import os

CPP = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp'
H = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.h'
SET = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\settings.cpp'


def load(p):
    raw = io.open(p, encoding='utf-8', errors='replace').read()
    return raw, ('\r\n' if '\r\n' in raw else '\n'), raw.replace('\r\n', '\n')


def save(p, s, nl):
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


# ══ 1. browser_screen.cpp ════════════════════════════════════════════════
raw, NL, S = load(CPP)

# 1a. 抽出 clearSavedPages()
OLD = """/* 清空全部：破坏性操作，要求再点一次确认（3 秒有效） */
static uint32_t s_clearArmUntil = 0;
static void dl_clear_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  uint32_t now = millis();
  if (s_clearArmUntil == 0 || now > s_clearArmUntil) {
    s_clearArmUntil = now + 3000;
    toast("再点一次确认清空");
    return;
  }
  s_clearArmUntil = 0;
  if (!LittleFS.begin(false)) { toast("存储不可用"); return; }
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  int n = 0;
  while (f) {
    String nm = String(f.name());
    f = root.openNextFile();          /* 先推进迭代，再删当前项 */
    if (!nm.endsWith(".html")) continue;
    if (!nm.startsWith("/")) nm = String("/") + nm;
    if (LittleFS.remove(nm)) n++;
  }
  Serial.printf("[Browser] cleared %d saved pages\\n", n);
  char msg[48];
  snprintf(msg, sizeof(msg), "已清空 %d 个", n);
  toast(msg);
  g_uiPendingKind = UI_PEND_DOWNLOADS;
}
"""
NEW = """/* 删掉 LittleFS 里所有已存页面。返回删了几个。
   ⚠️ 挂载后不 end() —— 会打挂正在运行的页面服务器。 */
static int clearSavedPages() {
  if (!LittleFS.begin(false)) return -1;
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  int n = 0;
  while (f) {
    String nm = String(f.name());
    f = root.openNextFile();          /* 先推进迭代，再删当前项 */
    if (!nm.endsWith(".html")) continue;
    if (!nm.startsWith("/")) nm = String("/") + nm;
    if (LittleFS.remove(nm)) n++;
  }
  Serial.printf("[Browser] cleared %d saved pages\\n", n);
  return n;
}

/* 清空全部：破坏性操作，要求再点一次确认（3 秒有效） */
static uint32_t s_clearArmUntil = 0;
static void dl_clear_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  uint32_t now = millis();
  if (s_clearArmUntil == 0 || now > s_clearArmUntil) {
    s_clearArmUntil = now + 3000;
    toast("再点一次确认清空");
    return;
  }
  s_clearArmUntil = 0;
  int n = clearSavedPages();
  if (n < 0) { toast("存储不可用"); return; }
  char msg[48];
  snprintf(msg, sizeof(msg), "已清空 %d 个", n);
  toast(msg);
  g_uiPendingKind = UI_PEND_DOWNLOADS;
}
"""
assert OLD in S, '1a anchor missing'
S = S.replace(OLD, NEW, 1)
print('1a clearSavedPages 抽出 OK')

# 1b. 暴露外部接口（放在 BrowserScreen_listPages 旁边）
OLD2 = """void BrowserScreen_serve(bool on) { on ? pageServerStart() : pageServerStop(); }
void BrowserScreen_listPages() { listSavedPages(); }
"""
NEW2 = """void BrowserScreen_serve(bool on) { on ? pageServerStart() : pageServerStop(); }
void BrowserScreen_listPages() { listSavedPages(); }

/* ── 给设置屏的「缓存/下载查看与清理」接口 ──
   2026-09-23 master 要求：内存策略是「只保留当前页 + 上一页」，
   但用户要有地方看见占了多大、能手删。这里是那个出口。 */
void BrowserScreen_cacheInfo(int* pages, size_t* bytes,
                             int* dlCount, size_t* dlBytes) {
  if (pages) *pages = 0;
  if (bytes) *bytes = 0;
  for (int i = 0; i < PAGE_CACHE_SLOTS; i++) {
    if (!g_pageCache[i].data) continue;
    if (pages) (*pages)++;
    if (bytes) *bytes += g_pageCache[i].len;
  }
  if (dlCount) *dlCount = 0;
  if (dlBytes) *dlBytes = 0;
  /* ⚠️ 挂载后不 end() */
  if (!LittleFS.begin(false)) return;
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  while (f) {
    if (String(f.name()).endsWith(".html")) {
      if (dlCount) (*dlCount)++;
      if (dlBytes) *dlBytes += f.size();
    }
    f = root.openNextFile();
  }
}

void BrowserScreen_clearCache() {
  for (int i = 0; i < PAGE_CACHE_SLOTS; i++) pageCacheFree(i);
  Serial.println("[Browser] page cache cleared");
}

int BrowserScreen_clearDownloads() { return clearSavedPages(); }
"""
assert OLD2 in S, '1b anchor missing'
S = S.replace(OLD2, NEW2, 1)
print('1b 外部接口 OK')
save(CPP, S, NL)

# ══ 2. browser_screen.h ══════════════════════════════════════════════════
raw, NLH, SH = load(H)
OLD3 = 'void BrowserScreen_listPages();'
if 'BrowserScreen_cacheInfo' not in SH:
    SH = SH.replace(
        OLD3,
        OLD3 + '\n\n'
        '/* 缓存/下载查看与清理（设置屏用） */\n'
        'void BrowserScreen_cacheInfo(int* pages, size_t* bytes,\n'
        '                             int* dlCount, size_t* dlBytes);\n'
        'void BrowserScreen_clearCache();\n'
        '/* 返回删掉的个数；-1 = 存储不可用 */\n'
        'int  BrowserScreen_clearDownloads();', 1)
    print('2  header 声明 OK')
else:
    print('2  header 已存在，跳过')
save(H, SH, NLH)

# ══ 3. settings.cpp ═════════════════════════════════════════════════════
raw, NLS, SS = load(SET)

SS = SS.replace('#include <lvgl.h>\n#include <WiFi.h>',
                '#include <lvgl.h>\n#include <WiFi.h>\n#include "browser_screen.h"', 1)
if 'browser_screen.h' in SS:
    print('3a include OK')

# 变量 + 刷新函数
SS = SS.replace(
    "lv_obj_t* g_statusLab = nullptr;\nlv_obj_t* g_timeSrcVal = nullptr;   /* \"时间源\" 那行的值 */",
    "lv_obj_t* g_statusLab = nullptr;\nlv_obj_t* g_timeSrcVal = nullptr;   /* \"时间源\" 那行的值 */\n"
    "lv_obj_t* g_cacheVal = nullptr;     /* 浏览器缓存 */\nlv_obj_t* g_dlVal = nullptr;        /* 已下载页面 */", 1)
print('3b 变量 OK')

OLD4 = """void ntp_status_cb(lv_timer_t* t) {
  (void)t;
  refreshTimeSourceLabel();"""
NEW4 = """/* 存储占用：页面缓存 + LittleFS 里下载下来的页面 */
static void refreshStorageLabel() {
  int pages = 0, dl = 0;
  size_t cb = 0, db = 0;
  BrowserScreen_cacheInfo(&pages, &cb, &dl, &db);
  char v[48];
  if (g_cacheVal && lv_obj_is_valid(g_cacheVal)) {
    snprintf(v, sizeof(v), "%d 页 / %u KB", pages, (unsigned)(cb / 1024));
    lv_label_set_text(g_cacheVal, v);
  }
  if (g_dlVal && lv_obj_is_valid(g_dlVal)) {
    snprintf(v, sizeof(v), "%d 个 / %u KB", dl, (unsigned)(db / 1024));
    lv_label_set_text(g_dlVal, v);
  }
}

static void clear_cache_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  BrowserScreen_clearCache();
  refreshStorageLabel();
  if (g_statusLab && lv_obj_is_valid(g_statusLab))
    lv_label_set_text(g_statusLab, "浏览器缓存已清理");
}

/* 破坏性操作：3 秒内再点一次才真删 */
static uint32_t s_dlArmUntil = 0;
static void clear_dl_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  uint32_t now = millis();
  if (s_dlArmUntil == 0 || now > s_dlArmUntil) {
    s_dlArmUntil = now + 3000;
    if (g_statusLab && lv_obj_is_valid(g_statusLab))
      lv_label_set_text(g_statusLab, "再点一次确认清空下载");
    return;
  }
  s_dlArmUntil = 0;
  int n = BrowserScreen_clearDownloads();
  refreshStorageLabel();
  char m[48];
  if (n < 0) snprintf(m, sizeof(m), "存储不可用");
  else snprintf(m, sizeof(m), "已删除 %d 个页面", n);
  if (g_statusLab && lv_obj_is_valid(g_statusLab)) lv_label_set_text(g_statusLab, m);
}

void ntp_status_cb(lv_timer_t* t) {
  (void)t;
  refreshTimeSourceLabel();
  refreshStorageLabel();"""
assert OLD4 in SS, '3c anchor missing'
SS = SS.replace(OLD4, NEW4, 1)
print('3c 回调 OK')

# UI 行与按钮（放在亮度滑块之后、状态标签之前）
OLD5 = """  g_statusLab = lv_label_create(scr);
  lv_label_set_text(g_statusLab, "联网后自动对时（NTP），或点上方按钮手动校准");"""
NEW5 = """  /* ── 浏览器缓存 / 下载：看得见占比，也删得掉 ── */
  g_cacheVal = makeRow(scr, "浏览器缓存", "0 页 / 0 KB", 306);
  g_dlVal = makeRow(scr, "已下载页面", "0 个 / 0 KB", 340);

  lv_obj_t* cBtn = lv_btn_create(scr);
  lv_obj_set_size(cBtn, 180, 40);
  lv_obj_align(cBtn, LV_ALIGN_TOP_MID, -100, 372);
  lv_obj_set_style_bg_opa(cBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(cBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(cBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(cBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(cBtn, 1, 0);
  lv_obj_set_style_radius(cBtn, 10, 0);
  lv_obj_add_event_cb(cBtn, clear_cache_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(cBtn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t* cLab = lv_label_create(cBtn);
  lv_label_set_text(cLab, "清理缓存");
  lv_obj_set_style_text_color(cLab, lv_color_white(), 0);
  lv_obj_set_style_text_font(cLab, &font_zh_16, 0);
  lv_obj_center(cLab);

  lv_obj_t* dBtn = lv_btn_create(scr);
  lv_obj_set_size(dBtn, 180, 40);
  lv_obj_align(dBtn, LV_ALIGN_TOP_MID, 100, 372);
  lv_obj_set_style_bg_opa(dBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(dBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(dBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(dBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(dBtn, 1, 0);
  lv_obj_set_style_radius(dBtn, 10, 0);
  lv_obj_add_event_cb(dBtn, clear_dl_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(dBtn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t* dLab = lv_label_create(dBtn);
  lv_label_set_text(dLab, "清理下载");
  lv_obj_set_style_text_color(dLab, lv_color_white(), 0);
  lv_obj_set_style_text_font(dLab, &font_zh_16, 0);
  lv_obj_center(dLab);

  g_statusLab = lv_label_create(scr);
  lv_label_set_text(g_statusLab, "联网后自动对时（NTP），或点上方按钮手动校准");"""
assert OLD5 in SS, '3d anchor missing'
SS = SS.replace(OLD5, NEW5, 1)
print('3d UI OK')

# 状态标签下移 + 创建时刷新 + 删屏时置空
SS = SS.replace('lv_obj_align(g_statusLab, LV_ALIGN_TOP_MID, 0, 300);',
                'lv_obj_align(g_statusLab, LV_ALIGN_TOP_MID, 0, 424);', 1)
SS = SS.replace('  g_statusLab = nullptr;\n  g_timeSrcVal = nullptr;',
                '  g_statusLab = nullptr;\n  g_timeSrcVal = nullptr;\n'
                '  g_cacheVal = nullptr;\n  g_dlVal = nullptr;', 1)
SS = SS.replace('  refreshTimeSourceLabel();\n  if (!g_ntpTimer)',
                '  refreshTimeSourceLabel();\n  refreshStorageLabel();\n  if (!g_ntpTimer)', 1)
print('3e 收尾 OK')

save(SET, SS, NLS)
print('settings.cpp size =', os.path.getsize(SET))
