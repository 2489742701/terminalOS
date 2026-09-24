#include "touchtest_screen.h"
#include "nav.h"
#include "status_bar.h"
#include "font_zh.h"
#include "../hal/touch.h"
#include <lvgl.h>
#include <Arduino.h>
#include <stdio.h>

/* ══ 触摸测试屏 ═══════════════════════════════════════════════════════════
 * 存在的唯一理由：master 说"不知道怎么测触摸偏移"。
 * 所以这里把诊断做成**纯视觉**的 —— 不需要理解坐标，不需要看串口：
 *
 *   手指按在哪，白色十字就该出现在哪。
 *   不重合 = 映射错了；往哪边偏、偏多少，一眼看得见。
 *
 * 底部四个开关点了立即生效（改的是 Touch 里的校准端点，不落盘、不重烧）：
 *   翻转X / 翻转Y / 交换XY / 重置
 * 试出对的组合后，用串口 `tcal x0 x1 y0 y1` 把端点抄进代码即可。
 *
 * 另外每次抬手都会往串口打一行坐标 —— 四个角各点一下，
 * 我这边就能直接算出端点，不需要 master 手抄数字。
 * ═════════════════════════════════════════════════════════════════════════ */

namespace {

constexpr int AREA_X = 0;
constexpr int AREA_Y = 80;
constexpr int AREA_W = 480;
constexpr int AREA_H = 300;      // 80..380

lv_obj_t* g_vLine = nullptr;
lv_obj_t* g_hLine = nullptr;
lv_obj_t* g_infoLab = nullptr;
lv_obj_t* g_stateLab = nullptr;

/* 十字：两条 1px 的细条。用 lv_obj 而不是 canvas —— 不用额外 PSRAM，
   挪位置只要 set_pos，比重画整块画布便宜得多。 */
lv_obj_t* mkLine(lv_obj_t* scr, int w, int h) {
  lv_obj_t* o = lv_obj_create(scr);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);   /* 别吃掉触摸事件 */
  lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);        /* 没按过之前不显示 */
  return o;
}

/* 四角靶心：给"该点哪"一个明确目标。点了看十字有没有落在圆点上 */
void mkTarget(lv_obj_t* scr, int cx, int cy) {
  lv_obj_t* o = lv_obj_create(scr);
  lv_obj_set_size(o, 24, 24);
  lv_obj_set_pos(o, cx - 12, cy - 12);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(o, lv_color_hex(0x666666), 0);
  lv_obj_set_style_border_width(o, 2, 0);
  lv_obj_set_style_radius(o, 12, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

void refreshState() {
  if (!g_stateLab) return;
  int x0, x1, y0, y1;
  bool sw = false;
  Touch::getCal(x0, x1, y0, y1, sw);
  char buf[80];
  snprintf(buf, sizeof(buf), "X %d→%d  Y %d→%d  %s",
           x0, x1, y0, y1, sw ? "已交换XY" : "");
  lv_label_set_text(g_stateLab, buf);
}

void touch_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
      code != LV_EVENT_RELEASED) return;

  lv_point_t p;
  lv_indev_get_point(lv_indev_get_act(), &p);

  int rx = -1, ry = -1;
  bool haveRaw = Touch::rawXY(rx, ry);

  if (g_vLine) {
    lv_obj_clear_flag(g_vLine, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(g_vLine, p.x, AREA_Y);
  }
  if (g_hLine) {
    lv_obj_clear_flag(g_hLine, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(g_hLine, AREA_X, p.y);
  }
  if (g_infoLab) {
    char buf[64];
    if (haveRaw) snprintf(buf, sizeof(buf), "屏幕 %d,%d   裸值 %d,%d", p.x, p.y, rx, ry);
    else         snprintf(buf, sizeof(buf), "屏幕 %d,%d", p.x, p.y);
    lv_label_set_text(g_infoLab, buf);
  }
  /* 抬手才打串口：PRESSING 每 8ms 一次，全打会刷屏 */
  if (code == LV_EVENT_RELEASED && haveRaw) {
    Serial.printf("[TouchTest] screen=(%d,%d) raw=(%d,%d)\n", p.x, p.y, rx, ry);
  }
}

void flip_x_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  Touch::flipX();
  refreshState();
}
void flip_y_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  Touch::flipY();
  refreshState();
}
void swap_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  Touch::swapXY();
  refreshState();
}
void reset_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  Touch::resetCal();
  refreshState();
}
void back_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
}

lv_obj_t* mkBtn(lv_obj_t* scr, const char* txt, lv_event_cb_t cb, int x, int y, int w) {
  lv_obj_t* b = lv_btn_create(scr);
  lv_obj_set_size(b, w, 34);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(b, lv_color_white(), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_obj_set_style_text_font(l, &font_zh_16, 0);
  lv_obj_center(l);
  return b;
}

void swipe_cb(lv_event_t* e) {
  static SwipeState st;
  swipe_detect(e, st, nav_launcher, SWIPE_H);
}

}  // namespace

lv_obj_t* TouchTestScreen_create() {
  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_RELEASED, NULL);

  StatusBar_create(scr, "触摸测试");

  g_infoLab = lv_label_create(scr);
  lv_label_set_text(g_infoLab, "屏幕 --,--   裸值 --,--");
  lv_obj_set_style_text_color(g_infoLab, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_text_font(g_infoLab, &font_zh_16, 0);
  lv_obj_align(g_infoLab, LV_ALIGN_TOP_LEFT, 16, 34);

  lv_obj_t* hint = lv_label_create(scr);
  lv_label_set_text(hint, "手指按在哪，十字就该出现在哪");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(hint, &font_zh_16, 0);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 56);

  g_stateLab = lv_label_create(scr);
  lv_label_set_text(g_stateLab, "");
  lv_obj_set_style_text_color(g_stateLab, lv_color_hex(0x66CCFF), 0);
  lv_obj_set_style_text_font(g_stateLab, &lv_font_montserrat_14, 0);
  lv_obj_align(g_stateLab, LV_ALIGN_TOP_MID, 0, 386);

  mkTarget(scr, 40, 100);
  mkTarget(scr, 440, 100);
  mkTarget(scr, 40, 360);
  mkTarget(scr, 440, 360);

  g_vLine = mkLine(scr, 1, AREA_H);
  lv_obj_set_pos(g_vLine, AREA_X, AREA_Y);
  g_hLine = mkLine(scr, AREA_W, 1);
  lv_obj_set_pos(g_hLine, AREA_X, AREA_Y);

  /* ⚠️ 480 高是硬边界：两行按钮必须都落在屏内。
     row1 404..438、row2 442..476，再往下就出屏了（改过一次，别再往下挪）。 */
  mkBtn(scr, "翻转X", flip_x_cb, 80, 404, 100);
  mkBtn(scr, "翻转Y", flip_y_cb, 190, 404, 100);
  mkBtn(scr, "交换XY", swap_cb, 300, 404, 100);
  mkBtn(scr, "重置", reset_cb, 80, 442, 100);
  mkBtn(scr, "返回", back_cb, 300, 442, 100);

  refreshState();
  return scr;
}
