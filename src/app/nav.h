#pragma once
#include <lvgl.h>
#include <stdlib.h>

extern lv_obj_t* nav_launcher;
extern lv_obj_t* nav_clock;
extern lv_obj_t* nav_settings;
extern lv_obj_t* nav_wifi;
extern lv_obj_t* nav_game;
extern lv_obj_t* nav_browser;
extern lv_obj_t* nav_draw;
extern lv_obj_t* nav_memory;
extern lv_obj_t* nav_sysinfo;
extern lv_obj_t* nav_weather;

/* ══ Activity 注册表（阶段 1）══
 * ESP32 没有 MMU/换页，lv_obj_create(NULL) 建的对象树会一直常驻，
 * 直到显式 lv_obj_del()。所以除 Launcher 外，所有应用改为：
 *   进入时按需创建（nav_open），离开/抢占时整体销毁（nav_release_*）。
 * 这样同一时刻最多 2 棵对象树存活，把 DRAM 让给重量级应用（浏览器）。
 * ══════════════════════════════════════════════════════════════════ */

// 按需创建：若 *target 为空则调用注册表里的 create()，返回实例（失败返回 nullptr）
lv_obj_t* nav_open(lv_obj_t** target);

// 销毁除 keep1/keep2 之外的所有已加载 Activity，释放其对象树
void nav_release_all_except(lv_obj_t* keep1, lv_obj_t* keep2 = nullptr);

// 回到 Launcher：先销毁其他所有 Activity，再切屏（唯一的"退出应用"入口）
void nav_back_home();

inline uint32_t nav_lock_until = 0;

inline bool nav_is_locked() {
  return lv_tick_get() < nav_lock_until;
}

inline void nav_go(lv_obj_t* scr) {
  if (scr) lv_scr_load(scr);
}

inline void nav_go_anim(lv_obj_t* scr, lv_scr_load_anim_t anim, uint32_t time = 300, bool auto_del = false) {
  if (scr) {
    lv_scr_load_anim(scr, anim, time, 0, auto_del);
    nav_lock_until = lv_tick_get() + time + 300;
  }
}

struct SwipeState {
  int startX = 0, startY = 0;
  bool triggered = false;
};

enum SwipeDir : uint8_t {
  SWIPE_H   = 0x01,
  SWIPE_V   = 0x02,
  SWIPE_ALL = 0x03,
};

inline void swipe_detect(lv_event_t* e, SwipeState& st, lv_obj_t* target,
                         uint8_t dirs = SWIPE_ALL, bool auto_del = false,
                         uint8_t edgeWidth = 0) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    if (edgeWidth > 0 && p.x > edgeWidth) {
      st.triggered = true;
      return;
    }
    st.startX = p.x; st.startY = p.y; st.triggered = false;
  } else if (code == LV_EVENT_PRESSING && !st.triggered) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    int dx = p.x - st.startX, dy = p.y - st.startY;

    bool hOk = (dirs & SWIPE_H) && abs(dx) > 40;
    bool vOk = (dirs & SWIPE_V) && abs(dy) > 40;
    if (!hOk && !vOk) return;

    st.triggered = true;
    lv_scr_load_anim_t anim;
    bool useH = hOk && (!vOk || abs(dx) > abs(dy));
    if (useH) {
      anim = (dx > 0) ? LV_SCR_LOAD_ANIM_MOVE_RIGHT : LV_SCR_LOAD_ANIM_MOVE_LEFT;
    } else {
      anim = (dy > 0) ? LV_SCR_LOAD_ANIM_MOVE_BOTTOM : LV_SCR_LOAD_ANIM_MOVE_TOP;
    }
    if (target) {
      lv_scr_load_anim(target, anim, 300, 0, auto_del);
      nav_lock_until = lv_tick_get() + 600;
    }
  } else if (code == LV_EVENT_RELEASED) {
    st.triggered = false;
  }
}
