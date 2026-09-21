#include "clock.h"
#include "icons.h"
#include "nav.h"
#include "font_zh.h"
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <time.h>
#include <stdio.h>

namespace {

const float kPi = 3.14159265f;
const int kClockSize = 200;

lv_obj_t* g_timeLab = nullptr;
lv_obj_t* g_dateLab = nullptr;
lv_obj_t* g_clockCanvas = nullptr;
lv_timer_t* g_clockTimer = nullptr;
SwipeState g_swipe;

const char* weekdayCN(int wday) {
  static const char* names[] = {"日", "一", "二", "三", "四", "五", "六"};
  if (wday < 0 || wday > 6) return "-";
  return names[wday];
}

void drawAnalogClock() {
  if (!g_clockCanvas) return;
  int S = kClockSize;
  int cx = S / 2, cy = S / 2;
  int R = S / 2;

  lv_canvas_fill_bg(g_clockCanvas, lv_color_black(), LV_OPA_COVER);

  lv_draw_arc_dsc_t ad;
  lv_draw_arc_dsc_init(&ad);
  ad.color = lv_color_white();
  ad.width = 3;
  ad.rounded = 1;
  ad.opa = LV_OPA_COVER;
  lv_canvas_draw_arc(g_clockCanvas, cx, cy, (lv_coord_t)(R * 0.90f), 0, 360, &ad);

  lv_draw_line_dsc_t tickLd;
  lv_draw_line_dsc_init(&tickLd);
  tickLd.color = lv_color_hex(0x888888);
  tickLd.width = 1;
  tickLd.opa = LV_OPA_COVER;
  for (int i = 0; i < 12; i++) {
    float ang = (-90.0f + i * 30.0f) * kPi / 180.0f;
    float r1 = R * 0.82f, r2 = R * 0.88f;
    lv_point_t pts[2] = {
      {(lv_coord_t)(cx + r1 * cosf(ang)), (lv_coord_t)(cy + r1 * sinf(ang))},
      {(lv_coord_t)(cx + r2 * cosf(ang)), (lv_coord_t)(cy + r2 * sinf(ang))}
    };
    lv_canvas_draw_line(g_clockCanvas, pts, 2, &tickLd);
  }

  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  if (!t) return;
  float sec = t->tm_sec;
  float minF = t->tm_min + sec / 60.0f;
  float hourF = (t->tm_hour % 12) + minF / 60.0f;

  float hourAng = (-90.0f + hourF * 30.0f) * kPi / 180.0f;
  float minAng = (-90.0f + minF * 6.0f) * kPi / 180.0f;
  float secAng = (-90.0f + sec * 6.0f) * kPi / 180.0f;

  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = lv_color_white();
  ld.width = 4;
  ld.round_start = 1;
  ld.round_end = 1;
  ld.opa = LV_OPA_COVER;
  lv_point_t hourSeg[2] = {
    {(lv_coord_t)cx, (lv_coord_t)cy},
    {(lv_coord_t)(cx + R * 0.45f * cosf(hourAng)), (lv_coord_t)(cy + R * 0.45f * sinf(hourAng))}
  };
  lv_canvas_draw_line(g_clockCanvas, hourSeg, 2, &ld);

  ld.width = 3;
  lv_point_t minSeg[2] = {
    {(lv_coord_t)cx, (lv_coord_t)cy},
    {(lv_coord_t)(cx + R * 0.65f * cosf(minAng)), (lv_coord_t)(cy + R * 0.65f * sinf(minAng))}
  };
  lv_canvas_draw_line(g_clockCanvas, minSeg, 2, &ld);

  ld.color = lv_color_hex(0xFF4444);
  ld.width = 2;
  lv_point_t secSeg[2] = {
    {(lv_coord_t)cx, (lv_coord_t)cy},
    {(lv_coord_t)(cx + R * 0.75f * cosf(secAng)), (lv_coord_t)(cy + R * 0.75f * sinf(secAng))}
  };
  lv_canvas_draw_line(g_clockCanvas, secSeg, 2, &ld);

  ad.color = lv_color_white();
  ad.width = 6;
  lv_canvas_draw_arc(g_clockCanvas, cx, cy, (lv_coord_t)(S * 0.03f), 0, 360, &ad);
}

void clockTimer_cb(lv_timer_t* t) {
  (void)t;
  drawAnalogClock();
}

void back_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
}

void swipe_cb(lv_event_t* e) {
  swipe_detect(e, g_swipe, nav_launcher);
}

}  // namespace

void ClockScreen_update() {
  if (!g_timeLab) return;
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  if (!t) return;

  char buf[16];
  strftime(buf, sizeof(buf), "%H:%M", t);
  lv_label_set_text(g_timeLab, buf);

  char date[32];
  snprintf(date, sizeof(date), "%04d-%02d-%02d 周%s", t->tm_year + 1900,
           t->tm_mon + 1, t->tm_mday, weekdayCN(t->tm_wday));
  lv_label_set_text(g_dateLab, date);
}

lv_obj_t* ClockScreen_create() {
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

  lv_obj_t* back = icon_create(scr, Icon::Back, 36);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 14, 14);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(back, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(back, back_event_cb, LV_EVENT_CLICKED, NULL);

  g_clockCanvas = lv_canvas_create(scr);
  void* buf = heap_caps_malloc(kClockSize * kClockSize * 2, MALLOC_CAP_SPIRAM);
  if (buf) {
    lv_canvas_set_buffer(g_clockCanvas, buf, kClockSize, kClockSize, LV_IMG_CF_TRUE_COLOR);
    drawAnalogClock();
  }
  lv_obj_align(g_clockCanvas, LV_ALIGN_TOP_MID, 0, 55);

  g_timeLab = lv_label_create(scr);
  lv_label_set_text(g_timeLab, "--:--");
  lv_obj_set_style_text_color(g_timeLab, lv_color_white(), 0);
  lv_obj_set_style_text_font(g_timeLab, &lv_font_montserrat_48, 0);
  lv_obj_align(g_timeLab, LV_ALIGN_BOTTOM_MID, 0, -70);

  g_dateLab = lv_label_create(scr);
  lv_label_set_text(g_dateLab, "---- -- -- 周-");
  lv_obj_set_style_text_color(g_dateLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(g_dateLab, &font_zh_16, 0);
  lv_obj_align(g_dateLab, LV_ALIGN_BOTTOM_MID, 0, -30);

  g_clockTimer = lv_timer_create(clockTimer_cb, 200, nullptr);

  ClockScreen_update();
  return scr;
}
