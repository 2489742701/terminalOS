#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
浏览器功能改动第二批（2026-09-23 master 要求）：
「下载的网站」列表页 —— 列表 + 单项删除 + 清空 + 点开重看（离线，走页面缓存）。

两个安全规矩（这个项目血泪换来的）：
 1. 回调里不能直接重建列表/开页面 —— 那是正在回调自己的 widget，等于在 LVGL
    派发事件时把对象 lv_obj_clean 掉。一律走 tick 里的延迟通道 g_uiPendingKind。
 2. LittleFS 挂载后**不 end()** —— 会把正在运行的页面服务器搞成 404。
    LittleFS.begin() 是幂等的（已挂载直接返回 true）。
"""
import io
import os

P = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp'

raw = io.open(P, encoding='utf-8', errors='replace').read()
NL = '\r\n' if '\r\n' in raw else '\n'
S = raw.replace('\r\n', '\n')

# ── A. 前向声明 ───────────────────────────────────────────────────────────
A_OLD = 'static void showSearchHome();   /* 定义在下方（导航回调里要用） */'
A_NEW = A_OLD + '\nstatic void showDownloadsHome();  /* 下载列表（定义在下方） */'
assert A_OLD in S
S = S.replace(A_OLD, A_NEW, 1)
print('A 前向声明 OK')

# ── B. 搜索首页：新闻源下面加「下载的网站」入口 ────────────────────────────
B_OLD = """  lv_obj_t* hint = lv_label_create(box);
  lv_label_set_text(hint, "待接入");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(hint, &font_zh_16, 0);
  lv_obj_center(hint);
"""
B_NEW = B_OLD + """
  /* ── 下载管理入口：点进去看 LittleFS 里存下来的所有页面 ──
     回调里只置一个"待办"，真正的重建在下一 tick（见 g_uiPendingKind 的说明）。 */
  lv_obj_t* dlRow = makeRow(g_content, false);
  makeChipBtn(dlRow, "下载的网站", dl_open_list_cb, NULL);
"""
assert B_OLD in S
S = S.replace(B_OLD, B_NEW, 1)
print('B 搜索首页入口 OK')

# ── C. 在第一个 namespace 结束前插入下载列表的全部实现 ─────────────────────
C_OLD = """  Serial.println("[Browser] search home shown");
}

}  // namespace
"""
C_NEW = """  Serial.println("[Browser] search home shown");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 下载的网站（LittleFS 列表 + 删除管理）
 *
 * 文件名只有 URL 的 hash（/pXXXXXXXX.html），所以「下载」时我们把原 URL 写在
 * 文件第一行 <!--URL:xxx--> —— 列表页靠它显示可读名字，离线重看也靠它回填。
 *
 * ⚠️ 两条安全规矩（都是这个项目用崩溃换来的）：
 *   1. **回调里不许重建列表 / 不许开页面**。那样等于 LVGL 正在派发某个 widget
 *      的事件时把它的父容器 lv_obj_clean 掉。所有跳转统一走 tick 里的延迟通道。
 *      只有纯文件操作（删除）可以当场做。
 *   2. **LittleFS 挂载后不许 end()** —— 会把正在跑的页面服务器整成 404
 *      （listSavedPages 踩过。LittleFS.begin() 幂等，已挂载时直接返回 true。
 * ═══════════════════════════════════════════════════════════════════════════ */
#define MAX_DL_ROWS 14

enum PendingUiKind { UI_PEND_NONE = 0, UI_PEND_SEARCH, UI_PEND_DOWNLOADS };

/* 自建 UI 之间的跳转：延迟到下一 tick 执行（理由见上） */
static volatile int g_uiPendingKind = UI_PEND_NONE;
static char g_dlPendingPath[64] = {0};   /* 待离线打开的下载文件 */

static void showDownloadsHome();   /* 下面会用到（定义在最后） */

static void dl_goto(int kind) {
  g_uiPendingKind = kind;
}

