# -*- coding: utf-8 -*-
"""触摸测试屏的接入：nav 表 / App::loop 全局定义 / 串口 nav / 画板入口按钮。"""
import io
GT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"


def rep(rel, old, new, crlf=False):
    p = GT + "\\" + rel
    t = io.open(p, encoding="utf-8", newline=None).read()
    assert t.count(old) == 1, "%s: count=%d for %r" % (rel, t.count(old), old[:70])
    t = t.replace(old, new)
    if crlf:
        t = t.replace("\n", "\r\n")
    io.open(p, "w", encoding="utf-8", newline="").write(t)
    print("patched", rel)


# ── nav.h ──────────────────────────────────────────────────────────────
rep(r"src\app\nav.h",
    "extern lv_obj_t* nav_taskmgr;  // 后台管理（任务管理器）",
    "extern lv_obj_t* nav_taskmgr;  // 后台管理（任务管理器）\n"
    "extern lv_obj_t* nav_touchtest; // 触摸测试（诊断坐标偏移，串口 `nav touchtest`）")

# ── app.cpp：全局定义（漏了就是链接错）──────────────────────────────────
rep(r"src\app\app.cpp",
    "lv_obj_t* nav_taskmgr = nullptr;",
    "lv_obj_t* nav_taskmgr = nullptr;\nlv_obj_t* nav_touchtest = nullptr;")

# ── nav.cpp ────────────────────────────────────────────────────────────
rep(r"src\app\nav.cpp",
    '#include "taskmgr_screen.h"',
    '#include "taskmgr_screen.h"\n#include "touchtest_screen.h"')
rep(r"src\app\nav.cpp",
    '    {"taskmgr",  &nav_taskmgr,  TaskMgrScreen_create,  nullptr,         nullptr,         0},',
    '    {"taskmgr",  &nav_taskmgr,   TaskMgrScreen_create,   nullptr,         nullptr,         0},\n'
    '    /* 触摸测试：诊断用，不进 Launcher，只走串口 `nav touchtest` 和画板底部入口 */\n'
    '    {"touchtest", &nav_touchtest, TouchTestScreen_create, nullptr,        nullptr},')

# ── serial_console.cpp ─────────────────────────────────────────────────
rep(r"src\hal\serial_console.cpp",
    '{"taskmgr",   &nav_taskmgr},',
    '{"taskmgr",   &nav_taskmgr},\n  {"touchtest", &nav_touchtest},')
rep(r"src\hal\serial_console.cpp",
    '  Serial.println("  screens: launcher clock settings wifi game 2048 browser draw memory sysinfo weather games desktop taskmgr");',
    '  Serial.println("  screens: launcher clock settings wifi game 2048 browser draw memory sysinfo weather games desktop taskmgr touchtest");')

# ── draw_screen.cpp：底部加一个「 touch 」入口 ──────────────────────────
rep(r"src\app\draw_screen.cpp",
    '''  lv_obj_t* clrBtn = lv_btn_create(scr);
  lv_obj_set_size(clrBtn, 100, 36);
  lv_obj_align(clrBtn, LV_ALIGN_BOTTOM_MID, 0, -12);''',
    '''  /* 「清除」挪到左边，右边给「触摸」留出位置 —— 画板上手感正常，
     所以把触摸测试入口放在这里最顺手（master 提的）。 */
  lv_obj_t* clrBtn = lv_btn_create(scr);
  lv_obj_set_size(clrBtn, 100, 36);
  lv_obj_align(clrBtn, LV_ALIGN_BOTTOM_LEFT, 40, -12);''')

rep(r"src\app\draw_screen.cpp",
    '''  lv_obj_t* clab = lv_label_create(clrBtn);
  lv_label_set_text(clab, "清除");
  lv_obj_set_style_text_color(clab, lv_color_white(), 0);
  lv_obj_set_style_text_font(clab, &font_zh_16, 0);
  lv_obj_center(clab);

  return scr;''',
    '''  lv_obj_t* clab = lv_label_create(clrBtn);
  lv_label_set_text(clab, "清除");
  lv_obj_set_style_text_color(clab, lv_color_white(), 0);
  lv_obj_set_style_text_font(clab, &font_zh_16, 0);
  lv_obj_center(clab);

  lv_obj_t* ttBtn = lv_btn_create(scr);
  lv_obj_set_size(ttBtn, 100, 36);
  lv_obj_align(ttBtn, LV_ALIGN_BOTTOM_RIGHT, -40, -12);
  lv_obj_set_style_bg_opa(ttBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(ttBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(ttBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(ttBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(ttBtn, 1, 0);
  lv_obj_set_style_radius(ttBtn, 8, 0);
  lv_obj_add_event_cb(ttBtn, touchtest_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(ttBtn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t* tlab = lv_label_create(ttBtn);
  lv_label_set_text(tlab, "触摸");
  lv_obj_set_style_text_color(tlab, lv_color_white(), 0);
  lv_obj_set_style_text_font(tlab, &font_zh_16, 0);
  lv_obj_center(tlab);

  return scr;''')

# ── draw_screen.cpp：加回调 + include ──────────────────────────────────
rep(r"src\app\draw_screen.cpp",
    "lv_obj_t* DrawScreen_create() {",
    '''/* 跳触摸测试屏。走 nav_open 按需创建 —— 它不是常用屏，不该常驻占 DRAM。 */
void touchtest_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (nav_is_locked()) return;
  if (!nav_touchtest) nav_open(&nav_touchtest);
  if (!nav_touchtest) return;
  nav_go_anim(nav_touchtest, LV_SCR_LOAD_ANIM_OVER_RIGHT, 300);
}

lv_obj_t* DrawScreen_create() {''')
print("ALL OK")
