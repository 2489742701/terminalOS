#include "game_screen.h"
#include "icons.h"
#include "nav.h"
#include "font_zh.h"
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <stdio.h>

namespace {

constexpr int GRID = 20;
constexpr int CELL = 18;
constexpr int BOARD = GRID * CELL;

enum Dir { UP, DOWN, LEFT, RIGHT };

struct Point { int x, y; };

const lv_color_t COLOR_BG   = lv_color_hex(0x080808);
const lv_color_t COLOR_HEAD = lv_color_white();
const lv_color_t COLOR_BODY = lv_color_hex(0xAAAAAA);
const lv_color_t COLOR_FOOD = lv_color_hex(0xFF4444);

Point snake[GRID * GRID];
int snakeLen;
Dir dir;
Dir nextDir;
Point food;
int score;
int bestScore = 0;
Preferences prefs;
bool gameOver;
bool paused;
uint32_t lastStepMs;

lv_obj_t* g_canvas = nullptr;
lv_color_t* g_canvasBuf = nullptr;
lv_obj_t* g_scoreLab = nullptr;
lv_obj_t* g_bestLab = nullptr;
lv_obj_t* g_overLab = nullptr;
lv_obj_t* g_pauseLab = nullptr;
lv_obj_t* g_xLine1 = nullptr;
lv_obj_t* g_xLine2 = nullptr;
bool g_showX = false;
SwipeState g_swipe;
int g_tapStartX = 0, g_tapStartY = 0;

void placeFood() {
  bool ok = false;
  while (!ok) {
    food.x = rand() % GRID;
    food.y = rand() % GRID;
    ok = true;
    for (int i = 0; i < snakeLen; i++) {
      if (snake[i].x == food.x && snake[i].y == food.y) { ok = false; break; }
    }
  }
}

void resetGame() {
  snakeLen = 3;
  for (int i = 0; i < snakeLen; i++) {
    snake[i].x = GRID / 2 - i;
    snake[i].y = GRID / 2;
  }
  dir = RIGHT;
  nextDir = RIGHT;
  score = 0;
  gameOver = false;
  paused = false;
  placeFood();
  lastStepMs = millis();
}

void drawCell(int gx, int gy, lv_color_t color) {
  if (!g_canvas) return;
  int sx = gx * CELL + 1;
  int sy = gy * CELL + 1;
  int w = CELL - 2;
  for (int dy = 0; dy < w; dy++) {
    for (int dx = 0; dx < w; dx++) {
      lv_canvas_set_px_color(g_canvas, sx + dx, sy + dy, color);
    }
  }
}

void renderFull() {
  if (!g_canvas) return;
  lv_canvas_fill_bg(g_canvas, COLOR_BG, LV_OPA_COVER);
  for (int i = 0; i <= GRID; i++) {
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_hex(0x1a1a1a);
    ld.width = 1;
    lv_point_t h[2] = {{0, (lv_coord_t)(i * CELL)}, {BOARD, (lv_coord_t)(i * CELL)}};
    lv_point_t v[2] = {{(lv_coord_t)(i * CELL), 0}, {(lv_coord_t)(i * CELL), BOARD}};
    lv_canvas_draw_line(g_canvas, h, 2, &ld);
    lv_canvas_draw_line(g_canvas, v, 2, &ld);
  }
  drawCell(food.x, food.y, COLOR_FOOD);
  for (int i = 0; i < snakeLen; i++) {
    drawCell(snake[i].x, snake[i].y, i == 0 ? COLOR_HEAD : COLOR_BODY);
  }
}

void renderStep(Point oldHead, Point oldTail, bool ateFood) {
  if (!g_canvas) return;
  drawCell(oldHead.x, oldHead.y, COLOR_BODY);
  drawCell(snake[0].x, snake[0].y, COLOR_HEAD);
  if (!ateFood) {
    drawCell(oldTail.x, oldTail.y, COLOR_BG);
  } else {
    drawCell(food.x, food.y, COLOR_FOOD);
  }
}

void step() {
  if (gameOver || paused) return;
  dir = nextDir;
  Point head = snake[0];
  switch (dir) {
    case UP:    head.y--; break;
    case DOWN:  head.y++; break;
    case LEFT:  head.x--; break;
    case RIGHT: head.x++; break;
  }
  if (head.x < 0 || head.x >= GRID || head.y < 0 || head.y >= GRID) {
    gameOver = true;
    if (score > bestScore) {
      bestScore = score;
      prefs.putUInt("best", bestScore);
      if (g_bestLab) {
        char buf[16];
        snprintf(buf, sizeof(buf), "最高 %d", bestScore);
        lv_label_set_text(g_bestLab, buf);
      }
    }
    if (g_overLab) lv_label_set_text(g_overLab, "游戏结束\n滑动退出");
    return;
  }
  for (int i = 0; i < snakeLen; i++) {
    if (snake[i].x == head.x && snake[i].y == head.y) {
      gameOver = true;
      if (score > bestScore) {
        bestScore = score;
        prefs.putUInt("best", bestScore);
        if (g_bestLab) {
          char buf[16];
          snprintf(buf, sizeof(buf), "最高 %d", bestScore);
          lv_label_set_text(g_bestLab, buf);
        }
      }
      if (g_overLab) lv_label_set_text(g_overLab, "游戏结束\n滑动退出");
      return;
    }
  }
  Point oldHead = snake[0];
  Point oldTail = snake[snakeLen - 1];
  bool ate = (head.x == food.x && head.y == food.y);

  for (int i = snakeLen - 1; i > 0; i--) snake[i] = snake[i - 1];
  snake[0] = head;
  if (ate) {
    score++;
    snakeLen++;
    snake[snakeLen - 1] = oldTail;
    placeFood();
    if (g_scoreLab) {
      char buf[16];
      snprintf(buf, sizeof(buf), "分数 %d", score);
      lv_label_set_text(g_scoreLab, buf);
    }
  }
  renderStep(oldHead, oldTail, ate);
}

void back_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
}

