#include "calendar_screen.h"
#include "nav.h"
#include "status_bar.h"
#include "font_zh.h"
#include <Arduino.h>
#include <stdio.h>
#include <time.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * 轻量月历。三条老规矩照旧：
 *   · 屏销毁时把全局指针清干净（tick 里写悬空指针会崩）
 *   · 滑动退出左右通用，竖滑留给"翻月"
 *   · 不建任务、不建常驻定时器
 * ═══════════════════════════════════════════════════════════════════════════ */

static lv_obj_t* g_titleLab = nullptr;
static lv_obj_t* g_dayLab[42] = {nullptr};
static lv_obj_t* g_dayBox[42] = {nullptr};
static lv_obj_t* g_selLab = nullptr;
static lv_obj_t* g_prevBtn = nullptr;
static lv_obj_t* g_nextBtn = nullptr;

static int g_year = 2026, g_month = 9;   /* 正在显示的年月 */
static int g_todayY = 0, g_todayM = 0, g_todayD = 0;
static int g_selDay = 0;

static const char* kWeekHead[7] = {"日", "一", "二", "三", "四", "五", "六"};
static const char* kWeekName[7] = {"周日", "周一", "周二", "周三", "周四",
                                   "周五", "周六"};

/* Zeller：0=周日。跟天气页那份实现一致（那边用来标"周五"） */
static int weekdayOf(int y, int m, int d) {
  if (m < 3) { m += 12; y--; }
  int K = y % 100, J = y / 100;
  int h = (d + 13 * (m + 1) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
  return (h + 6) % 7;
}

static int daysInMonth(int y, int m) {
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) return 29;
  if (m < 1 || m > 12) return 30;
  return kDays[m - 1];
}

/* ── 手势 ── */
static void cal_delete_cb(lv_event_t* e);

static SwipeState g_swipe;
static void swipe_cb(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  /* 左右滑 = 退出（统一手势）。竖滑关掉：这个屏不滚，留着竖滑也不会误触，
     但翻月是用按钮做的，不需要手势 —— 保持一致就一并关掉。 */
  swipe_back_to_any(e, g_swipe, nav_launcher, false);
  (void)c;
}

static void refresh();

static void prev_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_month--;
  if (g_month < 1) { g_month = 12; g_year--; }
  g_selDay = 0;
  refresh();
}
static void next_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_month++;
  if (g_month > 12) { g_month = 1; g_year++; }
  g_selDay = 0;
  refresh();
}

/* 点某一天：底部显示星期。⚠️ 回调里只改标签，不重建 */
static void day_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  int d = (int)(intptr_t)lv_event_get_user_data(e);
  if (d <= 0) return;
  g_selDay = d;
  char t[48];
  snprintf(t, sizeof(t), "%d年%d月%d日 %s", g_year, g_month, d,
           kWeekName[weekdayOf(g_year, g_month, d)]);
  if (g_selLab) lv_label_set_text(g_selLab, t);
  refresh();
}

static lv_obj_t* mkLabel(lv_obj_t* parent, const lv_font_t* f, uint32_t color,
                         const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text ? text : "");
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(l, f, 0);
  return l;
}

/* ── 重画整月 ── */
static void refresh() {
  char t[32];
  snprintf(t, sizeof(t), "%d年%d月", g_year, g_month);
  if (g_titleLab) lv_label_set_text(g_titleLab, t);

  int first = weekdayOf(g_year, g_month, 1);   /* 1 号是周几 */
  int ndays = daysInMonth(g_year, g_month);

  for (int i = 0; i < 42; i++) {
    lv_obj_t* box = g_dayBox[i];
    lv_obj_t* lab = g_dayLab[i];
    if (!box || !lab) continue;

    int d = i - first + 1;
    bool inMonth = (d >= 1 && d <= ndays);
    bool isToday = inMonth && g_year == g_todayY && g_month == g_todayM &&
                   d == g_todayD;
    bool isSel = inMonth && d == g_selDay;

    if (!inMonth) {
      lv_label_set_text(lab, "");
      lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(box, 0, 0);
      continue;
    }

    snprintf(t, sizeof(t), "%d", d);
    lv_label_set_text(lab, t);

    /* 周日/周六 淡一点；今天金色；选中的加边框 */
    int w = weekdayOf(g_year, g_month, d);
    uint32_t col = (w == 0 || w == 6) ? 0x888888 : 0xDDDDDD;
    if (isToday) col = 0xFFD700;
    lv_obj_set_style_text_color(lab, lv_color_hex(col), 0);

    if (isToday) {
      lv_obj_set_style_border_width(box, 2, 0);
      lv_obj_set_style_border_color(box, lv_color_hex(0xFFD700), 0);
    } else if (isSel) {
      lv_obj_set_style_border_width(box, 1, 0);
      lv_obj_set_style_border_color(box, lv_color_hex(0x666666), 0);
    } else {
      lv_obj_set_style_border_width(box, 0, 0);
    }
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  }

  if (!g_selLab) return;
  if (g_selDay <= 0) {
    if (g_todayD) {
      snprintf(t, sizeof(t), "今天 %d年%d月%d日 %s", g_todayY, g_todayM,
               g_todayD, kWeekName[weekdayOf(g_todayY, g_todayM, g_todayD)]);
      lv_label_set_text(g_selLab, t);
    }
  }
}

