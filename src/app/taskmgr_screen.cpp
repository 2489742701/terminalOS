#include "taskmgr_screen.h"

#include "app_registry.h"
#include "font_zh.h"
#include "icons.h"
#include "nav.h"
#include "status_bar.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

const int MAX_ROWS = 16;

SwipeState g_swipe;
lv_obj_t* g_list = nullptr;
lv_obj_t* g_memLab = nullptr;
uint32_t g_lastUpdate = 0;

/* 每行的 id 指向 nav.cpp 表里那个 const char*（静态生命周期），
   拿它当事件 user_data 是安全的 —— 别去指栈上的东西。 */
const char* g_rowIds[MAX_ROWS];
int g_rowCount = 0;

/* 关应用必须延迟一拍：按钮的 CLICKED 回调里如果直接重建列表，
   lv_obj_clean 掉的就是**正在派发事件的按钮自己**。项目铁律。 */
char g_pendingId[16] = {0};
bool g_pendingKillAll = false;
bool g_pendingSet = false;

void swipe_cb(lv_event_t* e) { swipe_back_to_any(e, g_swipe, nav_launcher); }

void rebuild();

void kill_timer_cb(lv_timer_t* t) {
  lv_timer_del(t);
  if (!g_pendingSet) return;
  g_pendingSet = false;
  if (g_pendingKillAll) {
    int n = nav_close_all();
    Serial.printf("[TaskMgr] close all: %d\n", n);
  } else if (g_pendingId[0]) {
    Serial.printf("[TaskMgr] close %s: %s\n", g_pendingId,
                  nav_close(g_pendingId) ? "ok" : "refused");
  }
  g_pendingKillAll = false;
  g_pendingId[0] = 0;
  rebuild();
}

void schedule_kill(const char* id, bool all) {
  if (g_pendingSet) return;
  g_pendingSet = true;
  g_pendingKillAll = all;
  if (id) {
    snprintf(g_pendingId, sizeof(g_pendingId), "%s", id);
  } else {
    g_pendingId[0] = 0;
  }
  lv_timer_t* t = lv_timer_create(kill_timer_cb, 1, NULL);
  if (t) lv_timer_set_repeat_count(t, 1);
}

void kill_btn_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char* id = (const char*)lv_event_get_user_data(e);
  if (!id) return;
  schedule_kill(id, false);
}

void kill_all_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  schedule_kill(nullptr, true);
}

/* 一行 = 一个还在内存里的 Activity */
void addRow(lv_obj_t* parent, const NavRunningInfo& info, int idx) {
  g_rowIds[idx] = info.id;

  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, 60);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(0x2a2a2a), 0);
  lv_obj_set_style_radius(row, 8, 0);
  lv_obj_set_style_pad_all(row, 8, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  /* 显示名走应用注册表（有中文名）；注册表里没有的（子游戏等）就显示 id */
  const char* label = info.id;
  int ri = appreg_find(info.id);
  const AppEntry* e = appreg_at(ri);
  if (e && e->label) label = e->label;

  lv_obj_t* name = lv_label_create(row);
  lv_label_set_text(name, label);
  lv_obj_set_style_text_color(name, lv_color_white(), 0);
  lv_obj_set_style_text_font(name, &font_zh_16, 0);
  lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, -10);

  char sub[48];
  if (info.bytes >= 1024) {
    snprintf(sub, sizeof(sub), "占用 %.1f KB", info.bytes / 1024.0f);
  } else if (info.bytes > 0) {
    snprintf(sub, sizeof(sub), "占用 %u B", (unsigned)info.bytes);
  } else {
    strcpy(sub, "常驻 / 未计量");
  }
  lv_obj_t* subLab = lv_label_create(row);
  lv_label_set_text(subLab, sub);
  lv_obj_set_style_text_color(subLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(subLab, &lv_font_montserrat_14, 0);
  lv_obj_align(subLab, LV_ALIGN_LEFT_MID, 0, 12);

  lv_obj_t* btn = lv_btn_create(row);
  lv_obj_set_size(btn, 72, 36);
  lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x552222), 0);
  lv_obj_set_style_radius(btn, 8, 0);

  lv_obj_t* bl = lv_label_create(btn);
  lv_obj_set_style_text_font(bl, &font_zh_16, 0);
  lv_obj_center(bl);

  if (info.current) {
    /* 前台：不能关（删当前屏必崩），按钮画成灰色说明原因 */
    lv_label_set_text(bl, "前台");
    lv_obj_set_style_text_color(bl, lv_color_hex(0x999999), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
    lv_obj_add_state(btn, LV_STATE_DISABLED);
  } else {
    lv_label_set_text(bl, "结束");
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_add_event_cb(btn, kill_btn_cb, LV_EVENT_CLICKED, (void*)g_rowIds[idx]);
  }
}

