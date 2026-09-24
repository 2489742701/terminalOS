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
extern lv_obj_t* nav_2048;     // 2048（游戏栏目）
extern lv_obj_t* nav_sysinfo;
extern lv_obj_t* nav_weather;
extern lv_obj_t* nav_games;    // 游戏栏目：贪吃蛇 / 记忆卡牌 / 2048
extern lv_obj_t* nav_desktop;  // 桌面图标管理（设置里进入）
extern lv_obj_t* nav_taskmgr;  // 后台管理（任务管理器）

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

/* ── 后台管理（任务管理器）接口 ──────────────────────────────────────────────
   应用本来就是按需创建的，但切屏过程中仍会有几棵对象树留在内存里。
   后台页要把它们列出来让用户能关掉 —— 所以 nav 得能回答"谁还在"。 */
struct NavRunningInfo {
  const char* id;      // Activity 名（"browser"），静态字符串
  uint32_t bytes;      // 创建时吃掉的内部 DRAM（0 = 未计量）
  bool current;        // 是否当前前台（前台不可关：删当前屏必崩）
};

// 列出所有仍在内存里的 Activity（Launcher 不算），返回条数
int nav_running_list(NavRunningInfo* out, int max);

// 关掉指定 Activity。前台 / Launcher / 释放守卫不通过 -> 返回 false
bool nav_close(const char* id);

// 关掉除 Launcher 和前台之外的所有 Activity，返回实际关掉的个数
int nav_close_all();

/* 游戏栏目里的子游戏返回时用：优先回游戏栏目（不存在就现开一个），
   万一开不出来再退回桌面 —— 避免从游戏栏目进贪吃蛇、退出却掉到桌面。 */
lv_obj_t* nav_games_or_home();

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
