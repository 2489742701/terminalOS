#include "launcher.h"
#include "icons.h"
#include "nav.h"
#include "font_zh.h"
#include <lvgl.h>

namespace {

struct AppEntry {
  Icon icon;
  const char* label;
  lv_obj_t** target;  // 指向 nav_xxx 全局指针
};

void tile_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (nav_is_locked()) return;
  lv_obj_t** pscr = (lv_obj_t**)lv_event_get_user_data(e);
  if (pscr && *pscr) nav_go_anim(*pscr, LV_SCR_LOAD_ANIM_OVER_RIGHT, 300);
}

void addTile(lv_obj_t* scr, const AppEntry& entry, int idx) {
  const int tileW = 100, tileH = 110, gap = 12;
  const int cols = 4;
  const int startX = (480 - (cols * tileW + (cols - 1) * gap)) / 2;
  const int startY = 100;
  int col = idx % cols;
  int row = idx / cols;
  int x = startX + col * (tileW + gap);
  int y = startY + row * (tileH + gap);

  lv_obj_t* tile = lv_obj_create(scr);
  lv_obj_set_size(tile, tileW, tileH);
  lv_obj_set_pos(tile, x, y);
  lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tile, 0, 0);
  lv_obj_set_style_pad_all(tile, 0, 0);
  lv_obj_set_style_radius(tile, 16, 0);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(tile, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* ic = icon_create(tile, entry.icon, 52);
  lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 18);

  lv_obj_t* lab = lv_label_create(tile);
  lv_label_set_text(lab, entry.label);
  lv_obj_set_style_text_color(lab, lv_color_white(), 0);
  lv_obj_set_style_text_font(lab, &font_zh_16, 0);
  lv_obj_align(lab, LV_ALIGN_BOTTOM_MID, 0, -24);

  // event user_data 存指向 nav_xxx 全局指针的指针，点击时解引用取实时目标屏
  lv_obj_add_event_cb(tile, tile_event_cb, LV_EVENT_CLICKED, (void*)entry.target);
}

}  // namespace

lv_obj_t* LauncherScreen_create() {
  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "GEEK TERMINAL");
  lv_obj_set_style_text_color(title, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 56);

  // v1 应用入口：时钟 + 设置 + WiFi + 游戏 + 浏览器
  AppEntry entries[] = {
      {Icon::Clock,   "时钟",   &nav_clock},
      {Icon::Settings, "设置",   &nav_settings},
      {Icon::Wifi,    "无线",   &nav_wifi},
      {Icon::Game,    "游戏",   &nav_game},
      {Icon::Browser, "浏览器", &nav_browser},
      {Icon::Terminal, "画板",   &nav_draw},
      {Icon::Music,   "记忆",   &nav_memory},
      {Icon::Power,   "系统",   &nav_sysinfo},
      {Icon::Weather,  "天气",   &nav_weather},
  };
  for (int i = 0; i < (int)(sizeof(entries) / sizeof(entries[0])); i++) {
    addTile(scr, entries[i], i);
  }
  return scr;
}
