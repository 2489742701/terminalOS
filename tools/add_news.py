#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""把热点新闻接进浏览器搜索首页（替换掉"待接入"占位框）。

复用浏览器那个**常驻**的 fetch 任务（g_fetchKind 分支），不再新建任务 ——
ESP32 的任务栈只能从内部 DRAM 分配，新建一个常驻任务等于常驻吃掉十几 KB。
"""
import io
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
F = os.path.join(ROOT, "src", "app", "browser_screen.cpp")

src = io.open(F, encoding="utf-8", errors="replace").read()
nl = "\r\n" if "\r\n" in src else "\n"
orig = src


def sub(old, new, tag):
    global src
    old = old.replace("\n", nl)
    new = new.replace("\n", nl)
    if new in src:
        print("SKIP: " + tag)
        return
    if old not in src:
        print("MISS: " + tag + "   <-- 没匹配上")
        return
    src = src.replace(old, new, 1)
    print("OK: " + tag)


# ── include ──
sub('#include "screensaver.h"\n',
    '#include "screensaver.h"\n#include "news.h"\n', "include")

# ── 全局状态 ──
sub("static lv_obj_t* g_searchKb = nullptr;      /* 键盘挂在屏上，不跟 content 一起清 */\n",
    """static lv_obj_t* g_searchKb = nullptr;      /* 键盘挂在屏上，不跟 content 一起清 */

/* ── 热点新闻 ──
   数据由后台 fetch 任务填（g_fetchKind=FETCH_NEWS 分支），UI 只读。
   ⚠️ 数组是全局静态：网页内容会被 lv_obj_clean 清掉，新闻不能跟着没。 */
static NewsItem g_news[NEWS_MAX_ITEMS];
static int g_newsCount = 0;
static char g_newsPlatform[16] = "baidu";
static volatile int g_fetchKind = 0;   /* 0=网页 1=新闻 */
#define FETCH_WEB  0
#define FETCH_NEWS 1
""", "globals")

# ── fetch_task 分支（放在解析网页之前）──
sub("""    String url = g_fetchUrl;
    if (!url.startsWith("http://") && !url.startsWith("https://"))
      url = "http://" + url;
""",
    """    /* 热点新闻是纯 JSON，不走 HTML 引擎：自己下载 + 解析，填 g_news。
       复用这个常驻任务，省掉再开一个任务栈（任务栈只能从 DRAM 分配）。 */
    if (g_fetchKind == FETCH_NEWS) {
      g_newsCount = news_fetch(g_newsPlatform, g_news, NEWS_MAX_ITEMS);
      Serial.printf("[Browser] news %s -> %d items, DRAM free=%u\\n",
                    g_newsPlatform, g_newsCount,
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
      g_taskDone = true;
      continue;
    }

    String url = g_fetchUrl;
    if (!url.startsWith("http://") && !url.startsWith("https://"))
      url = "http://" + url;
""", "fetch branch")

# ── 启动新闻加载 + 点击新闻条目 ──
sub("/* 聚焦搜索框才弹键盘。键盘常驻隐藏、只切 HIDDEN —— 反复 create/delete 会打碎堆。 */",
    """/* ── 热点新闻：起后台任务拉一个平台 ── */
static void startNews(const char* platform) {
  if (g_state == BROWSER_LOADING) { toast("正在加载，请稍等"); return; }
  ensureFetchTask();
  if (!g_fetchTask) return;

  snprintf(g_newsPlatform, sizeof(g_newsPlatform), "%s", platform);
  g_fetchKind = FETCH_NEWS;
  g_taskDone = false;
  g_state = BROWSER_LOADING;
  toastHide();
  showLoadingOverlay();
  updateNavButtons();
  ScreenSaver::setSuppressed(true);
  xTaskNotifyGive(g_fetchTask);
}

static void news_platform_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char* code = (const char*)lv_event_get_user_data(e);
  if (!code) return;
  Serial.printf("[Browser] news platform: %s\\n", code);
  startNews(code);
}

/* 点新闻标题 -> 打开原文。⚠️ 只置待办，真正的导航在下一 tick（老规矩） */
static void news_item_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (idx < 0 || idx >= g_newsCount) return;
  g_linkPending = String(g_news[idx].url);
  g_linkPendingSet = true;
}

/* 聚焦搜索框才弹键盘。键盘常驻隐藏、只切 HIDDEN —— 反复 create/delete 会打碎堆。 */""",
    "news fns")

# ── 把"新闻源"占位框换成真列表 ──
sub("""  /* ── 新闻源占位（等 master 给源再填） ── */
  lv_obj_t* newsTitle = lv_label_create(g_content);
  lv_label_set_text(newsTitle, "新闻源");
  lv_obj_set_style_text_color(newsTitle, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(newsTitle, &font_zh_16, 0);

  lv_obj_t* box = lv_obj_create(g_content);
  lv_obj_set_width(box, lv_pct(100));
  lv_obj_set_height(box, 110);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(box, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0x333333), 0);
  lv_obj_set_style_radius(box, 8, 0);
  lv_obj_t* hint = lv_label_create(box);
  lv_label_set_text(hint, "待接入");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(hint, &font_zh_16, 0);
  lv_obj_center(hint);