void restart_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  resetGame();
  if (g_overLab) lv_label_set_text(g_overLab, "");
  if (g_scoreLab) lv_label_set_text(g_scoreLab, "分数 0");
  if (g_pauseLab) lv_label_set_text(g_pauseLab, "暂停");
  renderFull();
}

void pause_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  paused = !paused;
  if (g_overLab) {
    lv_label_set_text(g_overLab, paused ? "暂停\n滑动退出" : "");
  }
  if (g_pauseLab) {
    lv_label_set_text(g_pauseLab, paused ? "继续" : "暂停");
  }
}

void setDir(Dir d) {
  if (gameOver || paused) return;
  nextDir = d;
  if (nextDir == UP && dir == DOWN) nextDir = dir;
  if (nextDir == DOWN && dir == UP) nextDir = dir;
  if (nextDir == LEFT && dir == RIGHT) nextDir = dir;
  if (nextDir == RIGHT && dir == LEFT) nextDir = dir;
}

void help_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_showX = !g_showX;
  if (g_xLine1) lv_obj_set_style_opa(g_xLine1, g_showX ? LV_OPA_50 : LV_OPA_TRANSP, 0);
  if (g_xLine2) lv_obj_set_style_opa(g_xLine2, g_showX ? LV_OPA_50 : LV_OPA_TRANSP, 0);
}

void swipe_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    g_tapStartX = p.x; g_tapStartY = p.y;
  } else if (code == LV_EVENT_RELEASED) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    int dx = p.x - g_tapStartX, dy = p.y - g_tapStartY;
    if (abs(dx) < 20 && abs(dy) < 20) {
      if (gameOver || paused) return;
      int cx = 240, cy = 240;
      int tdx = p.x - cx, tdy = p.y - cy;
      if (abs(tdx) < 10 && abs(tdy) < 10) return;
      if (abs(tdx) > abs(tdy)) {
        setDir(tdx > 0 ? RIGHT : LEFT);
      } else {
        setDir(tdy > 0 ? DOWN : UP);
      }
      return;
    }
    if (g_tapStartX < 40) {
      nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
    }
  }
}

}  // namespace

