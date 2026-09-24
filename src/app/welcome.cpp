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
lv_obj_t* g_scr = nullptr;
SwipeState g_swipe;

void enterLauncher() {
  if (g_entered) return;
  g_entered = true;
  if (g_timer) { lv_timer_del(g_timer); g_timer = nullptr; }
  if (nav_launcher) {
    /* ⚠️ auto_del 只能删「欢迎屏自己」。
       lv_scr_load_anim(..., auto_del=true) 删的是被替换掉的旧屏，而不是欢迎屏。
       如果 3 秒内用户（或串口 nav）已经先切进了别的应用，此刻活跃屏是那个应用，
       auto_del=true 就会把那棵对象树静默 lv_obj_del 掉：
         - nav.cpp 的 nav_browser 等指针仍在，只是变成悬空指针；
         - 屏上所有 lv_timer（状态栏 1s 定时刷新）还在跑，下一拍就写到已释放的
           label 上 → Guru Meditation (LoadProhibited) 重启。
       实测：串口 `browser https://m.baidu.com/` 加载完 3 秒定时一到就崩，正是此因。
       所以这里按「当前活跃屏是不是欢迎屏」决定 auto_del。 */
    const bool autoDel = (g_scr != nullptr && lv_scr_act() == g_scr);
    lv_scr_load_anim(nav_launcher, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 500, 0, autoDel);
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
  /* 3 秒自动进主桌面。但如果这 3 秒内已经不在欢迎屏上（触摸滑走了 / 串口 nav
     先切去了别的应用），这个定时器必须放弃，不能再抢屏（理由见 enterLauncher）。 */
  if (!g_scr || lv_scr_act() != g_scr) {
    if (g_timer) { lv_timer_del(g_timer); g_timer = nullptr; }
    g_entered = true;
    return;
  }
  enterLauncher();
}

}  // namespace

lv_obj_t* WelcomeScreen_create() {
  g_entered = false;
  lv_obj_t* scr = lv_obj_create(NULL);
  g_scr = scr;
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
