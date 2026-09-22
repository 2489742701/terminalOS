#include "launcher.h"
#include "icons.h"
#include "nav.h"
#include "font_zh.h"
#include <lvgl.h>

namespace {

struct AppEntry {
  Icon icon;
  const char* label;
  lv_obj_t** target;
};

struct TileData {
  lv_obj_t** target;
  Icon icon;
  const char* label;
};

TileData g_tileData[12];

void anim_opa_cb(void* var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t*)var, (lv_opa_t)v, 0);
}

void splashTimer_cb(lv_timer_t* t) {
  TileData* td = (TileData*)t->user_data;
  lv_timer_del(t);
  if (td && td->target && *td->target) {
    lv_scr_load_anim(*td->target, LV_SCR_LOAD_ANIM_OVER_RIGHT, 300, 0, true);
    nav_lock_until = lv_tick_get() + 600;
  }
}

void tile_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (nav_is_locked()) return;
  TileData* td = (TileData*)lv_event_get_user_data(e);
  if (!td || !td->target || !*td->target) return;

  lv_obj_t* splash = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(splash, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(splash, 0, 0);
  lv_obj_set_style_pad_all(splash, 0, 0);
  lv_obj_clear_flag(splash, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* bigIcon = icon_create(splash, td->icon, 120);
  lv_obj_align(bigIcon, LV_ALIGN_CENTER, 0, -30);

  lv_obj_t* lab = lv_label_create(splash);
  lv_label_set_text(lab, td->label);
  lv_obj_set_style_text_color(lab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(lab, &font_zh_24, 0);
  lv_obj_align(lab, LV_ALIGN_CENTER, 0, 60);

  lv_scr_load_anim(splash, LV_SCR_LOAD_ANIM_OVER_RIGHT, 200, 0, false);
  nav_lock_until = lv_tick_get() + 500;

  lv_timer_t* timer = lv_timer_create(splashTimer_cb, 220, td);
  lv_timer_set_repeat_count(timer, 1);
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

  g_tileData[idx].target = entry.target;
  g_tileData[idx].icon = entry.icon;
  g_tileData[idx].label = entry.label;

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

  lv_obj_add_event_cb(tile, tile_event_cb, LV_EVENT_CLICKED, (void*)&g_tileData[idx]);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, tile);
  lv_anim_set_values(&a, 0, 255);
  lv_anim_set_time(&a, 300);
  lv_anim_set_delay(&a, idx * 40);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, anim_opa_cb);
  lv_anim_start(&a);
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

  AppEntry entries[] = {
      {Icon::Clock,    "时钟",   &nav_clock},
      {Icon::Settings, "设置",   &nav_settings},
      {Icon::Wifi,     "无线",   &nav_wifi},
      {Icon::Game,     "游戏",   &nav_game},
      {Icon::Browser,  "浏览器", &nav_browser},
      {Icon::Terminal, "画板",   &nav_draw},
      {Icon::Music,    "记忆",   &nav_memory},
      {Icon::Power,    "系统",   &nav_sysinfo},
      {Icon::Weather,  "天气",   &nav_weather},
  };
  for (int i = 0; i < (int)(sizeof(entries) / sizeof(entries[0])); i++) {
    addTile(scr, entries[i], i);
  }
  return scr;
}
