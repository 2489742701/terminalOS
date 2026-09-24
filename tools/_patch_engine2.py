# -*- coding: utf-8 -*-
"""搜索引擎加回两个：必应 + 360，**另起一行**显示（master："换行，加两个搜索引擎"）。

跟上一版"点一下循环切换"不同：这回两个 chip 都摆出来，直接点选，
选中的那个高亮（白底黑字），一眼知道当前用哪个。
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"
s = io.open(P, encoding="utf-8").read()

old = """/* 搜索引擎：目前**只有必应**（master 2026-09-25 拍板退回单一引擎）。
   历史：曾短暂加过 360(www.so.com) 和百度，后来撤了 ——
     36氪(36kr) 是科技媒体、**没有搜索**，跟 360 不是一家，别再搞混。
   ⚠️ 必应必须用桌面 Chrome120 UA（移动 UA 只给 5 条、无分页），
      这条规矩在 browser_engine 那边，别动。
   ⚠️ 要再加引擎：往 kEngines 里加一行、再把首页那个切换 chip 加回来即可。 */
struct SearchEngine { const char* name; const char* tpl; };
static const SearchEngine kEngines[] = {
    {"必应", "https://cn.bing.com/search?q="},
};
static const int kEngineCount = (int)(sizeof(kEngines) / sizeof(kEngines[0]));
static int g_engineIdx = 0;
"""
new = """/* 搜索引擎（master 2026-09-25：必应 + 360，两个都摆出来直接点选）
   ⚠️ 36氪(36kr) 是科技媒体、**没有搜索**，跟 360 不是一家，别再搞混。
   ⚠️ 必应必须用桌面 Chrome120 UA（移动 UA 只给 5 条、无分页），
      这条规矩在 browser_engine 那边，别动。
   360 设备实测：www.so.com/s?q=esp32 -> 110 widget / 44 链接 / 390ms，能用。 */
struct SearchEngine { const char* name; const char* tpl; };
static const SearchEngine kEngines[] = {
    {"必应", "https://cn.bing.com/search?q="},
    {"360",  "https://www.so.com/s?q="},
};
static const int kEngineCount = (int)(sizeof(kEngines) / sizeof(kEngines[0]));
static int g_engineIdx = 0;

/* 选某个引擎（index）。切完重画首页显示当前选中态 */
static void engine_pick_cb(lv_event_t* e);
"""
assert s.count(old) == 1
s = s.replace(old, new, 1)

# 回调实现：放在 startSearch 之后
old2 = """static void search_go_cb(lv_event_t* e) {"""
new2 = """static void engine_pick_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (idx < 0 || idx >= kEngineCount) return;
  g_engineIdx = idx;
  Serial.printf("[Search] engine -> %s\\n", kEngines[idx].name);
  g_uiPendingKind = UI_PEND_SEARCH;   /* 下一 tick 重画，别在回调里 clean */
}

static void search_go_cb(lv_event_t* e) {"""
assert s.count(old2) == 1
s = s.replace(old2, new2, 1)

# 首页新增一行：两个引擎 chip
old3 = """  lv_obj_t* go = makeChipBtn(row, "搜索", search_go_cb, NULL);"""
new3 = """  lv_obj_t* go = makeChipBtn(row, "搜索", search_go_cb, NULL);

  /* 搜索引擎**另起一行**（master 2026-09-25），两个都摆出来直接点选，
     选中的白底黑字 —— 比"点一下循环切换"清楚。 */
  lv_obj_t* egRow = makeRow(g_content, false);
  for (int i = 0; i < kEngineCount; i++) {
    lv_obj_t* b = makeChipBtn(egRow, kEngines[i].name, engine_pick_cb,
                              (void*)(intptr_t)i);
    lv_obj_set_size(b, 76, 32);
    if (i == g_engineIdx) {
      lv_obj_set_style_bg_color(b, lv_color_white(), 0);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
      lv_obj_set_style_border_color(b, lv_color_white(), 0);
      lv_obj_t* lb = lv_obj_get_child(b, 0);
      if (lb) lv_obj_set_style_text_color(lb, lv_color_black(), 0);
    }
  }"""
assert s.count(old3) == 1
s = s.replace(old3, new3, 1)

io.open(P, "w", encoding="utf-8", newline="").write(s)
print("engines ok")
