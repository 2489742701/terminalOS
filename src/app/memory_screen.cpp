#include "memory_screen.h"
#include "icons.h"
#include "nav.h"
#include "status_bar.h"
#include "font_zh.h"
#include <lvgl.h>
#include <stdlib.h>
#include <stdio.h>

namespace {

constexpr int ROWS = 3;
constexpr int COLS = 4;
constexpr int PAIRS = ROWS * COLS / 2;
constexpr int CARD_W = 90;
constexpr int CARD_H = 90;
constexpr int CARD_GAP = 10;

SwipeState g_swipe;
lv_obj_t* g_cards[ROWS * COLS];
lv_obj_t* g_cardLabels[ROWS * COLS];
lv_obj_t* g_stepLab = nullptr;
lv_obj_t* g_bestLab = nullptr;
lv_obj_t* g_msgLab = nullptr;
int g_patterns[ROWS * COLS];
bool g_matched[ROWS * COLS];
bool g_flipped[ROWS * COLS];
int g_firstIdx = -1;
int g_secondIdx = -1;
int g_steps = 0;
int g_matchedCount = 0;
bool g_busy = false;
lv_timer_t* g_flipTimer = nullptr;
/* 屏是否还活着。翻牌定时器（800ms 后回调）会在屏被销毁后触发，
   那时 g_cards[] 全是悬空指针 —— 与 clock 同一个坑。 */
static bool g_alive = false;

void swipe_cb(lv_event_t* e) {
  swipe_detect(e, g_swipe, nav_games_or_home());
}

void updateSteps() {
  if (g_stepLab) {
    char buf[16];
    snprintf(buf, sizeof(buf), "步数 %d", g_steps);
    lv_label_set_text(g_stepLab, buf);
  }
}

void showCard(int idx) {
  if (idx < 0 || idx >= ROWS * COLS) return;
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", g_patterns[idx]);
  lv_label_set_text(g_cardLabels[idx], buf);
  lv_obj_set_style_bg_color(g_cards[idx], lv_color_hex(0x222222), 0);
}

void hideCard(int idx) {
  if (idx < 0 || idx >= ROWS * COLS) return;
  lv_label_set_text(g_cardLabels[idx], "?");
  lv_obj_set_style_bg_color(g_cards[idx], lv_color_hex(0x111111), 0);
}

static void mem_delete_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  g_alive = false;
  g_flipTimer = nullptr;
  g_msgLab = nullptr;
  for (int i = 0; i < ROWS * COLS; i++) g_cards[i] = nullptr;
}

void flipTimer_cb(lv_timer_t* t) {
  (void)t;
  if (!g_alive) return;          /* 屏已销毁，g_cards[] 不可再碰 */
  if (g_flipTimer) { lv_timer_del(g_flipTimer); g_flipTimer = nullptr; }
  if (g_firstIdx >= 0 && g_secondIdx >= 0) {
    if (g_patterns[g_firstIdx] == g_patterns[g_secondIdx]) {
      g_matched[g_firstIdx] = true;
      g_matched[g_secondIdx] = true;
      g_matchedCount++;
      lv_obj_set_style_bg_color(g_cards[g_firstIdx], lv_color_hex(0x003300), 0);
      lv_obj_set_style_bg_color(g_cards[g_secondIdx], lv_color_hex(0x003300), 0);
      if (g_matchedCount == PAIRS) {
        if (g_msgLab) {
          char buf[32];
          snprintf(buf, sizeof(buf), "完成 %d 步", g_steps);
          lv_label_set_text(g_msgLab, buf);
        }
      }
    } else {
      hideCard(g_firstIdx);
      hideCard(g_secondIdx);
    }
    g_flipped[g_firstIdx] = false;
    g_flipped[g_secondIdx] = false;
    g_firstIdx = -1;
    g_secondIdx = -1;
    g_busy = false;
  }
}

