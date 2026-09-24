# -*- coding: utf-8 -*-
"""天气 UI 美化（master：「天气预报 UI 可以搞得更好看一点，现在有点敷衍」）

  1) 上半屏重排：左上角放 Icon::Weather，城市/温度/描述右对齐成一列
  2) 当前实况：格子改成圆角卡片（微底色 + 边框）
  3) 未来 24 小时：每格背景按温度冷暖上色（冷蓝 -> 暖红）
  4) 近 7 日：每行加一根温度条（本周最低~最高归一化，条内是当天区间）
  5) 进屏先读 SD 缓存，屏幕立刻有内容，不等网络
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\weather_screen.cpp"
src = io.open(P, encoding="utf-8").read()

# ── 1) 新全局：图标 / 小时卡片 / 温度条 ───────────────────────────────────
old = """/* ── 下半屏：今日逐时（横向滚） ── */
static lv_obj_t* g_hourLab[HOUR_N] = {nullptr};"""
new = """/* ── 下半屏：今日逐时 ── */
static lv_obj_t* g_hourLab[HOUR_N] = {nullptr};
static lv_obj_t* g_hourCard[HOUR_N] = {nullptr};   /* 卡片本体：按温度上底色 */

/* ── 下半屏：近 7 日的温度条 ── */
static lv_obj_t* g_dayBarBg[DAY_N] = {nullptr};    /* 底槽（本周范围） */
static lv_obj_t* g_dayBar[DAY_N] = {nullptr};      /* 当天 min~max 区间 */

/* 上半屏的天气图标 */
static lv_obj_t* g_wxIcon = nullptr;"""
assert src.count(old) == 1
src = src.replace(old, new, 1)

# ── 2) 温度 -> 冷暖色 ─────────────────────────────────────────────────────
old2 = """/* ── UI 小工具 ── */"""
new2 = """/* 温度 -> 冷暖色（低=冷蓝 0x24405E，高=暖红 0x6E2F2A）。
   用在逐时格子的底色上：一眼看出哪几个小时最热。 */
