# -*- coding: utf-8 -*-
"""搜索引擎改回**只有必应**。

master 问「36氪是不是就是那个有搜索引擎的」—— 不是：
    36氪(36kr) = 科技媒体，是**新闻源**，没有搜索。
    360(www.so.com) = 搜索引擎，跟 36氪不是一家，而且也不是新闻源（API 0 条）。
他的判断是"36 开头又能搜的不存在就退回只有 Bing" -> 条件成立，执行。
⚠️ 想加回来很容易：往 kEngines 里加一行即可，代码结构留着。
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"
s = io.open(P, encoding="utf-8").read()

# 1) 引擎表只剩必应
old = """/* 搜索引擎（master 2026-09-25：必应之外再加 360）
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
"""
new = """/* 搜索引擎：目前**只有必应**（master 2026-09-25 拍板退回单一引擎）。
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
assert s.count(old) == 1
s = s.replace(old, new, 1)

# 2) 删掉切换回调（只有一个引擎，点了没意义）
old2 = """/* 切换搜索引擎：切完当场重画搜索首页（行的重建走 UI_PEND 通道，别在回调里 clean） */
static void engine_switch_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_engineIdx = (g_engineIdx + 1) % kEngineCount;
  Serial.printf("[Search] engine -> %s\\n", kEngines[g_engineIdx].name);
  toast((String("搜索引擎：") + kEngines[g_engineIdx].name).c_str());
  g_uiPendingKind = UI_PEND_SEARCH;
}

"""
assert s.count(old2) == 1
s = s.replace(old2, "", 1)

# 3) 删掉首页那个切换 chip
old3 = """
  /* 搜索引擎切换：点一下换下一个（必应 -> 360 -> 百度 -> …），
     名字旁显示当前引擎，选中态白底黑字。 */
  lv_obj_t* eb = makeChipBtn(row, kEngines[g_engineIdx].name,
                             engine_switch_cb, NULL);
  lv_obj_set_size(eb, 68, 36);
  lv_obj_set_style_bg_color(eb, lv_color_hex(0x2a2a2a), 0);
  lv_obj_set_style_border_color(eb, lv_color_hex(0xFFD700), 0);"""
assert s.count(old3) == 1
s = s.replace(old3, "", 1)

io.open(P, "w", encoding="utf-8", newline="").write(s)
print("engine -> bing only")
