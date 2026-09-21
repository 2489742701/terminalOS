#include "welcome.h"
#include "icons.h"
#include "nav.h"
#include "font_zh.h"
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <time.h>

namespace {

bool g_entered = false;
lv_timer_t* g_timer = nullptr;
SwipeState g_swipe;

void enterLauncher() {
  if (g_entered) return;
  g_entered = true;
  if (g_timer) { lv_timer_del(g_timer); g_timer = nullptr; }
  if (nav_launcher) {
    lv_scr_load_anim(nav_launcher, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 500, 0, true);
    nav_lock_until = lv_tick_get() + 800;
  }
}

void swipe_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    g_swipe.startX = p.x; g_swipe.startY = p.y; g_swipe.triggered = false;
  } else if (code == LV_EVENT_PRESSING && !g_swipe.triggered) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    int dx = p.x - g_swipe.startX, dy = p.y - g_swipe.startY;
    if (abs(dx) > 40 || abs(dy) > 40) {
      g_swipe.triggered = true;
      enterLauncher();
    }
  }
}

void timer_cb(lv_timer_t* t) {
  (void)t;
  enterLauncher();
}

}  // namespace

lv_obj_t* WelcomeScreen_create() {
  g_entered = false;
  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSING, NULL);

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "GEEK TERMINAL");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

  lv_obj_t* hint = lv_label_create(scr);
  lv_label_set_text(hint, "滑动进入");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
  lv_obj_set_style_text_font(hint, &font_zh_16, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -50);

  g_timer = lv_timer_create(timer_cb, 3000, nullptr);
  return scr;
}
