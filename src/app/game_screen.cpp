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
constexpr int CELL = 14;
constexpr int BOARD = GRID * CELL;

constexpr int DPAD_SIZE = 110;

enum Dir { UP, DOWN, LEFT, RIGHT };

struct Point { int x, y; };

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
lv_obj_t* g_scoreLab = nullptr;
lv_obj_t* g_bestLab = nullptr;
lv_obj_t* g_overLab = nullptr;
lv_obj_t* g_dpad = nullptr;
lv_obj_t* g_pauseLab = nullptr;
bool g_showGuide = true;
SwipeState g_swipe;
int g_swipeStartX = 0, g_swipeStartY = 0;

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
  lv_canvas_set_px_color(g_canvas, gx * CELL + 1, gy * CELL + 1, color);
  for (int dy = 0; dy < CELL - 2; dy++) {
    for (int dx = 0; dx < CELL - 2; dx++) {
      lv_canvas_set_px_color(g_canvas, gx * CELL + 1 + dx, gy * CELL + 1 + dy, color);
    }
  }
}

void render() {
  if (!g_canvas) return;
  lv_canvas_fill_bg(g_canvas, lv_color_hex(0x080808), LV_OPA_COVER);
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
  drawCell(food.x, food.y, lv_color_hex(0xFF4444));
  for (int i = 0; i < snakeLen; i++) {
    drawCell(snake[i].x, snake[i].y, i == 0 ? lv_color_white() : lv_color_hex(0xAAAAAA));
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
  for (int i = snakeLen - 1; i > 0; i--) snake[i] = snake[i - 1];
  snake[0] = head;
  if (head.x == food.x && head.y == food.y) {
    score++;
    snakeLen++;
    snake[snakeLen - 1] = snake[snakeLen - 2];
    placeFood();
    if (g_scoreLab) {
      char buf[16];
      snprintf(buf, sizeof(buf), "分数 %d", score);
      lv_label_set_text(g_scoreLab, buf);
    }
  }
  render();
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
  render();
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

void dpad_draw() {
  if (!g_dpad) return;
  lv_canvas_fill_bg(g_dpad, lv_color_hex(0x1a1a1a), LV_OPA_COVER);
  if (g_showGuide) {
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_hex(0x666666);
    ld.width = 1;
    lv_point_t d1[2] = {{0, 0}, {DPAD_SIZE, DPAD_SIZE}};
    lv_point_t d2[2] = {{DPAD_SIZE, 0}, {0, DPAD_SIZE}};
    lv_canvas_draw_line(g_dpad, d1, 2, &ld);
    lv_canvas_draw_line(g_dpad, d2, 2, &ld);
  }
}

void dpad_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (gameOver || paused) return;
  lv_point_t p;
  lv_indev_get_point(lv_indev_get_act(), &p);
  int cx = lv_obj_get_x(g_dpad) + DPAD_SIZE / 2;
  int cy = lv_obj_get_y(g_dpad) + DPAD_SIZE / 2;
  int dx = p.x - cx, dy = p.y - cy;
  if (abs(dx) < 5 && abs(dy) < 5) return;
  if (abs(dx) > abs(dy)) {
    setDir(dx > 0 ? RIGHT : LEFT);
  } else {
    setDir(dy > 0 ? DOWN : UP);
  }
}

void help_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_showGuide = !g_showGuide;
  dpad_draw();
}

void swipe_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    g_swipeStartX = p.x; g_swipeStartY = p.y;
  } else if (code == LV_EVENT_RELEASED) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    int dx = p.x - g_swipeStartX, dy = p.y - g_swipeStartY;
    if (abs(dx) < 20 && abs(dy) < 20) return;
    if (gameOver || paused) {
      nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
      return;
    }
    if (abs(dx) > abs(dy)) {
      setDir(dx > 0 ? RIGHT : LEFT);
    } else {
      setDir(dy > 0 ? DOWN : UP);
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
  if (buf) {
    lv_canvas_set_buffer(g_canvas, buf, BOARD, BOARD, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(g_canvas, lv_color_hex(0x080808), LV_OPA_COVER);
  }
  lv_obj_align(g_canvas, LV_ALIGN_TOP_MID, 0, 55);

  g_overLab = lv_label_create(scr);
  lv_label_set_text(g_overLab, "");
  lv_obj_set_style_text_color(g_overLab, lv_color_hex(0xFF4444), 0);
  lv_obj_set_style_text_font(g_overLab, &font_zh_24, 0);
  lv_obj_set_style_text_align(g_overLab, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(g_overLab, LV_ALIGN_TOP_MID, 0, 170);

  g_dpad = lv_canvas_create(scr);
  void* dpadBuf = heap_caps_malloc(DPAD_SIZE * DPAD_SIZE * 2, MALLOC_CAP_SPIRAM);
  if (dpadBuf) {
    lv_canvas_set_buffer(g_dpad, dpadBuf, DPAD_SIZE, DPAD_SIZE, LV_IMG_CF_TRUE_COLOR);
    dpad_draw();
  }
  lv_obj_set_pos(g_dpad, 50, 345);
  lv_obj_add_flag(g_dpad, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_dpad, dpad_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* helpBtn = lv_btn_create(scr);
  lv_obj_set_size(helpBtn, 36, 36);
  lv_obj_set_pos(helpBtn, 175, 382);
  lv_obj_set_style_bg_opa(helpBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(helpBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(helpBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(helpBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(helpBtn, 1, 0);
  lv_obj_set_style_radius(helpBtn, 18, 0);
  lv_obj_add_event_cb(helpBtn, help_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* helpLab = lv_label_create(helpBtn);
  lv_label_set_text(helpLab, "?");
  lv_obj_set_style_text_color(helpLab, lv_color_white(), 0);
  lv_obj_set_style_text_font(helpLab, &lv_font_montserrat_16, 0);
  lv_obj_center(helpLab);

  auto mkBtn = [&](const char* txt, lv_event_cb_t cb, int x, int y) {
    lv_obj_t* b = lv_btn_create(scr);
    lv_obj_set_size(b, 80, 34);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x161616), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, lv_color_white(), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, &font_zh_16, 0);
    lv_obj_center(l);
    return l;
  };
  mkBtn("重置", restart_cb, 275, 350);
  g_pauseLab = mkBtn("暂停", pause_cb, 275, 394);

  resetGame();
  render();
  return scr;
}

void GameScreen_tick() {
  if (gameOver || paused || !g_canvas) return;
  if (millis() - lastStepMs >= 150) {
    lastStepMs = millis();
    step();
  }
}