void rebuild() {
  if (!g_list || !lv_obj_is_valid(g_list)) return;
  lv_obj_clean(g_list);

  NavRunningInfo infos[MAX_ROWS];
  int n = nav_running_list(infos, MAX_ROWS);
  g_rowCount = n;

  if (n <= 0) {
    lv_obj_t* empty = lv_label_create(g_list);
    lv_label_set_text(empty, "没有后台应用");
    lv_obj_set_style_text_color(empty, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(empty, &font_zh_16, 0);
    return;
  }
  for (int i = 0; i < n; i++) addRow(g_list, infos[i], i);
}

void updateMem() {
  if (!g_memLab || !lv_obj_is_valid(g_memLab)) return;
  uint32_t dram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  char buf[64];
  /* ⚠️ 只查 free size。运行时不要碰 get_largest_free_block / get_info：
     它们会持堆锁，跟 WiFi 驱动抢，实测触发 wdt。 */
  snprintf(buf, sizeof(buf), "运行 %d 个 · DRAM 空闲 %u KB", g_rowCount,
           (unsigned)(dram / 1024));
  lv_label_set_text(g_memLab, buf);
}

}  // namespace

lv_obj_t* TaskMgrScreen_create() {
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

  StatusBar_create(scr, "后台管理");

  lv_obj_t* hint = lv_label_create(scr);
  lv_label_set_text(hint, "应用按需启动；下面列出仍占内存的应用");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x777777), 0);
  lv_obj_set_style_text_font(hint, &font_zh_16, 0);
  lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 16, 52);

  g_list = lv_obj_create(scr);
  lv_obj_set_size(g_list, 448, 320);
  lv_obj_align(g_list, LV_ALIGN_TOP_LEFT, 16, 80);
  lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(g_list, 8, 0);
  lv_obj_set_style_pad_all(g_list, 0, 0);
  lv_obj_set_style_border_width(g_list, 0, 0);
  lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
  lv_obj_set_scroll_dir(g_list, LV_DIR_VER);

  g_memLab = lv_label_create(scr);
  lv_label_set_text(g_memLab, "");
  lv_obj_set_style_text_color(g_memLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(g_memLab, &lv_font_montserrat_14, 0);
  lv_obj_align(g_memLab, LV_ALIGN_BOTTOM_LEFT, 16, -22);

  lv_obj_t* all = lv_btn_create(scr);
  lv_obj_set_size(all, 120, 44);
  lv_obj_align(all, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
  lv_obj_set_style_bg_color(all, lv_color_hex(0x552222), 0);
  lv_obj_set_style_bg_color(all, lv_color_hex(0x883333), LV_STATE_PRESSED);
  lv_obj_set_style_radius(all, 8, 0);
  lv_obj_add_event_cb(all, kill_all_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* al = lv_label_create(all);
  lv_label_set_text(al, "全部结束");
  lv_obj_set_style_text_color(al, lv_color_white(), 0);
  lv_obj_set_style_text_font(al, &font_zh_16, 0);
  lv_obj_center(al);

  rebuild();
  updateMem();
  return scr;
}

void TaskMgrScreen_tick() {
  if (millis() - g_lastUpdate > 2000) {
    g_lastUpdate = millis();
    updateMem();
  }
}
