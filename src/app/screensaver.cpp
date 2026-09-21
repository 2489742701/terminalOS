#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_heap_caps.h>
#include <math.h>
#include "screensaver.h"
#include "font_zh.h"
#include "../hal/display.h"
#include "../hal/touch.h"
#include "../config/pins.h"

// ---- 可调参数 ----
static const unsigned long ACTIVE_TIMEOUT_MS   = 30000;   // 30s 无操作 -> DIM
static const unsigned long DIM_TIMEOUT_MS      = 180000;  // 3min 在 DIM -> OFF
static const uint8_t       DIM_BACKLIGHT_PCT   = 15;      // DIM 背光亮度(%)


// ---- 静态成员 ----
ScreenSaver::State   ScreenSaver::state        = ScreenSaver::ACTIVE;
lv_obj_t*            ScreenSaver::mainScr      = nullptr;
lv_obj_t*            ScreenSaver::returnScr    = nullptr;
lv_obj_t*            ScreenSaver::dimScr       = nullptr;
lv_obj_t*            ScreenSaver::clockLabel   = nullptr;
lv_obj_t*            ScreenSaver::dateLabel     = nullptr;
lv_obj_t*            ScreenSaver::clockCanvas  = nullptr;
unsigned long        ScreenSaver::lastActivityMs = 0;
unsigned long        ScreenSaver::lastClockMs    = 0;
unsigned long        ScreenSaver::lastTapMs      = 0;
int                  ScreenSaver::pressX       = 0;
int                  ScreenSaver::pressY       = 0;
bool                 ScreenSaver::unlocked     = false;

void ScreenSaver::init(lv_obj_t* mainScreen) {
  mainScr = mainScreen;
  initSoftClock();
  buildDimScreen();
  lastActivityMs = millis();
  lastClockMs = 0;
  state = ACTIVE;
}