static void dl_open_list_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  dl_goto(UI_PEND_DOWNLOADS);
}

static void dl_back_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  dl_goto(UI_PEND_SEARCH);
}

static void dl_open_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char* p = (const char*)lv_event_get_user_data(e);
  if (!p) return;
  snprintf(g_dlPendingPath, sizeof(g_dlPendingPath), "%s", p);
}

/* chip 的 user_data 是 malloc 出来的路径；contentReset() 把整棵内容树 clean 掉时
   由这个回调回收 —— 否则每进一次列表就漏一份，几天后 DRAM 单调下降到崩。 */
static void dl_chip_free_ud(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  char* p = (char*)lv_event_get_user_data(e);
  if (p) free(p);
}

/* 取出下载时写在第一行的 <!--URL:xxx--> */
static bool savedFileUrl(const char* path, String& url) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  String first = f.readStringUntil('\\n');
  f.close();
  int a = first.indexOf("<!--URL:");
  if (a < 0) return false;
  int b = first.indexOf("-->", a + 8);
  if (b < 0) return false;
  url = first.substring(a + 8, b);
  return url.length() > 0;
}

/* 把一个已下载的页面塞回页面缓存 —— 之后 startFetch(url) 直接命中，不再联网。
   pageCachePut 内部会拷一份，这里的临时 buffer 用完就还。 */