void card_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_busy) return;
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (idx < 0 || idx >= ROWS * COLS) return;
  if (g_matched[idx] || g_flipped[idx]) return;

  showCard(idx);
  g_flipped[idx] = true;
  g_steps++;
  updateSteps();

  if (g_firstIdx < 0) {
    g_firstIdx = idx;
  } else {
    g_secondIdx = idx;
    g_busy = true;
    g_flipTimer = lv_timer_create(flipTimer_cb, 800, nullptr);
    lv_timer_set_repeat_count(g_flipTimer, 1);
  }
}

void resetGame() {
  for (int i = 0; i < PAIRS; i++) {
    g_patterns[i * 2] = i + 1;
    g_patterns[i * 2 + 1] = i + 1;
  }
  for (int i = ROWS * COLS - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    int tmp = g_patterns[i]; g_patterns[i] = g_patterns[j]; g_patterns[j] = tmp;
  }
  for (int i = 0; i < ROWS * COLS; i++) {
    g_matched[i] = false;
    g_flipped[i] = false;
    hideCard(i);
  }
  g_firstIdx = -1;
  g_secondIdx = -1;
  g_steps = 0;
  g_matchedCount = 0;
  g_busy = false;
  if (g_msgLab) lv_label_set_text(g_msgLab, "");
  updateSteps();
}

}  // namespace

lv_obj_t* MemoryScreen_create() {
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
  lv_obj_add_event_cb(scr, mem_delete_cb, LV_EVENT_DELETE, NULL);
  g_alive = true;

  StatusBar_create(scr, "记忆卡牌");

  g_stepLab = lv_label_create(scr);
  lv_label_set_text(g_stepLab, "步数 0");
  lv_obj_set_style_text_color(g_stepLab, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_text_font(g_stepLab, &font_zh_16, 0);
  lv_obj_align(g_stepLab, LV_ALIGN_TOP_RIGHT, -20, 22);

  int totalW = COLS * CARD_W + (COLS - 1) * CARD_GAP;
  int totalH = ROWS * CARD_H + (ROWS - 1) * CARD_GAP;
  int startX = (480 - totalW) / 2;
  int startY = 70;

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      int idx = r * COLS + c;
      int x = startX + c * (CARD_W + CARD_GAP);
      int y = startY + r * (CARD_H + CARD_GAP);
      g_cards[idx] = lv_btn_create(scr);
      lv_obj_set_size(g_cards[idx], CARD_W, CARD_H);
      lv_obj_set_pos(g_cards[idx], x, y);
      lv_obj_set_style_bg_color(g_cards[idx], lv_color_hex(0x111111), 0);
      lv_obj_set_style_bg_opa(g_cards[idx], LV_OPA_COVER, 0);
      lv_obj_set_style_border_color(g_cards[idx], lv_color_hex(0x444444), 0);
      lv_obj_set_style_border_width(g_cards[idx], 1, 0);
      lv_obj_set_style_radius(g_cards[idx], 8, 0);
      lv_obj_add_event_cb(g_cards[idx], card_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
      lv_obj_add_flag(g_cards[idx], LV_OBJ_FLAG_EVENT_BUBBLE);

      lv_obj_t* lab = lv_label_create(g_cards[idx]);
      g_cardLabels[idx] = lab;
      lv_label_set_text(lab, "?");
      lv_obj_set_style_text_color(lab, lv_color_white(), 0);
      lv_obj_set_style_text_font(lab, &lv_font_montserrat_24, 0);
      lv_obj_center(lab);
    }
  }

  g_msgLab = lv_label_create(scr);
  lv_label_set_text(g_msgLab, "");
  lv_obj_set_style_text_color(g_msgLab, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_text_font(g_msgLab, &font_zh_24, 0);
  lv_obj_align(g_msgLab, LV_ALIGN_BOTTOM_MID, 0, -16);

  resetGame();
  return scr;
}