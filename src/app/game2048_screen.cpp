#include "game2048_screen.h"
#include "nav.h"
#include "status_bar.h"
#include "font_zh.h"
#include <lvgl.h>
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>   /* esp_random()：硬件随机数，比 rand() 那套确定性序列强 */
#include <stdio.h>
#include <string.h>

/* ══ 2048 ════════════════════════════════════════════════════════════════
 * 480x480 的正屏天然适合 4x4。棋盘 360x360（格 84 + 缝 8），
 * 上方留状态栏 + 分数行，下方留两个按钮。
 *
 * 交互跟贪吃蛇对齐：
 *   · 屏上任意处滑动 = 一步（上下左右取位移大的那个轴，阈值 24px）
 *   · 从左边缘（x<40）右滑 = 回游戏栏目
 *   · 底部「重置」/「返回」两个按钮兜底（边缘滑动不直观，得有明路）
 * ════════════════════════════════════════════════════════════════════════ */

namespace {

constexpr int N = 4;
constexpr int CELL = 84;
constexpr int GAP = 8;
constexpr int BOARD_PX = N * CELL + (N - 1) * GAP;   // 360
constexpr int BOARD_X = (480 - BOARD_PX) / 2;        // 60
constexpr int BOARD_Y = 60;

/* 方向：0=左 1=右 2=上 3=下 */
enum { MV_LEFT = 0, MV_RIGHT = 1, MV_UP = 2, MV_DOWN = 3 };

int g_board[N][N];
int g_score = 0;
int g_best = 0;
bool g_over = false;
bool g_won = false;
Preferences g_prefs;

lv_obj_t* g_cells[N][N];
lv_obj_t* g_labs[N][N];
lv_obj_t* g_scoreLab = nullptr;
lv_obj_t* g_bestLab = nullptr;
lv_obj_t* g_msgLab = nullptr;
SwipeState g_backSwipe;   /* 边缘返回专用（与走棋的 g_startX/Y 分开，互不干扰） */
bool g_backFired = false; /* 本次手势已被"返回"消费掉 -> RELEASED 不再走棋 */
int g_startX = 0, g_startY = 0;

/* 值 -> 底色 / 字色。整体压暗，跟整机黑底一套语言（白底亮色块在这个屏上很刺眼） */
struct TileStyle {
  uint32_t bg;
  uint32_t fg;
};

TileStyle styleFor(int v) {
  switch (v) {
    case 2:    return {0x232323, 0xDDDDDD};
    case 4:    return {0x2e2a24, 0xDDDDDD};
    case 8:    return {0x7a4a1e, 0xFFFFFF};
    case 16:   return {0x93541c, 0xFFFFFF};
    case 32:   return {0xa63a1c, 0xFFFFFF};
    case 64:   return {0xbf2a14, 0xFFFFFF};
    case 128:  return {0xc08a10, 0x1a1a1a};
    case 256:  return {0xc89a0c, 0x1a1a1a};
    case 512:  return {0xd0a800, 0x1a1a1a};
    case 1024: return {0xd8b800, 0x1a1a1a};
    case 2048: return {0xe8cc00, 0x1a1a1a};
    default:   return {0xf0dc40, 0x1a1a1a};   /* >2048 */
  }
}

/* ── 一行向左压缩合并。
   ⚠️ mergedLast 是必须的：4 4 4 4 向左应得 8 8，不是 16。
      没有这个标记就会出现"一步连并两次"。 */
bool slideLine(int in[N], int* gain) {
  int out[N] = {0, 0, 0, 0};
  int n = 0;
  bool mergedLast = false;
  for (int i = 0; i < N; i++) {
    if (in[i] == 0) continue;
    if (n > 0 && !mergedLast && out[n - 1] == in[i]) {
      out[n - 1] *= 2;
      *gain += out[n - 1];
      mergedLast = true;
    } else {
      out[n++] = in[i];
      mergedLast = false;
    }
  }
  bool changed = false;
  for (int i = 0; i < N; i++) {
    if (in[i] != out[i]) changed = true;
    in[i] = out[i];
  }
  return changed;
}

/* 四个方向复用同一套行逻辑：把要处理的那一条"抽成向左的一行"，处理完再写回去 */
bool moveBoard(int dir) {
  bool moved = false;
  int gain = 0;
  for (int k = 0; k < N; k++) {
    int line[N];
    for (int i = 0; i < N; i++) {
      switch (dir) {
        case MV_LEFT:  line[i] = g_board[k][i];           break;  // 行 k，左→右
        case MV_RIGHT: line[i] = g_board[k][N - 1 - i];   break;  // 行 k，右→左
        case MV_UP:    line[i] = g_board[i][k];           break;  // 列 k，上→下
        case MV_DOWN:  line[i] = g_board[N - 1 - i][k];   break;  // 列 k，下→上
      }
    }
    if (slideLine(line, &gain)) moved = true;
    for (int i = 0; i < N; i++) {
      switch (dir) {
        case MV_LEFT:  g_board[k][i] = line[i];           break;
        case MV_RIGHT: g_board[k][N - 1 - i] = line[i];   break;
        case MV_UP:    g_board[i][k] = line[i];           break;
        case MV_DOWN:  g_board[N - 1 - i][k] = line[i];   break;
      }
    }
  }
  g_score += gain;
  return moved;
}

int emptyCells(int* out) {
  int n = 0;
  for (int r = 0; r < N; r++) {
    for (int c = 0; c < N; c++) {
      if (g_board[r][c] == 0) out[n++] = r * N + c;
    }
  }
  return n;
}

void spawn() {
  int empties[N * N];
  int n = emptyCells(empties);
  if (n == 0) return;
  int idx = empties[(int)(esp_random() % (uint32_t)n)];
  g_board[idx / N][idx % N] = (esp_random() % 10 == 0) ? 4 : 2;
}

bool canMove() {
  for (int r = 0; r < N; r++) {
    for (int c = 0; c < N; c++) {
      if (g_board[r][c] == 0) return true;
      if (c + 1 < N && g_board[r][c] == g_board[r][c + 1]) return true;
      if (r + 1 < N && g_board[r][c] == g_board[r + 1][c]) return true;
    }
  }
  return false;
}

int maxTile() {
  int m = 0;
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++)
      if (g_board[r][c] > m) m = g_board[r][c];
  return m;
}

