# -*- coding: utf-8 -*-
"""热点新闻 UI：
  1) 已验证源的名字旁画金色小星星（选中态文字变黑，星星**仍金色**）
  2) 底部「更新」按钮 + 显示"x 分钟前更新"
  3) 进页面自动拉一次（默认豆瓣），之后每天自动刷一次
"""
import io

P = r"C:/Users/longyaosi/Downloads/C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"
src = io.open(P, encoding="utf-8").read()

# ── 1) 新闻 chip：名字 + 金星 ──────────────────────────────────────────────
old = """  lv_obj_t* npRow = makeRow(g_content, true);
  for (int i = 0; i < kNewsPlatformCount; i++) {
    lv_obj_t* b = makeChipBtn(npRow, kNewsPlatforms[i].name, news_platform_cb,
                              (void*)kNewsPlatforms[i].code);
    lv_obj_set_size(b, 76, 36);
    /* 当前正在看的源 -> 白底黑字（选中态）。
       master 2026-09-25：「正在被选中的热点新闻应该有一个高亮」——
       不然点完根本看不出列表是哪个源的。g_newsCount<=0 表示还没拉过，不高亮。 */
    if (g_newsCount > 0 && strcmp(g_newsPlatform, kNewsPlatforms[i].code) == 0) {
      lv_obj_set_style_bg_color(b, lv_color_white(), 0);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
      lv_obj_set_style_border_color(b, lv_color_white(), 0);
      lv_obj_t* lb = lv_obj_get_child(b, 0);
      if (lb) lv_obj_set_style_text_color(lb, lv_color_black(), 0);
    }
  }"""

new = """  lv_obj_t* npRow = makeRow(g_content, true);
  for (int i = 0; i < kNewsPlatformCount; i++) {
    const NewsPlatform& np = kNewsPlatforms[i];
    lv_obj_t* b = lv_btn_create(npRow);
    lv_obj_set_size(b, 76, 36);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x333333), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x444444), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(b, 2, 0);
    lv_obj_add_event_cb(b, news_platform_cb, LV_EVENT_CLICKED,
                        (void*)np.code);

    lv_obj_t* lb = lv_label_create(b);
    lv_label_set_text(lb, np.name);
    lv_obj_set_style_text_color(lb, lv_color_white(), 0);
    lv_obj_set_style_text_font(lb, &font_zh_16, 0);

    /* 已验证能打开看的源 -> 名字后跟一颗金色小星星（master 2026-09-25）。
       ⚠️ 星星单独一个 label：选中态要把**文字**刷成黑色，
          而星星"选中的时候也是金色"，不能跟着变。 */
    if (np.verified) {
      lv_obj_t* st = lv_label_create(b);
      lv_label_set_text(st, "*");
      lv_obj_set_style_text_color(st, lv_color_hex(0xFFD700), 0);
      lv_obj_set_style_text_font(st, &font_zh_16, 0);
    }

    /* 当前正在看的源 -> 白底黑字（选中态）。
       master 2026-09-25：「正在被选中的热点新闻应该有一个高亮」——
       不然点完根本看不出列表是哪个源的。g_newsCount<=0 表示还没拉过，不高亮。 */
    if (g_newsCount > 0 && strcmp(g_newsPlatform, np.code) == 0) {
      lv_obj_set_style_bg_color(b, lv_color_white(), 0);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
      lv_obj_set_style_border_color(b, lv_color_white(), 0);
      lv_obj_set_style_text_color(lb, lv_color_black(), 0);
    }
  }

  /* ── 更新按钮 + 上次更新时间 ──
     master：「在底部留一个更新按钮，点击之后才会主动更新这个新闻源」。
     默认 30 分钟内不会重复联网，所以要强制刷新就点它。 */
  {
    lv_obj_t* ur = lv_obj_create(g_content);
    lv_obj_set_width(ur, lv_pct(100));
    lv_obj_set_height(ur, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ur, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ur, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(ur, 8, 0);
    lv_obj_clear_flag(ur, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ur, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ur, 0, 0);
    lv_obj_set_style_pad_all(ur, 0, 0);

    lv_obj_t* rb = makeChipBtn(ur, "更新", news_refresh_cb, NULL);
    lv_obj_set_size(rb, 72, 32);

    lv_obj_t* tl = lv_label_create(ur);
    uint32_t age = newsAgeSec(g_newsPlatform);
    if (g_newsCount > 0 && age) {
      char t[40];
      if (age < 60) snprintf(t, sizeof(t), "%s · 刚刚更新", g_newsPlatform);
      else if (age < 3600) snprintf(t, sizeof(t), "%s · %u 分钟前更新",
                                    g_newsPlatform, (unsigned)(age / 60));
      else snprintf(t, sizeof(t), "%s · %u 小时前更新",
                    g_newsPlatform, (unsigned)(age / 3600));
      lv_label_set_text(tl, t);
    } else {
      lv_label_set_text(tl, "每 30 分钟更新一次");
    }
    lv_obj_set_style_text_color(tl, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(tl, &font_zh_16, 0);
  }"""
assert src.count(old) == 1
src = src.replace(old, new, 1)

# ── 2) news_refresh_cb 前向声明 + 实现 ────────────────────────────────────
old2 = """static void news_platform_cb(lv_event_t* e) {"""
new2 = """/* 底部「更新」：强制刷新当前源（跳过 30 分钟节流）。
   ⚠️ 回调里只调 startNews，真正的重建走 tick 通道（老规矩）。 */
static void news_refresh_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  Serial.printf("[Browser] news refresh (force): %s\\n", g_newsPlatform);
  startNews(g_newsPlatform, true);
}

static void news_platform_cb(lv_event_t* e) {"""
assert src.count(old2) == 1
src = src.replace(old2, new2, 1)

# ── 3) 进页面自动拉一次 + 每天自动一次 ────────────────────────────────────
old3 = """  /* ── 热点新闻（news.orz.ai）──"""
new3 = """  /* ── 进页面自动拉一次（master 2026-09-25）──
     规则：首次进（或当天还没自动刷过）就拉默认源 kNewsPlatforms[0]（豆瓣）。
     ⚠️ 判断"当天"要用真实日期（NTP 校准后），不能用 millis()
        —— millis 只有开机时长，重启就白记了。拿不到时间就退化为每次进都拉。 */
  if (g_newsCount <= 0) {
    time_t now = 0;
    struct tm tmv;
    time(&now);
    uint32_t day = 0;
    if (now > 1000000000 && localtime_r(&now, &tmv)) {
      day = (uint32_t)((tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 +
                       tmv.tm_mday);
    }
    if (day == 0 || day != g_newsAutoDay) {
      g_newsAutoDay = day;
      Serial.printf("[Browser] news auto-fetch day=%u src=%s\\n", day,
                    kNewsPlatforms[0].code);
      startNews(kNewsPlatforms[0].code);
    }
  }

  /* ── 热点新闻（news.orz.ai）──"""
assert src.count(old3) == 1
src = src.replace(old3, new3, 1)

io.open(P, "w", encoding="utf-8", newline="").write(src)
print("news ui ok")
