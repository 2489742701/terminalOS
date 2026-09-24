# -*- coding: utf-8 -*-
"""浏览器搜索引擎：加 360（www.so.com），可切换 必应 / 360 / 百度。

master：「360搜索也可以作为搜索引擎。可以用来搜索必应和360」。
实测 2026-09-25：https://www.so.com/s?q=<urlenc> -> 200 / 449KB（比必应小）。
⚠️ 必应那套 UA 规矩不变（桌面 Chrome120，移动 UA 只给 5 条）。
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"
src = io.open(P, encoding="utf-8").read()

old = """static void startSearch(const String& q) {
  String enc = urlEncode(q);
  if (!enc.length()) return;
  String url = String("https://cn.bing.com/search?q=") + enc;
  Serial.printf("[Search] \\"%s\\" -> %s\\n", q.c_str(), url.c_str());"""
new = """/* 搜索引擎（master 2026-09-25：必应之外再加 360）
   实测：www.so.com/s?q=  -> 200 / 449KB，能正常打开。
   ⚠️ 必应必须用桌面 Chrome120 UA（移动 UA 只给 5 条、无分页），
      这条规矩在 browser_engine 那边，别动。 */
struct SearchEngine { const char* name; const char* tpl; };
static const SearchEngine kEngines[] = {
    {"必应", "https://cn.bing.com/search?q="},
    {"360",  "https://www.so.com/s?q="},
    {"百度", "https://www.baidu.com/s?word="},
};
static const int kEngineCount = (int)(sizeof(kEngines) / sizeof(kEngines[0]));
static int g_engineIdx = 0;

static void startSearch(const String& q) {
  String enc = urlEncode(q);
  if (!enc.length()) return;
  String url = String(kEngines[g_engineIdx].tpl) + enc;
  Serial.printf("[Search] \\"%s\\" via %s -> %s\\n", q.c_str(),
                kEngines[g_engineIdx].name, url.c_str());"""
assert src.count(old) == 1
src = src.replace(old, new, 1)

# 搜索引擎切换 chip：放在"搜索"按钮那一行
old2 = """  lv_obj_t* go = makeChipBtn(row, "搜索", search_go_cb, NULL);"""
new2 = """  lv_obj_t* go = makeChipBtn(row, "搜索", search_go_cb, NULL);

  /* 搜索引擎切换：点一下换下一个（必应 -> 360 -> 百度 -> …），
     名字旁显示当前引擎，选中态白底黑字。 */
  lv_obj_t* eb = makeChipBtn(row, kEngines[g_engineIdx].name,
                             engine_switch_cb, NULL);
  lv_obj_set_size(eb, 68, 36);
  lv_obj_set_style_bg_color(eb, lv_color_hex(0x2a2a2a), 0);
  lv_obj_set_style_border_color(eb, lv_color_hex(0xFFD700), 0);"""
assert src.count(old2) == 1
src = src.replace(old2, new2, 1)

# 回调实现（放在 search_go_cb 之后）
old3 = """/* ── 热点新闻：起后台任务拉一个平台 ── */"""
new3 = """/* 切换搜索引擎：切完当场重画搜索首页（行的重建走 UI_PEND 通道，别在回调里 clean） */
static void engine_switch_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_engineIdx = (g_engineIdx + 1) % kEngineCount;
  Serial.printf("[Search] engine -> %s\\n", kEngines[g_engineIdx].name);
  toast((String("搜索引擎：") + kEngines[g_engineIdx].name).c_str());
  g_uiPendingKind = UI_PEND_SEARCH;
}

/* ── 热点新闻：起后台任务拉一个平台 ── */"""
assert src.count(old3) == 1
src = src.replace(old3, new3, 1)

io.open(P, "w", encoding="utf-8", newline="").write(src)
print("engine ok")