void ScreenSaver::buildDimScreen() {
  const int kClockSize = 160;
  dimScr = lv_obj_create(nullptr);
  lv_obj_set_size(dimScr, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_style_bg_color(dimScr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(dimScr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(dimScr, 0, 0);
  lv_obj_set_style_pad_all(dimScr, 0, 0);
  lv_obj_clear_flag(dimScr, LV_OBJ_FLAG_SCROLLABLE);

  clockCanvas = lv_canvas_create(dimScr);
  void* cbuf = heap_caps_malloc(kClockSize * kClockSize * 2, MALLOC_CAP_SPIRAM);
  if (cbuf) {
    lv_canvas_set_buffer(clockCanvas, cbuf, kClockSize, kClockSize, LV_IMG_CF_TRUE_COLOR);
  }
  lv_obj_align(clockCanvas, LV_ALIGN_CENTER, 0, -90);

  clockLabel = lv_label_create(dimScr);
  lv_label_set_text(clockLabel, "--:--");
  lv_obj_set_style_text_color(clockLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(clockLabel, &lv_font_montserrat_24, 0);
  lv_obj_align(clockLabel, LV_ALIGN_CENTER, 0, 40);

  dateLabel = lv_label_create(dimScr);
  lv_label_set_text(dateLabel, "");
  lv_obj_set_style_text_color(dateLabel, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_18, 0);
  lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, 80);

  // 全屏手势层：任意方向滑动解锁
  lv_obj_t* gesture = lv_obj_create(dimScr);
  lv_obj_set_size(gesture, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(gesture, 0, 0);
  lv_obj_set_style_bg_opa(gesture, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(gesture, 0, 0);
  lv_obj_set_style_pad_all(gesture, 0, 0);
  lv_obj_clear_flag(gesture, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(gesture, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(gesture, gesture_event_cb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(gesture, gesture_event_cb, LV_EVENT_PRESSING, nullptr);

  // 提示文字
  lv_obj_t* hint = lv_label_create(dimScr);
  lv_label_set_text(hint, "滑动解锁");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
  lv_obj_set_style_text_font(hint, &font_zh_16, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -60);
}

void ScreenSaver::gesture_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    pressX = p.x; pressY = p.y; unlocked = false;

    unsigned long now = millis();
    if (now - lastTapMs < 350) {
      enterOff();
      lastTapMs = 0;
    } else {
      lastTapMs = now;
    }
  } else if (code == LV_EVENT_PRESSING && !unlocked) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    if (abs(p.x - pressX) > 40 || abs(p.y - pressY) > 40) {
      unlocked = true;
      unlock();
    }
  }
}

void ScreenSaver::enterActive() {
  state = ACTIVE;
  Display::setBacklight(true);
  lv_obj_t* back = returnScr ? returnScr : mainScr;
  returnScr = nullptr;
  if (back) lv_scr_load(back);
  lastActivityMs = millis();
}

void ScreenSaver::enterDim(bool captureReturnScr) {
  state = DIM;
  if (captureReturnScr) returnScr = lv_scr_act();
  Display::setBacklightLevel(DIM_BACKLIGHT_PCT);
  updateClock();
  if (dimScr) lv_scr_load(dimScr);
  lastActivityMs = millis();
}

void ScreenSaver::enterOff() {
  state = OFF;
  Display::setBacklight(false);  // 关背光，屏幕全黑（最省电）
}

void ScreenSaver::unlock() {
  enterActive();
}

void ScreenSaver::sleepNow() {
  enterDim(true);
}

void ScreenSaver::notifyActivity() {
  lastActivityMs = millis();
}

void ScreenSaver::tick() {
  unsigned long now = millis();
  if (state == ACTIVE) {
    if (now - lastActivityMs > ACTIVE_TIMEOUT_MS) enterDim(true);
  } else if (state == DIM) {
    if (now - lastActivityMs > DIM_TIMEOUT_MS) enterOff();
    if (now - lastClockMs > 1000) { updateClock(); lastClockMs = now; }
  } else if (state == OFF) {
    int x = 0, y = 0;
    if (Touch::touched(x, y)) {
      enterDim(false);
    } else {
      esp_sleep_enable_timer_wakeup(200000);
      esp_light_sleep_start();
    }
  }
}

void ScreenSaver::drawAnalogClock() {
  if (!clockCanvas) return;
  const float kPi = 3.14159265f;
  const int S = 160;
  int cx = S / 2, cy = S / 2;
  int R = S / 2;

  lv_canvas_fill_bg(clockCanvas, lv_color_black(), LV_OPA_COVER);

  lv_draw_arc_dsc_t ad;
  lv_draw_arc_dsc_init(&ad);
  ad.color = lv_color_white();
  ad.width = 2;
  ad.rounded = 1;
  ad.opa = LV_OPA_COVER;
  lv_canvas_draw_arc(clockCanvas, cx, cy, (lv_coord_t)(R * 0.90f), 0, 360, &ad);

  lv_draw_line_dsc_t tickLd;
  lv_draw_line_dsc_init(&tickLd);
  tickLd.color = lv_color_hex(0x666666);
  tickLd.width = 1;
  tickLd.opa = LV_OPA_COVER;
  for (int i = 0; i < 12; i++) {
    float ang = (-90.0f + i * 30.0f) * kPi / 180.0f;
    float r1 = R * 0.80f, r2 = R * 0.87f;
    lv_point_t pts[2] = {
      {(lv_coord_t)(cx + r1 * cosf(ang)), (lv_coord_t)(cy + r1 * sinf(ang))},
      {(lv_coord_t)(cx + r2 * cosf(ang)), (lv_coord_t)(cy + r2 * sinf(ang))}
    };
    lv_canvas_draw_line(clockCanvas, pts, 2, &tickLd);
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
  ld.width = 3;
  ld.round_start = 1;
  ld.round_end = 1;
  ld.opa = LV_OPA_COVER;
  lv_point_t hourSeg[2] = {
    {(lv_coord_t)cx, (lv_coord_t)cy},
    {(lv_coord_t)(cx + R * 0.45f * cosf(hourAng)), (lv_coord_t)(cy + R * 0.45f * sinf(hourAng))}
  };
  lv_canvas_draw_line(clockCanvas, hourSeg, 2, &ld);

  ld.width = 2;
  lv_point_t minSeg[2] = {
    {(lv_coord_t)cx, (lv_coord_t)cy},
    {(lv_coord_t)(cx + R * 0.65f * cosf(minAng)), (lv_coord_t)(cy + R * 0.65f * sinf(minAng))}
  };
  lv_canvas_draw_line(clockCanvas, minSeg, 2, &ld);

  ld.color = lv_color_hex(0xFF4444);
  ld.width = 1;
  lv_point_t secSeg[2] = {
    {(lv_coord_t)cx, (lv_coord_t)cy},
    {(lv_coord_t)(cx + R * 0.75f * cosf(secAng)), (lv_coord_t)(cy + R * 0.75f * sinf(secAng))}
  };
  lv_canvas_draw_line(clockCanvas, secSeg, 2, &ld);

  ad.color = lv_color_white();
  ad.width = 4;
  lv_canvas_draw_arc(clockCanvas, cx, cy, (lv_coord_t)(S * 0.03f), 0, 360, &ad);
}

void ScreenSaver::updateClock() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  if (!t || !clockLabel) return;
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", t);
  lv_label_set_text(clockLabel, buf);
  char dbuf[32];
  strftime(dbuf, sizeof(dbuf), "%Y-%m-%d %a", t);
  lv_label_set_text(dateLabel, dbuf);
  drawAnalogClock();
}

void ScreenSaver::initSoftClock() {
  setenv("TZ", "CST-8", 1);
  tzset();
  int d = 1, y = 2026, H = 0, M = 0, S = 0;
  char mon[4] = {0};
  static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  int mo = 1;
  if (sscanf(__DATE__, "%3s %d %d", mon, &d, &y) == 3) {
    for (int i = 0; i < 12; i++) {
      if (strncmp(mon, months[i], 3) == 0) { mo = i + 1; break; }
    }
  }
  sscanf(__TIME__, "%d:%d:%d", &H, &M, &S);
  struct tm t;
  t.tm_year = y - 1900;
  t.tm_mon = mo - 1;
  t.tm_mday = d;
  t.tm_hour = H; t.tm_min = M; t.tm_sec = S;
  t.tm_isdst = 0;
  time_t tt = mktime(&t);
  if (tt != (time_t)-1) {
    struct timeval tv = { (time_t)tt, 0 };
    settimeofday(&tv, nullptr);
  }
}