static uint32_t tempColor(float t, float lo, float hi) {
  float k = (hi > lo + 0.01f) ? (t - lo) / (hi - lo) : 0.5f;
  if (k < 0) k = 0;
  if (k > 1) k = 1;
  uint8_t r = 0x24 + (uint8_t)((0x6E - 0x24) * k);
  uint8_t g = 0x40 + (uint8_t)((0x2F - 0x40) * k);
  uint8_t b = 0x5E + (uint8_t)((0x2A - 0x5E) * k);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* ── UI 小工具 ── */"""
assert src.count(old2) == 1
src = src.replace(old2, new2, 1)

# ── 3) 实况格子卡片化 ─────────────────────────────────────────────────────
old3 = """static lv_obj_t* mkCell(lv_obj_t* parent, const char* name) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_set_size(box, 214, 34);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);"""
new3 = """static lv_obj_t* mkCell(lv_obj_t* parent, const char* name) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_set_size(box, 214, 38);
  /* 卡片化：微微的底色 + 细边框 + 圆角。以前是纯透明，一屏灰字确实敷衍。 */
  lv_obj_set_style_bg_color(box, lv_color_hex(0x151515), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0x2A2A2A), 0);
  lv_obj_set_style_radius(box, 8, 0);
  lv_obj_set_style_pad_left(box, 10, 0);
  lv_obj_set_style_pad_right(box, 8, 0);
  lv_obj_set_style_pad_top(box, 0, 0);
  lv_obj_set_style_pad_bottom(box, 0, 0);"""
assert src.count(old3) == 1
src = src.replace(old3, new3, 1)

# ── 4) 上半屏重排 ─────────────────────────────────────────────────────────
old4 = """  /* ── 上半屏（固定）── */
  g_locLab = mkLabel(scr, &font_zh_16, 0x888888, GeoIP::city());
  lv_obj_align(g_locLab, LV_ALIGN_TOP_MID, 0, 34);

  g_updLab = mkLabel(scr, &lv_font_montserrat_14, 0x666666, "");
  lv_obj_align(g_updLab, LV_ALIGN_TOP_RIGHT, -12, 36);

  g_tempLab = mkLabel(scr, &lv_font_montserrat_48, 0xFFFFFF, "--°C");
  lv_obj_align(g_tempLab, LV_ALIGN_TOP_MID, 0, 56);

  g_descLab = mkLabel(scr, &font_zh_24, 0xCCCCCC, "--");
  lv_obj_align(g_descLab, LV_ALIGN_TOP_MID, 0, 112);

  g_subLab = mkLabel(scr, &font_zh_16, 0x888888, "");
  lv_obj_align(g_subLab, LV_ALIGN_TOP_MID, 0, 146);"""
new4 = """  /* ── 上半屏（固定）──
     重排：左边一个天气图标，右边城市 / 大温度 / 描述竖着排成一列，
     不再全都居中叠在一起（2026-09-25 改，原来确实有点敷衍）。 */
  g_wxIcon = icon_create(scr, Icon::Weather, 60);
  if (g_wxIcon) lv_obj_align(g_wxIcon, LV_ALIGN_TOP_LEFT, 22, 44);

  g_locLab = mkLabel(scr, &font_zh_16, 0x888888, GeoIP::city());
  lv_obj_align(g_locLab, LV_ALIGN_TOP_LEFT, 96, 40);

  g_updLab = mkLabel(scr, &lv_font_montserrat_14, 0x666666, "");
  lv_obj_align(g_updLab, LV_ALIGN_TOP_RIGHT, -12, 42);

  g_tempLab = mkLabel(scr, &lv_font_montserrat_48, 0xFFFFFF, "--°C");
  lv_obj_align(g_tempLab, LV_ALIGN_TOP_LEFT, 94, 60);

  g_descLab = mkLabel(scr, &font_zh_24, 0xCCCCCC, "--");
  lv_obj_align(g_descLab, LV_ALIGN_TOP_LEFT, 96, 118);

  g_subLab = mkLabel(scr, &font_zh_16, 0x888888, "");
  lv_obj_align(g_subLab, LV_ALIGN_TOP_LEFT, 96, 148);"""
assert src.count(old4) == 1
src = src.replace(old4, new4, 1)

# ── 5) 逐时卡片指针 ───────────────────────────────────────────────────────
old5 = """      lv_obj_t* card = lv_obj_create(hr);
      lv_obj_set_size(card, 52, 68);
      lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(card, 0, 0);"""
new5 = """      lv_obj_t* card = lv_obj_create(hr);
      lv_obj_set_size(card, 52, 68);
      g_hourCard[i] = card;
      lv_obj_set_style_bg_color(card, lv_color_hex(0x24405E), 0);
      lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(card, 6, 0);
      lv_obj_set_style_border_width(card, 0, 0);"""
assert src.count(old5) == 1
src = src.replace(old5, new5, 1)

# ── 6) 7 日行加温度条 ─────────────────────────────────────────────────────
old6 = """    g_dayDate[i] = mkLabel(line1, &font_zh_16, 0xDDDDDD, "");
    lv_obj_set_width(g_dayDate[i], 116);
    g_dayDesc[i] = mkLabel(line1, &font_zh_16, 0xAAAAAA, "");
    lv_obj_set_flex_grow(g_dayDesc[i], 1);
    g_dayTmp[i] = mkLabel(line1, &lv_font_montserrat_16, 0xFFFFFF, "");"""
new6 = """    g_dayDate[i] = mkLabel(line1, &font_zh_16, 0xDDDDDD, "");
    lv_obj_set_width(g_dayDate[i], 96);
    g_dayDesc[i] = mkLabel(line1, &font_zh_16, 0xAAAAAA, "");
    lv_obj_set_flex_grow(g_dayDesc[i], 1);

    /* 温度条：底槽 = 本周最低~最高的整段，里面的亮块 = 当天 min~max。
       一眼能看出哪天最热 —— 比一行干巴巴的数字强多了。
       ⚠️ 底槽**不能**是 flex 容器：区间块要用绝对坐标定位。 */
    g_dayBarBg[i] = lv_obj_create(line1);
    lv_obj_set_size(g_dayBarBg[i], 110, 8);
    lv_obj_set_style_bg_color(g_dayBarBg[i], lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(g_dayBarBg[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dayBarBg[i], 0, 0);
    lv_obj_set_style_radius(g_dayBarBg[i], 4, 0);
    lv_obj_clear_flag(g_dayBarBg[i], LV_OBJ_FLAG_SCROLLABLE);
    g_dayBar[i] = lv_obj_create(g_dayBarBg[i]);
    lv_obj_set_size(g_dayBar[i], 20, 8);
    lv_obj_set_pos(g_dayBar[i], 0, 0);
    lv_obj_set_style_bg_color(g_dayBar[i], lv_color_hex(0x5B9BD5), 0);
    lv_obj_set_style_bg_opa(g_dayBar[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dayBar[i], 0, 0);
    lv_obj_set_style_radius(g_dayBar[i], 4, 0);
    lv_obj_clear_flag(g_dayBar[i], LV_OBJ_FLAG_SCROLLABLE);

    g_dayTmp[i] = mkLabel(line1, &lv_font_montserrat_16, 0xFFFFFF, "");
    lv_obj_set_width(g_dayTmp[i], 74);
    lv_obj_set_style_text_align(g_dayTmp[i], LV_TEXT_ALIGN_RIGHT, 0);"""
assert src.count(old6) == 1
src = src.replace(old6, new6, 1)

io.open(P, "w", encoding="utf-8", newline="").write(src)
print("ui skeleton ok")