void renderCell(int r, int c) {
  lv_obj_t* box = g_cells[r][c];
  lv_obj_t* lab = g_labs[r][c];
  if (!box || !lab) return;
  int v = g_board[r][c];
  if (v == 0) {
    lv_obj_set_style_bg_color(box, lv_color_hex(0x141414), 0);
    lv_label_set_text(lab, "");
    return;
  }
  TileStyle st = styleFor(v);
  lv_obj_set_style_bg_color(box, lv_color_hex(st.bg), 0);
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", v);
  lv_label_set_text(lab, buf);
  lv_obj_set_style_text_color(lab, lv_color_hex(st.fg), 0);
  /* 48px 只够 3 位数（"512" ≈ 78px < 84）；4 位及以上退回 20px。
     ⚠️ lv_conf 里 MONTSERRAT_24/32 都是 0，实际能用的是 14/16/18/20/48。 */
  lv_obj_set_style_text_font(lab,
      strlen(buf) <= 3 ? &lv_font_montserrat_48 : &lv_font_montserrat_20, 0);
}

void renderAll() {
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) renderCell(r, c);
}

/* ── 走子动画（master 2026-09-25：2048 太"硬"）─────────────────────────
 * 值变了的格子（合并出来的 + 新生成的）弹一下：62% -> 100%，带一点点回弹。
 * 只动 style 的 transform_zoom，不新建对象 —— 这个屏常驻时要省 DRAM。
 * ⚠️ 关动画时必须把 zoom 复位成 256：上一次动画可能停在中间值，
 *    不复位的话格子会一直缩着。 */