lv_obj_t* GameScreen_create() {
  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_RELEASED, NULL);

  lv_obj_t* back = icon_create(scr, Icon::Back, 36);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 14, 14);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(back, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(back, back_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "贪吃蛇");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &font_zh_24, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

  g_scoreLab = lv_label_create(scr);
  lv_label_set_text(g_scoreLab, "分数 0");
  lv_obj_set_style_text_color(g_scoreLab, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_text_font(g_scoreLab, &font_zh_16, 0);
  lv_obj_align(g_scoreLab, LV_ALIGN_TOP_RIGHT, -20, 22);

  g_bestLab = lv_label_create(scr);
  prefs.begin("snake", false);
  bestScore = prefs.getUInt("best", 0);
  prefs.end();
  char bestBuf[16];
  snprintf(bestBuf, sizeof(bestBuf), "最高 %d", bestScore);
  lv_label_set_text(g_bestLab, bestBuf);
  lv_obj_set_style_text_color(g_bestLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(g_bestLab, &font_zh_16, 0);
  lv_obj_align(g_bestLab, LV_ALIGN_TOP_LEFT, 60, 22);

  g_canvas = lv_canvas_create(scr);
  void* buf = heap_caps_malloc(BOARD * BOARD * 2, MALLOC_CAP_SPIRAM);
  g_canvasBuf = (lv_color_t*)buf;
  if (buf) {
    lv_canvas_set_buffer(g_canvas, buf, BOARD, BOARD, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(g_canvas, COLOR_BG, LV_OPA_COVER);
  }
  lv_obj_clear_flag(g_canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(g_canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(g_canvas, LV_ALIGN_TOP_MID, 0, 55);

  g_overLab = lv_label_create(scr);
  lv_label_set_text(g_overLab, "");
  lv_obj_set_style_text_color(g_overLab, lv_color_hex(0xFF4444), 0);
  lv_obj_set_style_text_font(g_overLab, &font_zh_24, 0);
  lv_obj_set_style_text_align(g_overLab, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(g_overLab, LV_ALIGN_TOP_MID, 0, 200);

  static lv_point_t x1Pts[2] = {{0, 0}, {480, 480}};
  g_xLine1 = lv_line_create(scr);
  lv_line_set_points(g_xLine1, x1Pts, 2);
  lv_obj_set_style_line_color(g_xLine1, lv_color_hex(0x444444), 0);
  lv_obj_set_style_line_width(g_xLine1, 1, 0);
  lv_obj_set_style_opa(g_xLine1, LV_OPA_TRANSP, 0);

  static lv_point_t x2Pts[2] = {{480, 0}, {0, 480}};
  g_xLine2 = lv_line_create(scr);
  lv_line_set_points(g_xLine2, x2Pts, 2);
  lv_obj_set_style_line_color(g_xLine2, lv_color_hex(0x444444), 0);
  lv_obj_set_style_line_width(g_xLine2, 1, 0);
  lv_obj_set_style_opa(g_xLine2, LV_OPA_TRANSP, 0);

  auto mkBtn = [&](const char* txt, lv_event_cb_t cb, int x, int y) {
    lv_obj_t* b = lv_btn_create(scr);
    lv_obj_set_size(b, 70, 32);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x161616), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, lv_color_white(), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, &font_zh_16, 0);
    lv_obj_center(l);
    return l;
  };
  mkBtn("重置", restart_cb, 100, 438);
  g_pauseLab = mkBtn("暂停", pause_cb, 310, 438);

  lv_obj_t* helpBtn = lv_btn_create(scr);
  lv_obj_set_size(helpBtn, 36, 36);
  lv_obj_align(helpBtn, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_opa(helpBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(helpBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(helpBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(helpBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(helpBtn, 1, 0);
  lv_obj_set_style_radius(helpBtn, 18, 0);
  lv_obj_add_event_cb(helpBtn, help_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(helpBtn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t* helpLab = lv_label_create(helpBtn);
  lv_label_set_text(helpLab, "?");
  lv_obj_set_style_text_color(helpLab, lv_color_white(), 0);
  lv_obj_set_style_text_font(helpLab, &lv_font_montserrat_16, 0);
  lv_obj_center(helpLab);

  resetGame();
  renderFull();
  return scr;
}

void GameScreen_tick() {
  if (gameOver || paused || !g_canvas) return;
  if (millis() - lastStepMs >= 120) {
    lastStepMs = millis();
    step();
  }
}