""",
    """  /* ── 热点新闻（news.orz.ai）──
     平台 chip 点了会起后台任务；条目点了打开原文。数据来自 g_news，
     搜索首页每次重建时都重画一遍 —— 所以不用管"刷新后 UI 怎么更新"。 */
  lv_obj_t* newsTitle = lv_label_create(g_content);
  lv_label_set_text(newsTitle, "热点新闻");
  lv_obj_set_style_text_color(newsTitle, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(newsTitle, &font_zh_16, 0);

  lv_obj_t* npRow = makeRow(g_content, true);
  for (int i = 0; i < kNewsPlatformCount; i++) {
    lv_obj_t* b = makeChipBtn(npRow, kNewsPlatforms[i].name, news_platform_cb,
                              (void*)kNewsPlatforms[i].code);
    lv_obj_set_size(b, 76, 36);
  }

  lv_obj_t* box = lv_obj_create(g_content);
  lv_obj_set_width(box, lv_pct(100));
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(box, 8, 0);
  lv_obj_set_style_pad_gap(box, 6, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(box, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0x333333), 0);
  lv_obj_set_style_radius(box, 8, 0);

  if (g_newsCount <= 0) {
    lv_obj_t* hint = lv_label_create(box);
    lv_label_set_text(hint, g_newsCount < 0 ? "拉取失败，换个源试试" : "点上面的源加载");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(hint, &font_zh_16, 0);
  } else {
    for (int i = 0; i < g_newsCount && i < NEWS_MAX_ITEMS; i++) {
      lv_obj_t* it = lv_btn_create(box);
      lv_obj_set_width(it, lv_pct(100));
      lv_obj_set_height(it, LV_SIZE_CONTENT);
      lv_obj_set_style_bg_opa(it, LV_OPA_TRANSP, 0);
      lv_obj_set_style_bg_color(it, lv_color_hex(0x222222), LV_STATE_PRESSED);
      lv_obj_set_style_bg_opa(it, LV_OPA_COVER, LV_STATE_PRESSED);
      lv_obj_set_style_border_width(it, 0, 0);
      lv_obj_set_style_radius(it, 6, 0);
      lv_obj_set_style_pad_all(it, 4, 0);
      lv_obj_add_event_cb(it, news_item_cb, LV_EVENT_CLICKED,
                          (void*)(intptr_t)i);

      /* 序号用 ASCII，标题用中文字体（montserrat 是纯拉丁，会出豆腐块） */
      lv_obj_t* lab = lv_label_create(it);
      char num[8];
      snprintf(num, sizeof(num), "%d. ", i + 1);
      lv_label_set_text(lab, (String(num) + g_news[i].title).c_str());
      lv_obj_set_width(lab, lv_pct(100));
      lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_color(lab, lv_color_hex(0xDDDDDD), 0);
      lv_obj_set_style_text_font(lab, &font_zh_16, 0);
    }
  }
""", "news ui")

# ── tick 收尾：新闻加载完 -> 回搜索首页重画 ──
sub("""  if (g_uiPendingKind != UI_PEND_NONE) {
    int kind = g_uiPendingKind;
    g_uiPendingKind = UI_PEND_NONE;
    if (kind == UI_PEND_DOWNLOADS) showDownloadsHome();
    else showSearchHome();
    return;
  }
""",
    """  if (g_uiPendingKind != UI_PEND_NONE) {
    int kind = g_uiPendingKind;
    g_uiPendingKind = UI_PEND_NONE;
    if (kind == UI_PEND_DOWNLOADS) showDownloadsHome();
    else showSearchHome();
    return;
  }

  /* 新闻拉完了：收掉遮罩，回搜索首页把列表画出来。
     ⚠️ 这里只是置标志 + 改状态，重建交给下一 tick 的 UI_PEND 通道 ——
        在 tick 里重建没风险，但保持一条路更好排查。 */
  if (g_fetchKind == FETCH_NEWS && g_taskDone) {
    g_taskDone = false;
    g_fetchKind = FETCH_WEB;
    g_state = BROWSER_LOADED;
    g_currentUrl = "";
    hideLoadingOverlay();
    ScreenSaver::setSuppressed(false);
    if (g_urlArea && lv_obj_is_valid(g_urlArea))
      lv_label_set_text(g_urlArea, "热点新闻");
    toast(g_newsCount > 0 ? "已更新" : "没有拿到数据");
    g_uiPendingKind = UI_PEND_SEARCH;
    return;
  }
""", "tick finish")

if src != orig:
    io.open(F, "w", encoding="utf-8", newline="").write(src)
    print("written", F)
else:
    print("NO CHANGE")