static void zoom_exec(void* obj, int32_t v) {
  lv_obj_set_style_transform_zoom((lv_obj_t*)obj, (lv_coord_t)v, 0);
}
static void popCell(int r, int c) {
  lv_obj_t* box = g_cells[r][c];
  if (!box || !lv_obj_is_valid(box)) return;
  if (!ui_anim()) { lv_obj_set_style_transform_zoom(box, 256, 0); return; }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, box);
  lv_anim_set_exec_cb(&a, zoom_exec);
  lv_anim_set_values(&a, 160, 256);
  lv_anim_set_time(&a, 190);
  lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
  lv_anim_start(&a);
}

/* 中央提示：空串 = 隐藏。不隐藏的话那块半透明底板会一直压在棋盘上 */
void setMsg(const char* t) {
  if (!g_msgLab) return;
  lv_label_set_text(g_msgLab, t ? t : "");
  if (t && t[0]) lv_obj_clear_flag(g_msgLab, LV_OBJ_FLAG_HIDDEN);
  else           lv_obj_add_flag(g_msgLab, LV_OBJ_FLAG_HIDDEN);
}

void saveBest() {
  if (g_score <= g_best) return;
  g_best = g_score;
  g_prefs.begin("g2048", false);
  g_prefs.putUInt("best", (uint32_t)g_best);
  g_prefs.end();
  if (g_bestLab) {
    char buf[24];
    snprintf(buf, sizeof(buf), "最高 %d", g_best);
    lv_label_set_text(g_bestLab, buf);
  }
}

void updateScoreLab() {
  if (!g_scoreLab) return;
  char buf[24];
  snprintf(buf, sizeof(buf), "分数 %d", g_score);
  lv_label_set_text(g_scoreLab, buf);
}

void resetGame() {
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) g_board[r][c] = 0;
  g_score = 0;
  g_over = false;
  g_won = false;
  spawn();
  spawn();
  updateScoreLab();
  setMsg("");
  renderAll();
}

void doMove(int dir) {
  if (g_over) return;
  int prev[N][N];
  memcpy(prev, g_board, sizeof(prev));
  if (!moveBoard(dir)) return;      // 没动就不生成新块（否则原地滑也能刷出块）
  spawn();
  updateScoreLab();
  saveBest();
  renderAll();

  /* 弹一下所有"变了"的格子：合并出来的（值变大）和新生成的（原来是 0） */
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++)
      if (g_board[r][c] != 0 && g_board[r][c] != prev[r][c]) popCell(r, c);

  if (!g_won && maxTile() >= 2048) {
    g_won = true;
    setMsg("达成 2048！\n继续合并还能更高");
    return;
  }
  if (!canMove()) {
    g_over = true;
    setMsg("无路可走\n重置 或 滑动返回");
  }
}

void restart_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  resetGame();
}

void back_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  nav_go_anim(nav_games_or_home(), LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
}

void swipe_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) g_backFired = false;
  /* ① 左边缘滑入返回 —— 用统一手势（nav.h::swipe_back_detect）。
     触发后置 g_backFired，RELEASED 时不再把它当成走棋。 */
  if (swipe_back_detect(e, g_backSwipe)) {
    g_backFired = true;
    nav_go_anim(nav_games_or_home(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300);
    return;
  }
  if (code == LV_EVENT_PRESSED) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    g_startX = p.x;
    g_startY = p.y;
  } else if (code == LV_EVENT_RELEASED) {
    if (g_backFired) return;      // 这次手势是"返回"，不是走棋
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    int dx = p.x - g_startX, dy = p.y - g_startY;

    if (abs(dx) < 24 && abs(dy) < 24) return;   // 当成点击，不走棋
    if (abs(dx) > abs(dy)) doMove(dx > 0 ? MV_RIGHT : MV_LEFT);
    else                   doMove(dy > 0 ? MV_DOWN : MV_UP);
  }
}

