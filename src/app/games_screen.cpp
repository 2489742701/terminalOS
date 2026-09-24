#include "games_screen.h"
#include "app_registry.h"
#include "icons.h"
#include "nav.h"
#include "status_bar.h"
#include "font_zh.h"
#include <lvgl.h>
#include <Arduino.h>

/* 游戏栏目。
   磁贴几何跟 launcher 保持一致（100x110、4 列、起始 y=100），
   这样从桌面进游戏栏目时视觉上是"同一套格子换了一页内容"。 */

namespace {

struct TileData {
  lv_obj_t** target;
  Icon icon;
  const char* label;
};

TileData g_tileData[8];

void anim_opa_cb(void* var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t*)var, (lv_opa_t)v, 0);
}

void back_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
}

void swipe_cb(lv_event_t* e) {
  static SwipeState st;
  swipe_back_to_any(e, st, nav_launcher);
}

/* 点磁贴：按需创建目标 Activity 后切屏。
   ⚠️ 与 launcher 那套 splash 不同 —— 这里是栏目内跳转，中间插个闪屏
      反而拖沓，直接切。 */
void tile_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (nav_is_locked()) return;
  TileData* td = (TileData*)lv_event_get_user_data(e);
  if (!td || !td->target) return;

  if (!*td->target) nav_open(td->target);
  if (!*td->target) {
    Serial.println("[Games] open activity failed (low memory?)");
    return;
  }
  nav_go_anim(*td->target, LV_SCR_LOAD_ANIM_OVER_RIGHT, 300);
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
  lv_obj_add_event_cb(tile, tile_event_cb, LV_EVENT_CLICKED, (void*)&g_tileData[idx]);

  lv_obj_t* ic = icon_create(tile, entry.icon, 52);
  lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 18);

  lv_obj_t* lab = lv_label_create(tile);
  lv_label_set_text(lab, entry.label);
  lv_obj_set_style_text_color(lab, lv_color_white(), 0);
  lv_obj_set_style_text_font(lab, &font_zh_16, 0);
  lv_obj_align(lab, LV_ALIGN_BOTTOM_MID, 0, -24);

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

lv_obj_t* GamesScreen_create() {
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

  StatusBar_create(scr, "游戏");

  lv_obj_t* back = icon_create(scr, Icon::Back, 36);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 14, 56);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(back, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(back, back_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "游戏");
  lv_obj_set_style_text_color(title, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 58);

  int idx = 0;
  for (int i = 0; i < appreg_count() && idx < (int)(sizeof(g_tileData) / sizeof(g_tileData[0])); i++) {
    const AppEntry* e = appreg_at(i);
    if (!e || e->group != AppGroup::Game) continue;
    addTile(scr, *e, idx);
    idx++;
  }

  /* 一个游戏都没有（理论上不该发生，注册表是写死的）——
     给个提示，免得进来一片黑以为是崩了。 */
  if (idx == 0) {
    lv_obj_t* empty = lv_label_create(scr);
    lv_label_set_text(empty, "还没有任何游戏");
    lv_obj_set_style_text_color(empty, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(empty, &font_zh_16, 0);
    lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
  }

  return scr;
}
