#include "desktop_screen.h"
#include "app_registry.h"
#include "icons.h"
#include "launcher.h"
#include "nav.h"
#include "status_bar.h"
#include "font_zh.h"
#include <lvgl.h>
#include <string.h>
#include <stdio.h>

namespace {

/* 每行的开关句柄，按注册表下标存 —— 「恢复默认」要能一次性把开关推回默认态。 */
lv_obj_t* g_switches[12];
lv_obj_t* g_statusLab = nullptr;

void back_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
}

void swipe_cb(lv_event_t* e) {
  static SwipeState st;
  /* 只认左右滑：上下留给列表滚动，否则滑到一半就把整屏滑走了 */
  swipe_back_to_any(e, st, nav_launcher);
}

void setStatus(const char* msg) {
  if (g_statusLab && lv_obj_is_valid(g_statusLab)) lv_label_set_text(g_statusLab, msg);
}

void sw_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);

  appreg_set_visible(idx, on);
  /* 立刻重画 launcher 的磁贴区：launcher 屏对象本身不动，
     所以 ScreenSaver 持有的 nav_launcher 指针不会失效。 */
  LauncherScreen_refresh(nav_launcher);

  const AppEntry* ent = appreg_at(idx);
  if (ent && !on) {
    char msg[48];
    snprintf(msg, sizeof(msg), "已从桌面隐藏「%s」", ent->label);
    setStatus(msg);
  } else if (ent) {
    char msg[48];
    snprintf(msg, sizeof(msg), "「%s」已回到桌面", ent->label);
    setStatus(msg);
  }
}

void reset_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  appreg_reset_visible();
  for (int i = 0; i < appreg_count(); i++) {
    const AppEntry* ent = appreg_at(i);
    if (!ent || ent->group != AppGroup::Desktop) continue;
    if (g_switches[i] && lv_obj_is_valid(g_switches[i])) {
      if (appreg_visible(i)) lv_obj_add_state(g_switches[i], LV_STATE_CHECKED);
      else lv_obj_clear_state(g_switches[i], LV_STATE_CHECKED);
    }
  }
  LauncherScreen_refresh(nav_launcher);
  setStatus("已恢复默认显示");
}

void addRow(lv_obj_t* cont, int idx, int y) {
  const AppEntry* ent = appreg_at(idx);
  if (!ent) return;

  lv_obj_t* row = lv_obj_create(cont);
  lv_obj_set_size(row, 440, 48);
  lv_obj_set_pos(row, 20, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_radius(row, 10, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* ic = icon_create(row, ent->icon, 28);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 8, 0);

  lv_obj_t* lab = lv_label_create(row);
  lv_label_set_text(lab, ent->label);
  lv_obj_set_style_text_color(lab, lv_color_white(), 0);
  lv_obj_set_style_text_font(lab, &font_zh_16, 0);
  lv_obj_align(lab, LV_ALIGN_LEFT_MID, 48, 0);

  lv_obj_t* sw = lv_switch_create(row);
  lv_obj_set_size(sw, 46, 24);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_add_event_cb(sw, sw_event_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)idx);
  if (appreg_visible(idx)) lv_obj_add_state(sw, LV_STATE_CHECKED);

  /* 硬保护：「设置」永远不许隐藏。
     真把它藏了，用户就再也没有入口能改回来（只能刷机清 NVS）。 */
  if (strcmp(ent->id, "settings") == 0) {
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_state(sw, LV_STATE_DISABLED);
    lv_obj_set_style_opa(sw, LV_OPA_50, LV_STATE_DISABLED);
  }

  g_switches[idx] = sw;
}

}  // namespace

lv_obj_t* DesktopScreen_create() {
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

  for (int i = 0; i < (int)(sizeof(g_switches) / sizeof(g_switches[0])); i++) g_switches[i] = nullptr;
  g_statusLab = nullptr;

  StatusBar_create(scr, "桌面图标");

  lv_obj_t* back = icon_create(scr, Icon::Back, 36);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 14, 56);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(back, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(back, back_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "桌面图标");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &font_zh_24, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 58);

  lv_obj_t* cont = lv_obj_create(scr);
  lv_obj_set_size(cont, 480, 320);
  lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 96);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_scroll_dir(cont, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

  int y = 0;
  for (int i = 0; i < appreg_count(); i++) {
    const AppEntry* ent = appreg_at(i);
    if (!ent || ent->group != AppGroup::Desktop) continue;
    addRow(cont, i, y);
    y += 56;
  }

  lv_obj_t* rBtn = lv_btn_create(scr);
  lv_obj_set_size(rBtn, 200, 40);
  lv_obj_align(rBtn, LV_ALIGN_BOTTOM_MID, 0, -44);
  lv_obj_set_style_bg_opa(rBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(rBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(rBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(rBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(rBtn, 1, 0);
  lv_obj_set_style_radius(rBtn, 10, 0);
  lv_obj_add_event_cb(rBtn, reset_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(rBtn, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t* rLab = lv_label_create(rBtn);
  lv_label_set_text(rLab, "恢复默认");
  lv_obj_set_style_text_color(rLab, lv_color_white(), 0);
  lv_obj_set_style_text_font(rLab, &font_zh_16, 0);
  lv_obj_center(rLab);

  g_statusLab = lv_label_create(scr);
  lv_label_set_text(g_statusLab, "关掉的应用会从桌面消失，游戏栏目不受影响");
  lv_obj_set_style_text_color(g_statusLab, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(g_statusLab, &font_zh_16, 0);
  lv_obj_align(g_statusLab, LV_ALIGN_BOTTOM_MID, 0, -16);

  return scr;
}