lv_obj_t* mkBtn(lv_obj_t* scr, const char* txt, lv_event_cb_t cb, int x, int y) {
  lv_obj_t* b = lv_btn_create(scr);
  lv_obj_set_size(b, 90, 34);
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
  return b;
}

}  // namespace

lv_obj_t* Game2048Screen_create() {
  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSING, NULL);   /* 边缘返回要在移动中判定 */
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_RELEASED, NULL);

  StatusBar_create(scr, "2048");

  g_prefs.begin("g2048", false);
  g_best = (int)g_prefs.getUInt("best", 0);
  g_prefs.end();

  /* 分数行放在状态栏下方（y=34），不跟状态栏的时钟/图标抢位置 */
  g_bestLab = lv_label_create(scr);
  char bestBuf[24];
  snprintf(bestBuf, sizeof(bestBuf), "最高 %d", g_best);
  lv_label_set_text(g_bestLab, bestBuf);
  lv_obj_set_style_text_color(g_bestLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(g_bestLab, &font_zh_16, 0);
  lv_obj_align(g_bestLab, LV_ALIGN_TOP_LEFT, 16, 34);

  g_scoreLab = lv_label_create(scr);
  lv_label_set_text(g_scoreLab, "分数 0");
  lv_obj_set_style_text_color(g_scoreLab, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_text_font(g_scoreLab, &font_zh_16, 0);
  lv_obj_align(g_scoreLab, LV_ALIGN_TOP_RIGHT, -16, 34);

  for (int r = 0; r < N; r++) {
    for (int c = 0; c < N; c++) {
      lv_obj_t* box = lv_obj_create(scr);
      lv_obj_set_size(box, CELL, CELL);
      lv_obj_set_pos(box, BOARD_X + c * (CELL + GAP), BOARD_Y + r * (CELL + GAP));
      lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(box, lv_color_hex(0x141414), 0);
      lv_obj_set_style_border_width(box, 0, 0);
      lv_obj_set_style_radius(box, 8, 0);
      lv_obj_set_style_pad_all(box, 0, 0);
      lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_flag(box, LV_OBJ_FLAG_EVENT_BUBBLE);   /* 格子上滑动也要冒泡到屏 */

      lv_obj_t* lab = lv_label_create(box);
      lv_label_set_text(lab, "");
      lv_obj_center(lab);

      g_cells[r][c] = box;
      g_labs[r][c] = lab;
    }
  }

  /* 提示浮在棋盘正中（跟贪吃蛇的"游戏结束"一个位置）。
     ⚠️ 别放到底部：420 以下只剩 60px，还要塞两个按钮，会叠在一起。 */
  g_msgLab = lv_label_create(scr);
  lv_label_set_text(g_msgLab, "");
  lv_obj_set_style_text_color(g_msgLab, lv_color_hex(0xFFCC33), 0);
  lv_obj_set_style_text_font(g_msgLab, &font_zh_16, 0);
  lv_obj_set_style_text_align(g_msgLab, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_color(g_msgLab, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_msgLab, LV_OPA_80, 0);
  lv_obj_set_style_pad_all(g_msgLab, 8, 0);
  lv_obj_set_style_radius(g_msgLab, 8, 0);
  lv_obj_set_width(g_msgLab, 400);
  lv_obj_align(g_msgLab, LV_ALIGN_TOP_MID, 0, 200);
  lv_obj_add_flag(g_msgLab, LV_OBJ_FLAG_HIDDEN);   /* 有话要说才显形 */

  mkBtn(scr, "重置", restart_cb, 120, 436);
  mkBtn(scr, "返回", back_cb, 270, 436);

  resetGame();
  return scr;
}