static bool savedToCache(const char* path, String& url) {
  if (!savedFileUrl(path, url)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t sz = f.size();
  bool ok = false;
  if (sz > 0 && sz <= PAGE_CACHE_MAX_ENTRY) {
    uint8_t* buf = (uint8_t*)heap_caps_malloc(
        sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf) {
      f.readBytes((char*)buf, sz);
      pageCachePut(url, buf, sz);
      heap_caps_free(buf);
      ok = true;
    } else {
      Serial.println("[Browser] savedToCache: PSRAM alloc failed");
    }
  }
  f.close();
  return ok;
}

static void dl_del_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char* p = (const char*)lv_event_get_user_data(e);
  if (!p) return;
  if (!LittleFS.begin(false)) { toast("存储不可用"); return; }
  bool ok = LittleFS.remove(p);
  Serial.printf("[Browser] delete saved %s -> %s\\n", p, ok ? "ok" : "FAIL");
  if (ok) {
    toast("已删除");
    g_uiPendingKind = UI_PEND_DOWNLOADS;   /* 下一 tick 重建列表 */
  } else {
    toast("删除失败");
  }
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

/* ── 列表本体 ──
   每行 = [可读名字 + 大小] [删]：点名字离线重看，点「删」删掉这一项。 */
static void showDownloadsHome() {
  if (!g_content || !lv_obj_is_valid(g_content)) return;
  hideLoadingOverlay();
  contentReset();
  g_state = BROWSER_LOADED;
  g_currentUrl = "";
  if (g_urlArea && lv_obj_is_valid(g_urlArea))
    lv_label_set_text(g_urlArea, "下载的网站");
  updateNavButtons();

  lv_obj_t* title = lv_label_create(g_content);
  lv_label_set_text(title, "下载的网站");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &font_zh_16, 0);

  lv_obj_t* row = makeRow(g_content, false);
  makeChipBtn(row, "返回", dl_back_cb, NULL);
  makeChipBtn(row, "清空全部", dl_clear_cb, NULL);

  /* ⚠️ 挂载后不 end()：LittleFS.begin() 幂等，end() 反而会打挂页面服务器 */
  if (!LittleFS.begin(false)) {
    lv_obj_t* m = lv_label_create(g_content);
    lv_label_set_text(m, "存储不可用");
    lv_obj_set_style_text_color(m, lv_color_hex(0xFF6666), 0);
    lv_obj_set_style_text_font(m, &font_zh_16, 0);
    return;
  }

  File root = LittleFS.open("/");
  File f = root.openNextFile();
  int n = 0;
  while (f && n < MAX_DL_ROWS) {
    String nm = String(f.name());
    size_t sz = f.size();
    f = root.openNextFile();          /* 先推进，后面要 open 同一个文件读 URL */
    if (!nm.endsWith(".html")) continue;
    if (!nm.startsWith("/")) nm = String("/") + nm;

    String url;
    /* 下载功能加上 URL 头之前存的老文件没有头 —— 退化成显示文件名 */
    if (!savedFileUrl(nm.c_str(), url)) url = nm;

    String shown = url;
    int se = shown.indexOf("://");
    if (se > 0) shown = shown.substring(se + 3);   /* 砍掉 https:// */
    if (shown.length() > 26) shown = shown.substring(0, 26);
    char label[80];
    snprintf(label, sizeof(label), "%s  %uK", shown.c_str(), (unsigned)(sz / 1024));

    lv_obj_t* r = makeRow(g_content, false);
    char* dup = (char*)malloc(nm.length() + 1);
    char* dup2 = (char*)malloc(nm.length() + 1);
    if (!dup || !dup2) { free(dup); free(dup2); break; }
    strcpy(dup, nm.c_str());
    strcpy(dup2, nm.c_str());

    lv_obj_t* openBtn = makeChipBtn(r, label, dl_open_cb, dup);
    lv_obj_set_width(openBtn, 340);
    lv_obj_add_event_cb(openBtn, dl_chip_free_ud, LV_EVENT_DELETE, dup);
    lv_obj_t* delBtn = makeChipBtn(r, "删", dl_del_cb, dup2);
    lv_obj_set_width(delBtn, 52);
    lv_obj_add_event_cb(delBtn, dl_chip_free_ud, LV_EVENT_DELETE, dup2);
    n++;
  }

  if (n == 0) {
    lv_obj_t* m = lv_label_create(g_content);
    lv_label_set_text(m, "还没有下载任何页面");
    lv_obj_set_style_text_color(m, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(m, &font_zh_16, 0);
  }
  Serial.printf("[Browser] downloads list: %d rows\\n", n);
}

}  // namespace
"""
assert C_OLD in S
S = S.replace(C_OLD, C_NEW, 1)
print('C 下载列表实现 OK')

# ── D. tick 里的延迟 UI 通道 ──────────────────────────────────────────────
D_OLD = """  /* 首次进入：显示我们自己画的搜索首页，不再直接抓门户页 */
  if (g_firstLoad && g_state == BROWSER_IDLE) {"""
D_NEW = """  /* 自建 UI 之间的跳转（下载列表 ↔ 搜索首页）：widget 回调里只能置标志，
     真正的重建放这里 —— 否则是在 LVGL 派发期间删掉正在回调的对象。 */
  if (g_uiPendingKind != UI_PEND_NONE) {
    int kind = g_uiPendingKind;
    g_uiPendingKind = UI_PEND_NONE;
    if (kind == UI_PEND_DOWNLOADS) showDownloadsHome();
    else showSearchHome();
    return;
  }

  /* 打开某个已下载的页面：先把文件内容塞回页面缓存，再走常规加载路径，
     startFetch 会命中缓存跳过联网（离线也能看）。 */
  if (g_dlPendingPath[0]) {
    char path[64];
    snprintf(path, sizeof(path), "%s", g_dlPendingPath);
    g_dlPendingPath[0] = '\\0';
    String url;
    if (savedToCache(path, url)) {
      historyPush(url);
      setUrlText(url.c_str());
      startFetch(url);
    } else {
      toast("读取失败");
    }
    return;
  }

  /* 首次进入：显示我们自己画的搜索首页，不再直接抓门户页 */
  if (g_firstLoad && g_state == BROWSER_IDLE) {"""
assert D_OLD in S
S = S.replace(D_OLD, D_NEW, 1)
print('D tick 延迟通道 OK')

io.open(P, 'w', encoding='utf-8', newline='').write(S.replace('\n', NL))
print('written, size =', os.path.getsize(P))