lv_obj_t* CalendarScreen_create() {
  /* 当前日期（NTP 校准后就是真实时间；没校准就按编译期默认 2026-09） */
  time_t now = 0;
  struct tm tmv;
  time(&now);
  if (now > 1000000000 && localtime_r(&now, &tmv)) {
    g_todayY = tmv.tm_year + 1900;
    g_todayM = tmv.tm_mon + 1;
    g_todayD = tmv.tm_mday;
    g_year = g_todayY;
    g_month = g_todayM;
  } else {
    g_year = 2026;
    g_month = 9;
  }
  g_selDay = g_todayD;

  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_RELEASED, NULL);

  StatusBar_create(scr, "日历");
  lv_obj_add_event_cb(scr, cal_delete_cb, LV_EVENT_DELETE, NULL);

  /* ── 顶部：翻月 ── */
  g_prevBtn = lv_btn_create(scr);
  lv_obj_set_size(g_prevBtn, 56, 40);
  lv_obj_align(g_prevBtn, LV_ALIGN_TOP_LEFT, 12, 44);
  lv_obj_set_style_bg_opa(g_prevBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(g_prevBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(g_prevBtn, lv_color_hex(0x222222), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(g_prevBtn, 0, 0);
  lv_obj_add_event_cb(g_prevBtn, prev_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* pl = mkLabel(g_prevBtn, &font_zh_16, 0xCCCCCC, "<");
  lv_obj_center(pl);

  g_nextBtn = lv_btn_create(scr);
  lv_obj_set_size(g_nextBtn, 56, 40);
  lv_obj_align(g_nextBtn, LV_ALIGN_TOP_RIGHT, -12, 44);
  lv_obj_set_style_bg_opa(g_nextBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(g_nextBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(g_nextBtn, lv_color_hex(0x222222), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(g_nextBtn, 0, 0);
  lv_obj_add_event_cb(g_nextBtn, next_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* nl = mkLabel(g_nextBtn, &font_zh_16, 0xCCCCCC, ">");
  lv_obj_center(nl);

  g_titleLab = mkLabel(scr, &font_zh_24, 0xFFFFFF, "");
  lv_obj_align(g_titleLab, LV_ALIGN_TOP_MID, 0, 50);

  /* ── 星期表头 ── */
  {
    lv_obj_t* row = lv_obj_create(scr);
    lv_obj_set_size(row, 456, 26);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, 12, 88);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 7; i++) {
      lv_obj_t* c = lv_obj_create(row);
      lv_obj_set_size(c, 64, 26);
      lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(c, 0, 0);
      lv_obj_set_style_pad_all(c, 0, 0);
      lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t* l = mkLabel(c, &font_zh_16,
                            (i == 0 || i == 6) ? 0x777777 : 0x999999,
                            kWeekHead[i]);
      lv_obj_center(l);
    }
  }

  /* ── 6 行 × 7 列日期格 ── */
  {
    lv_obj_t* grid = lv_obj_create(scr);
    lv_obj_set_size(grid, 456, 300);
    lv_obj_align(grid, LV_ALIGN_TOP_LEFT, 12, 116);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, 0, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 42; i++) {
      lv_obj_t* box = lv_btn_create(grid);
      lv_obj_set_size(box, 64, 50);
      lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
      lv_obj_set_style_bg_color(box, lv_color_hex(0x222222), LV_STATE_PRESSED);
      lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_STATE_PRESSED);
      lv_obj_set_style_border_width(box, 0, 0);
      lv_obj_set_style_radius(box, 8, 0);
      lv_obj_set_style_pad_all(box, 0, 0);
      lv_obj_add_event_cb(box, day_cb, LV_EVENT_CLICKED,
                          (void*)(intptr_t)(i + 1));
      g_dayBox[i] = box;
      g_dayLab[i] = mkLabel(box, &lv_font_montserrat_16, 0xDDDDDD, "");
      lv_obj_center(g_dayLab[i]);
    }
  }

  /* ── 底部：选中/今天的说明 ── */
  g_selLab = mkLabel(scr, &font_zh_16, 0x888888, "");
  lv_obj_align(g_selLab, LV_ALIGN_BOTTOM_MID, 0, -20);

  refresh();
  Serial.printf("[Calendar] %d-%d (today %d-%d-%d)\n", g_year, g_month,
                g_todayY, g_todayM, g_todayD);
  return scr;
}

void CalendarScreen_tick() {
  /* 没有后台数据要贴。留着是为了跟别的应用保持同一套接口。 */
}

/* 屏销毁：指针全清，否则 tick / 回调写一次就崩（老规矩） */
static void cal_delete_cb(lv_event_t* e) {
  (void)e;
  g_titleLab = nullptr;
  g_selLab = nullptr;
  g_prevBtn = nullptr;
  g_nextBtn = nullptr;
  for (int i = 0; i < 42; i++) {
    g_dayLab[i] = nullptr;
    g_dayBox[i] = nullptr;
  }
}
