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
                         uint8_t dirs = SWIPE_ALL, bool auto_del = false) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
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
