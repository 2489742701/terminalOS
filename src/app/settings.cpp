#include "settings.h"
#include "icons.h"
#include "nav.h"
#include "font_zh.h"
#include "screensaver.h"
#include "../hal/display.h"
#include <lvgl.h>

namespace {

lv_obj_t* g_statusLab = nullptr;
SwipeState g_swipe;

void back_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
}

void swipe_cb(lv_event_t* e) {
  swipe_detect(e, g_swipe, nav_launcher, SWIPE_H);
}

void calib_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_statusLab) {
    lv_label_set_text(g_statusLab, "未联网，校时待接入 WiFi");
  }
}

void sleep_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  ScreenSaver::sleepNow();
}

void brightness_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
  int val = lv_slider_get_value(slider);
  Display::setBacklightLevel((uint8_t)val);
}

lv_obj_t* makeRow(lv_obj_t* parent, const char* key, const char* val, int y) {
  lv_obj_t* k = lv_label_create(parent);
  lv_label_set_text(k, key);
  lv_obj_set_style_text_color(k, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(k, &font_zh_16, 0);
  lv_obj_align(k, LV_ALIGN_TOP_LEFT, 40, y);

  lv_obj_t* v = lv_label_create(parent);
  lv_label_set_text(v, val);
  lv_obj_set_style_text_color(v, lv_color_white(), 0);
  lv_obj_set_style_text_font(v, &font_zh_16, 0);
  lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -40, y);
  return v;
}

}  // namespace

lv_obj_t* SettingsScreen_create() {
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

  lv_obj_t* back = icon_create(scr, Icon::Back, 40);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 18, 18);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(back, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(back, back_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "设置");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &font_zh_24, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

  makeRow(scr, "时间源", "编译时软时钟", 100);
  makeRow(scr, "固件", "GEEK TERMINAL v0.1", 140);

  // 校准时间按钮
  lv_obj_t* btn = lv_btn_create(scr);
  lv_obj_set_size(btn, 200, 44);
  lv_obj_align(btn, LV_ALIGN_TOP_MID, -110, 185);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, lv_color_white(), 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_radius(btn, 10, 0);
  lv_obj_add_event_cb(btn, calib_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t* blab = lv_label_create(btn);
  lv_label_set_text(blab, "校准时间");
  lv_obj_set_style_text_color(blab, lv_color_white(), 0);
  lv_obj_set_style_text_font(blab, &font_zh_16, 0);
  lv_obj_center(blab);

  // 息屏按钮
  lv_obj_t* sbtn = lv_btn_create(scr);
  lv_obj_set_size(sbtn, 200, 44);
  lv_obj_align(sbtn, LV_ALIGN_TOP_MID, 110, 185);
  lv_obj_set_style_bg_opa(sbtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(sbtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(sbtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(sbtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(sbtn, 1, 0);
  lv_obj_set_style_radius(sbtn, 10, 0);
  lv_obj_add_event_cb(sbtn, sleep_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(sbtn, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t* slab = lv_label_create(sbtn);
  lv_label_set_text(slab, "息屏");
  lv_obj_set_style_text_color(slab, lv_color_white(), 0);
  lv_obj_set_style_text_font(slab, &font_zh_16, 0);
  lv_obj_center(slab);

  // 亮度滑块
  lv_obj_t* brLab = lv_label_create(scr);
  lv_label_set_text(brLab, "亮度");
  lv_obj_set_style_text_color(brLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(brLab, &font_zh_16, 0);
  lv_obj_align(brLab, LV_ALIGN_TOP_LEFT, 40, 250);

  lv_obj_t* slider = lv_slider_create(scr);
  lv_obj_set_width(slider, 300);
  lv_obj_align(slider, LV_ALIGN_TOP_MID, 30, 252);
  lv_slider_set_range(slider, 5, 100);
  lv_slider_set_value(slider, 100, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
  lv_obj_add_event_cb(slider, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_flag(slider, LV_OBJ_FLAG_EVENT_BUBBLE);

  g_statusLab = lv_label_create(scr);
  lv_label_set_text(g_statusLab, "双击主屏时间可校准（待接入 WiFi）");
  lv_obj_set_style_text_color(g_statusLab, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(g_statusLab, &font_zh_16, 0);
  lv_obj_align(g_statusLab, LV_ALIGN_TOP_MID, 0, 300);

  return scr;
}
