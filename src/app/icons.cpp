#include "icons.h"
#include <math.h>
#include <esp_heap_caps.h>

namespace {

const float kPi = 3.14159265f;

lv_point_t P(int cx, int cy, int r, int deg) {
  float a = deg * kPi / 180.0f;
  lv_point_t p;
  p.x = (lv_coord_t)(cx + r * cosf(a));
  p.y = (lv_coord_t)(cy + r * sinf(a));
  return p;
}

void drawIcon(Icon type, lv_obj_t* canvas, uint16_t S) {
  int cx = S / 2, cy = S / 2;
  int R = S / 2;
  int lw = (int)(S / 16);
  if (lw < 2) lw = 2;

  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = lv_color_white();
  ld.width = (lv_coord_t)lw;
  ld.round_start = 1;
  ld.round_end = 1;
  ld.opa = LV_OPA_COVER;

  lv_draw_arc_dsc_t ad;
  lv_draw_arc_dsc_init(&ad);
  ad.color = lv_color_white();
  ad.width = (lv_coord_t)lw;
  ad.rounded = 1;
  ad.opa = LV_OPA_COVER;

  lv_draw_rect_dsc_t rd;
  lv_draw_rect_dsc_init(&rd);
  rd.bg_opa = LV_OPA_TRANSP;
  rd.border_color = lv_color_white();
  rd.border_width = (lv_coord_t)lw;
  rd.radius = (lv_coord_t)(S / 8);

  lv_point_t seg[3];

  switch (type) {
    case Icon::Clock: {
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.85f), 0, 360, &ad);
      seg[0] = P(cx, cy, (int)(R * 0.40f), -55);   // 时针
      seg[1] = {cx, cy};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      seg[0] = P(cx, cy, (int)(R * 0.62f), 60);     // 分针
      seg[1] = {cx, cy};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(S * 0.04f), 0, 360, &ad);
      break;
    }
    case Icon::Settings: {
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.55f), 0, 360, &ad);
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.22f), 0, 360, &ad);
      for (int i = 0; i < 8; i++) {
        int ang = i * 45;
        seg[0] = P(cx, cy, (int)(R * 0.55f), ang);
        seg[1] = P(cx, cy, (int)(R * 0.75f), ang);
        lv_canvas_draw_line(canvas, seg, 2, &ld);
      }
      break;
    }
    case Icon::Back: {
      seg[0] = P(cx, cy, (int)(R * 0.34f), 135);
      seg[1] = {cx, cy};
      seg[2] = P(cx, cy, (int)(R * 0.34f), 45);
      lv_canvas_draw_line(canvas, seg, 3, &ld);
      break;
    }
    case Icon::Wifi: {
      int by = cy + (int)(R * 0.5f);
      lv_canvas_draw_arc(canvas, cx, by, (lv_coord_t)(S * 0.05f), 0, 360, &ad);
      lv_canvas_draw_arc(canvas, cx, by, (lv_coord_t)(R * 0.28f), 220, 320, &ad);
      lv_canvas_draw_arc(canvas, cx, by, (lv_coord_t)(R * 0.50f), 212, 328, &ad);
      lv_canvas_draw_arc(canvas, cx, by, (lv_coord_t)(R * 0.72f), 206, 334, &ad);
      break;
    }
    case Icon::Weather: {
      int sx = cx, sy = cy - (int)(R * 0.05f);
      lv_canvas_draw_arc(canvas, sx, sy, (lv_coord_t)(R * 0.22f), 0, 360, &ad);
      for (int i = 0; i < 8; i++) {
        int ang = i * 45;
        seg[0] = P(sx, sy, (int)(R * 0.30f), ang);
        seg[1] = P(sx, sy, (int)(R * 0.44f), ang);
        lv_canvas_draw_line(canvas, seg, 2, &ld);
      }
      break;
    }
    case Icon::Switch: {
      lv_canvas_draw_rect(canvas, cx - (lv_coord_t)(R * 0.60f), cy - (lv_coord_t)(R * 0.22f),
                          (lv_coord_t)(R * 1.20f), (lv_coord_t)(R * 0.44f), &rd);
      lv_canvas_draw_arc(canvas, cx + (lv_coord_t)(R * 0.30f), cy, (lv_coord_t)(R * 0.16f), 0, 360, &ad);
      break;
    }
    case Icon::Terminal: {
      lv_canvas_draw_rect(canvas, cx - (lv_coord_t)(R * 0.60f), cy - (lv_coord_t)(R * 0.42f),
                          (lv_coord_t)(R * 1.20f), (lv_coord_t)(R * 0.72f), &rd);
      seg[0] = {cx - (lv_coord_t)(R * 0.35f), cy - (lv_coord_t)(R * 0.05f)};
      seg[1] = {cx - (lv_coord_t)(R * 0.18f), cy + (lv_coord_t)(R * 0.12f)};
      seg[2] = {cx - (lv_coord_t)(R * 0.35f), cy + (lv_coord_t)(R * 0.29f)};
      lv_canvas_draw_line(canvas, seg, 3, &ld);
      seg[0] = {cx - (lv_coord_t)(R * 0.08f), cy - (lv_coord_t)(R * 0.05f)};
      seg[1] = {cx - (lv_coord_t)(R * 0.08f), cy + (lv_coord_t)(R * 0.24f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      break;
    }
    case Icon::Music: {
      lv_canvas_draw_arc(canvas, cx - (lv_coord_t)(R * 0.18f), cy + (lv_coord_t)(R * 0.22f),
                         (lv_coord_t)(R * 0.16f), 0, 360, &ad);
      seg[0] = {cx - (lv_coord_t)(R * 0.02f), cy + (lv_coord_t)(R * 0.18f)};
      seg[1] = {cx - (lv_coord_t)(R * 0.02f), cy - (lv_coord_t)(R * 0.42f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      seg[0] = {cx - (lv_coord_t)(R * 0.02f), cy - (lv_coord_t)(R * 0.42f)};
      seg[1] = {cx + (lv_coord_t)(R * 0.22f), cy - (lv_coord_t)(R * 0.28f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      break;
    }
    case Icon::Power: {
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.40f), 290, 360, &ad);
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.40f), 0, 250, &ad);
      seg[0] = {cx, cy};
      seg[1] = {cx, cy - (lv_coord_t)(R * 0.42f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      break;
    }
    case Icon::Game: {
      lv_canvas_draw_rect(canvas, cx - (lv_coord_t)(R * 0.55f), cy - (lv_coord_t)(R * 0.35f),
                          (lv_coord_t)(R * 1.10f), (lv_coord_t)(R * 0.70f), &rd);
      lv_canvas_draw_arc(canvas, cx - (lv_coord_t)(R * 0.28f), cy, (lv_coord_t)(R * 0.08f), 0, 360, &ad);
      seg[0] = {cx + (lv_coord_t)(R * 0.15f), cy - (lv_coord_t)(R * 0.12f)};
      seg[1] = {cx + (lv_coord_t)(R * 0.35f), cy + (lv_coord_t)(R * 0.08f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      seg[0] = {cx + (lv_coord_t)(R * 0.35f), cy - (lv_coord_t)(R * 0.12f)};
      seg[1] = {cx + (lv_coord_t)(R * 0.15f), cy + (lv_coord_t)(R * 0.08f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      break;
    }
    case Icon::Browser: {
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.50f), 0, 360, &ad);
      seg[0] = {cx - (lv_coord_t)(R * 0.50f), cy};
      seg[1] = {cx + (lv_coord_t)(R * 0.50f), cy};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.25f), 0, 360, &ad);
      seg[0] = {cx, cy - (lv_coord_t)(R * 0.50f)};
      seg[1] = {cx, cy - (lv_coord_t)(R * 0.25f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      seg[0] = {cx, cy + (lv_coord_t)(R * 0.25f)};
      seg[1] = {cx, cy + (lv_coord_t)(R * 0.50f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      break;
    }
  }
}

}  // namespace

lv_obj_t* icon_create(lv_obj_t* parent, Icon type, uint16_t size) {
  if (size < 16) size = 16;
  lv_obj_t* canvas = lv_canvas_create(parent);
  uint32_t bufBytes = (uint32_t)size * size * 4;  // TRUE_COLOR_ALPHA: 4B/px
  // 从 PSRAM 分配 canvas 缓冲，不占用 LVGL 128KB 内存池
  void* buf = heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
  if (!buf) return canvas;
  lv_canvas_set_buffer(canvas, buf, (lv_coord_t)size, (lv_coord_t)size,
                       LV_IMG_CF_TRUE_COLOR_ALPHA);
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);
  drawIcon(type, canvas, size);
  return canvas;
}
