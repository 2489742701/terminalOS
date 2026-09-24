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
extern lv_obj_t* nav_calendar;
extern lv_obj_t* nav_games;    // 游戏栏目：贪吃蛇 / 记忆卡牌 / 2048
extern lv_obj_t* nav_desktop;  // 桌面图标管理（设置里进入）
extern lv_obj_t* nav_taskmgr;  // 后台管理（任务管理器）
extern lv_obj_t* nav_touchtest; // 触摸测试（诊断坐标偏移，串口 `nav touchtest`）

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

/* 带动画的"回桌面"（顶栏返回键 / 设置根页返回用）。
   ⚠️ 为什么不能直接用 nav_go_anim + nav_back_home：
      nav_back_home 是「无动画 lv_scr_load 后立刻 lv_obj_del 旧屏」——
      那是被逼的：动画期间旧屏仍在参与渲染，边动画边删必崩（本项目踩过）。
      这里的折中是**先把动画跑完，再延迟一拍销毁**：切屏立刻发生（旧屏此刻
      已经不再是活动屏，但动画还在画它），280ms 后动画结束才真正释放。
      关动画时直接退化成 nav_back_home()，一步到位。 */
void nav_back_home_anim();

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

/* 动画总开关（定义在 settings_store.cpp，开机从 NVS 读一次）。
   ⚠️ 改动**重启才生效**：见 settings_store.h 里的说明。
   这里用 extern 而不是每次去读 NVS —— Preferences 每次 begin/end 都要开句柄，
   而动画判定发生在切页/返回的高频路径上。 */
extern bool g_uiAnim;
inline bool ui_anim() { return g_uiAnim; }

inline uint32_t nav_lock_until = 0;

inline bool nav_is_locked() {
  return lv_tick_get() < nav_lock_until;
}

inline void nav_go(lv_obj_t* scr) {
  if (scr) lv_scr_load(scr);
}

inline void nav_go_anim(lv_obj_t* scr, lv_scr_load_anim_t anim, uint32_t time = 300, bool auto_del = false) {
  if (!scr) return;
  if (!g_uiAnim) {
    /* 关动画：直接切，不排队动画。nav_lock 仍然要给一点点，
       否则"切完立刻又切"会在同一帧里连着发生（触摸连点）。 */
    lv_scr_load(scr);
    nav_lock_until = lv_tick_get() + 120;
    return;
  }
  lv_scr_load_anim(scr, anim, time, 0, auto_del);
  nav_lock_until = lv_tick_get() + time + 300;
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

/* ══ 统一的「左边缘滑入返回」手势（master 2026-09-25 定的约定）══════════════
 * 以前每个屏自己调 swipe_detect，参数五花八门：有的限制左边缘 40px，
 * 有的**全屏任意位置横滑都能返回**（跟横向滚动 / 游戏操作打架），
 * 还有两个屏压根没接。现在统一成这一个：
 *
 *   手势 = **从左边缘起手**（x <= SWIPE_EDGE_W）+ **往屏幕内部（右）**滑够距离。
 *   ⚠️ 边缘外起手一律不算，"左滑一下返回"只在这条边里有效。
 *
 * 用法（在屏的 PRESSED / PRESSING / RELEASED 三个事件上都挂同一个 cb）：
 *   swipe_back_to(e, st, nav_launcher);        // 切到某个屏
 *   swipe_back_act(e, st, settings_menu_back_action);  // 或执行一个动作（如返回上一级）
 * ══════════════════════════════════════════════════════════════════════════ */
constexpr uint8_t SWIPE_EDGE_W = 44;   // 左边缘可起手宽度（px）
constexpr int SWIPE_BACK_DX = 36;      // 往里滑多少算触发（px）

/* 返回 true = 本次手势成立（一帧内只成立一次） */
inline bool swipe_back_detect(lv_event_t* e, SwipeState& st,
                              uint8_t edgeWidth = SWIPE_EDGE_W) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    if (p.x > edgeWidth) {
      /* 不是从边缘起手：把 triggered 置上，本次手势彻底作废
         （后面再怎么滑都不会触发，避免误触） */
      st.triggered = true;
      return false;
    }
    st.startX = p.x; st.startY = p.y; st.triggered = false;
    return false;
  }
  if (code == LV_EVENT_PRESSING && !st.triggered) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    int dx = p.x - st.startX, dy = p.y - st.startY;
    if (dx < SWIPE_BACK_DX) return false;   /* 只认"往里"（右）滑 */
    if (abs(dy) > dx) return false;         /* 竖向为主 -> 让给滚动 */
    st.triggered = true;
    return true;
  }
  if (code == LV_EVENT_RELEASED) st.triggered = false;
  return false;
}

inline void swipe_back_to(lv_event_t* e, SwipeState& st, lv_obj_t* target,
                          uint8_t edgeWidth = SWIPE_EDGE_W) {
  if (swipe_back_detect(e, st, edgeWidth) && target)
    nav_go_anim(target, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300);
}

inline void swipe_back_act(lv_event_t* e, SwipeState& st, void (*act)(),
                           uint8_t edgeWidth = SWIPE_EDGE_W) {
  if (swipe_back_detect(e, st, edgeWidth) && act) act();
}

/* ══ 通用版："所有页面左右滑动都能退出" ═══════════════════════════════════
 * master 2026-09-25：左右滑动退出是**全页通用**的；上下滑动退出则**按页面**
 * 决定（浏览器这种要滚动的页面不给）。
 *
 * 与上面边缘版的区别：
 *   · 水平：**任意起手点**都能触发（边缘版只认左边缘 44px），
 *     左滑 / 右滑都算 —— 触发距离放大到 SWIPE_BACK_DX_ANY 防误触。
 *   · 竖向：只在**上/下边缘 44px 内起手**才认，中间起手让给页面滚动
 *     （否则设置页、任务管理这种能滚的页面根本没法滚）。
 *
 * ⚠️ 游戏 / 画板 / 触摸测试**不要用这个**：它们本来就把滑动当操作，
 *    必须继续用边缘版 swipe_back_detect。
 */
constexpr int SWIPE_BACK_DX_ANY = 48;   // 任意起手 -> 门槛调高一点
constexpr int SWIPE_BACK_DY = 48;       // 竖向触发距离

inline bool swipe_back_any(lv_event_t* e, SwipeState& st,
                           bool allowVertical = true, int screenH = 480) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    st.startX = p.x; st.startY = p.y; st.triggered = false;
    return false;
  }
  if (code == LV_EVENT_PRESSING && !st.triggered) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    int dx = p.x - st.startX, dy = p.y - st.startY;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax >= SWIPE_BACK_DX_ANY && ax >= ay) {   /* 左右滑：哪个方向都算 */
      st.triggered = true;
      return true;
    }
    if (allowVertical && ay >= SWIPE_BACK_DY && ay > ax &&
        (st.startY <= SWIPE_EDGE_W || st.startY >= screenH - SWIPE_EDGE_W)) {
      st.triggered = true;                        /* 上下滑：只认上下边缘起手 */
      return true;
    }
    return false;
  }
  if (code == LV_EVENT_RELEASED) st.triggered = false;
  return false;
}

inline void swipe_back_to_any(lv_event_t* e, SwipeState& st, lv_obj_t* target,
                              bool allowVertical = true) {
  if (swipe_back_any(e, st, allowVertical) && target)
    nav_go_anim(target, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300);
}

inline void swipe_back_act_any(lv_event_t* e, SwipeState& st, void (*act)(),
                               bool allowVertical = true) {
  if (swipe_back_any(e, st, allowVertical) && act) act();
}
