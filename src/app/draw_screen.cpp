#include "draw_screen.h"
#include "icons.h"
#include "nav.h"
#include "font_zh.h"
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <math.h>

namespace {

constexpr int CANVAS_W = 440;
constexpr int CANVAS_H = 340;

SwipeState g_swipe;
lv_obj_t* g_canvas = nullptr;
int g_lastX = -1, g_lastY = -1;
bool g_drawing = false;

void back_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
}

void swipe_cb(lv_event_t* e) {
  swipe_detect(e, g_swipe, nav_launcher, SWIPE_H);
}

void clear_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_canvas) lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);
}

void draw_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    int cxOff = lv_obj_get_x(g_canvas);
    int cyOff = lv_obj_get_y(g_canvas);
    g_lastX = p.x - cxOff; g_lastY = p.y - cyOff; g_drawing = true;
  } else if (code == LV_EVENT_PRESSING && g_drawing) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);

    int cxOff = lv_obj_get_x(g_canvas);
    int cyOff = lv_obj_get_y(g_canvas);
    int x2 = p.x - cxOff, y2 = p.y - cyOff;
    int x1 = g_lastX, y1 = g_lastY;

    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_white();
    ld.width = 3;
    ld.round_start = 1;
    ld.round_end = 1;
    ld.opa = LV_OPA_COVER;

    int dx = x2 - x1, dy = y2 - y1;
    int steps = (int)sqrtf((float)(dx * dx + dy * dy));
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
      float t = (float)i / steps;
      int px = (int)(x1 + dx * t);
      int py = (int)(y1 + dy * t);
      if (px >= 0 && px < CANVAS_W && py >= 0 && py < CANVAS_H) {
        for (int oy = -1; oy <= 1; oy++) {
          for (int ox = -1; ox <= 1; ox++) {
            int fx = px + ox, fy = py + oy;
            if (fx >= 0 && fx < CANVAS_W && fy >= 0 && fy < CANVAS_H) {
              lv_canvas_set_px_color(g_canvas, fx, fy, lv_color_white());
            }
          }
        }
      }
    }

    g_lastX = x2; g_lastY = y2;
  } else if (code == LV_EVENT_RELEASED) {
    g_drawing = false;
    g_lastX = -1; g_lastY = -1;
  }
}

}  // namespace

lv_obj_t* DrawScreen_create() {
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

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "画板");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &font_zh_24, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

  g_canvas = lv_canvas_create(scr);
  void* buf = heap_caps_malloc(CANVAS_W * CANVAS_H * 2, MALLOC_CAP_SPIRAM);
  if (buf) {
    lv_canvas_set_buffer(g_canvas, buf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);
  }
  lv_obj_align(g_canvas, LV_ALIGN_TOP_MID, 0, 60);
  lv_obj_add_flag(g_canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_canvas, draw_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(g_canvas, draw_cb, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(g_canvas, draw_cb, LV_EVENT_RELEASED, NULL);

  lv_obj_t* clrBtn = lv_btn_create(scr);
  lv_obj_set_size(clrBtn, 100, 36);
  lv_obj_align(clrBtn, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_set_style_bg_opa(clrBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(clrBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(clrBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(clrBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(clrBtn, 1, 0);
  lv_obj_set_style_radius(clrBtn, 8, 0);
  lv_obj_add_event_cb(clrBtn, clear_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(clrBtn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t* clab = lv_label_create(clrBtn);
  lv_label_set_text(clab, "清除");
  lv_obj_set_style_text_color(clab, lv_color_white(), 0);
  lv_obj_set_style_text_font(clab, &font_zh_16, 0);
  lv_obj_center(clab);

  return scr;
}