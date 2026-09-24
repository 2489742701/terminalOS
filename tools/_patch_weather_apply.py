# -*- coding: utf-8 -*-
"""applyResult 里驱动新视觉（逐时冷暖色 / 7 日温度条），以及进屏先读 SD 缓存。"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\weather_screen.cpp"
src = io.open(P, encoding="utf-8").read()

# ── 1) 逐时：冷暖色 + 记录 lo/hi ──────────────────────────────────────────
old = """  /* ── 今日逐时 ── */
  for (int i = 0; i < HOUR_N; i++) {
    if (!g_hourLab[i]) continue;
    if (i >= r.hourCount) { lv_label_set_text(g_hourLab[i], ""); continue; }
    const HourFc& h = r.hours[i];
    snprintf(buf, sizeof(buf), "%s\\n%.0f°\\n%.0f%%", h.hh, h.temp, h.pop);
    lv_label_set_text(g_hourLab[i], buf);
  }"""
new = """  /* ── 今日逐时：先算这 24 小时的温度范围，再按冷暖给每格上底色 ── */
  float hLo = 999, hHi = -999;
  for (int i = 0; i < r.hourCount; i++) {
    if (r.hours[i].temp < hLo) hLo = r.hours[i].temp;
    if (r.hours[i].temp > hHi) hHi = r.hours[i].temp;
  }
  for (int i = 0; i < HOUR_N; i++) {
    if (!g_hourLab[i]) continue;
    if (i >= r.hourCount) {
      lv_label_set_text(g_hourLab[i], "");
      if (g_hourCard[i]) lv_obj_set_style_bg_opa(g_hourCard[i], LV_OPA_TRANSP, 0);
      continue;
    }
    const HourFc& h = r.hours[i];
    snprintf(buf, sizeof(buf), "%s\\n%.0f°\\n%.0f%%", h.hh, h.temp, h.pop);
    lv_label_set_text(g_hourLab[i], buf);
    if (g_hourCard[i]) {
      lv_obj_set_style_bg_color(g_hourCard[i],
                                lv_color_hex(tempColor(h.temp, hLo, hHi)), 0);
      lv_obj_set_style_bg_opa(g_hourCard[i], LV_OPA_COVER, 0);
    }
  }"""
assert src.count(old) == 1
src = src.replace(old, new, 1)

# ── 2) 7 日温度条 ─────────────────────────────────────────────────────────
old2 = """    if (g_dayTmp[i]) {
      snprintf(buf, sizeof(buf), "%.0f° / %.0f°", d.tmax, d.tmin);
      lv_label_set_text(g_dayTmp[i], buf);
    }"""
new2 = """    if (g_dayTmp[i]) {
      snprintf(buf, sizeof(buf), "%.0f° / %.0f°", d.tmax, d.tmin);
      lv_label_set_text(g_dayTmp[i], buf);
    }
    /* 温度条：按**本周**的 min~max 归一化，条里的亮块是当天的 min~max */
    if (g_dayBar[i] && g_dayBarBg[i] && wHi > wLo) {
      const int W = 110;
      float span = wHi - wLo;
      int x = (int)((d.tmin - wLo) / span * (float)W);
      int w = (int)((d.tmax - d.tmin) / span * (float)W);
      if (x < 0) x = 0;
      if (x > W - 4) x = W - 4;
      if (w < 6) w = 6;
      if (x + w > W) w = W - x;
      lv_obj_set_pos(g_dayBar[i], x, 0);
      lv_obj_set_size(g_dayBar[i], w, 8);
      /* 当天越热，条越偏暖色 */
      lv_obj_set_style_bg_color(g_dayBar[i],
                                lv_color_hex(tempColor(d.tmax, wLo, wHi)), 0);
    }"""
assert src.count(old2) == 1
src = src.replace(old2, new2, 1)

# 7 日循环前先算本周范围
old3 = """  /* ── 近 7 日 ── */
  for (int i = 0; i < DAY_N; i++) {"""
new3 = """  /* ── 近 7 日 ── */
  float wLo = 999, wHi = -999;
  for (int i = 0; i < r.dayCount; i++) {
    if (r.days[i].tmin < wLo) wLo = r.days[i].tmin;
    if (r.days[i].tmax > wHi) wHi = r.days[i].tmax;
  }
  for (int i = 0; i < DAY_N; i++) {"""
assert src.count(old3) == 1
src = src.replace(old3, new3, 1)

# ── 3) 进屏先读 SD 缓存 ───────────────────────────────────────────────────
old4 = """  /* 进屏就自动拉一次（后台任务，不卡 UI）。有上次的结果先贴上，屏幕不是空的。 */
  if (g_haveRes) applyResult(g_res);
  weatherStart(false);"""
new4 = """  /* 进屏就自动拉一次（后台任务，不卡 UI）。有上次的结果先贴上，屏幕不是空的。 */
  if (g_haveRes) applyResult(g_res);
  else {
    /* 内存里没有（刚开机）-> 试试 SD 上的 JSON 缓存。
       有就当场解析贴上，屏幕上立刻有内容，不用干等 1~2 秒的网络。 */
    String cached;
    if (wxCacheLoad(cached)) {
      WeatherResult r;
      memset(&r, 0, sizeof(r));
      r.windDir = -1;
      if (parseWeatherJson(cached, r)) {
        g_res = r;
        g_haveRes = true;
        applyResult(r);
        if (g_statusLab) lv_label_set_text(g_statusLab, "来自SD缓存，正在更新");
      }
    }
  }
  weatherStart(false);"""
assert src.count(old4) == 1
src = src.replace(old4, new4, 1)

# 需要把 fetchOnce 里的解析部分抽成 parseWeatherJson(String, WeatherResult&)
# 简单做法：让 fetchOnce 复用 —— 这里改成声明一个解析函数，fetchOnce 调它
old5 = """static bool wxCacheSave(const String& body);
static bool wxCacheLoad(String& out);"""
new5 = """static bool wxCacheSave(const String& body);
static bool wxCacheLoad(String& out);
/* 解析 open-meteo 的响应体。抽出来是为了 SD 缓存也能复用同一套解析。 */
static bool parseWeatherJson(const String& body, WeatherResult& r);"""
assert src.count(old5) == 1
src = src.replace(old5, new5, 1)

io.open(P, "w", encoding="utf-8", newline="").write(src)
print("applyResult ok")
