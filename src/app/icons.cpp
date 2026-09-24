#include "icons.h"
#include <math.h>
#include <esp_heap_caps.h>

namespace {

const float kPi = 3.14159265f;

lv_point_t P(int cx, int cy, int r, int deg) {
  float a = deg * kPi / 180.0f;
  lv_point_t p;
  p.x = (lv_coord_t)(cx + r * cosf(a));
  p.y = (lv_coord_t)(cy + r * sinf(a));
  return p;
}

/* 信号格「未点亮」用的灰色：必须看得见，只是压暗，不能透明（master 要求）。
   0x555555 太暗了 —— 22px 图标里线宽只有 1~2px，弱信号时三道弧几乎看不见，
   看着像"没连上"。提到 0x8A8A8A：与白色点亮态仍有明确对比，但暗弧清晰可辨。 */
static lv_color_t dim_color() { return lv_color_hex(0x8A8A8A); }

/* 画 WiFi：3 道同心弧 + 中心点。level=0..3 表示点亮几道弧（3=满格）。
   未点亮的弧仍然画出来，只是用暗色，这样格子形状始终可见。

   几何（相对画布边长 S）：
     扇心 y = 0.70·S，外弧半径 0.48·S，三弧同开 96°（222°→318°，正对上方）
     → 包围盒 高 0.56·S / 宽 0.71·S，垂直居中在 0.50·S。
   旧版把扇心压到 0.75·S、弧度开到 128°，弧被拉成"笑脸"，且包围盒中心
   落在 0.61·S —— 看起来又矮又往下滑。改这两处即可，不用改配色逻辑。 */
/* 用折线逼近圆弧。
   为什么不用 lv_canvas_draw_arc：vendored LVGL 的 lv_draw_sw_arc 在内半径退化时
   会无条件 lv_draw_mask_free_param() 一个**未初始化**的栈变量
   （lv_draw_sw_arc.c:168；全圆环分支 line 121 早先修过，部分圆弧分支漏了）→
   释放野指针 → LoadProhibited panic。WiFi 图标放大到 22px 后稳定复现。
   折线完全不碰 mask 半径缓存，没有这条退化路径，而且线宽/圆端点更可控。 */
static void arc_polyline(lv_obj_t* canvas, int cx, int cy, int r,
                         int a0, int a1, lv_draw_line_dsc_t* ld) {
  const int N = 14;
  lv_point_t pts[N + 1];
  for (int i = 0; i <= N; i++) {
    float deg = a0 + (a1 - a0) * (float)i / (float)N;
    float rad = deg * 3.14159265f / 180.0f;
    pts[i].x = (lv_coord_t)(cx + r * cosf(rad) + 0.5f);
    pts[i].y = (lv_coord_t)(cy + r * sinf(rad) + 0.5f);
  }
  lv_canvas_draw_line(canvas, pts, N + 1, ld);
}

/* 实心圆点（用于「更多」的三点、门把手等）。
   ⚠️ 不用 lv_canvas_draw_arc(0,360)：vendored LVGL 的整圆分支早先修过，但半径
      退化时仍有踩未初始化 mask 参数的风险（见 drawWifi 注释）。用「半径=半边长」
      的圆角矩形画圆最稳，语义等价且没有退化路径。 */
static void draw_dot(lv_obj_t* canvas, int cx, int cy, int r, lv_color_t color) {
  lv_draw_rect_dsc_t rd;
  lv_draw_rect_dsc_init(&rd);
  rd.bg_color = color;
  rd.bg_opa = LV_OPA_COVER;
  rd.border_width = 0;
  rd.radius = (lv_coord_t)r;
  lv_canvas_draw_rect(canvas, cx - r, cy - r, (lv_coord_t)(r * 2),
                      (lv_coord_t)(r * 2), &rd);
}

/* 直线箭头：杆 (x0,y0)→(x1,y1) + 终点箭头。
   两翼由「沿 -方向回退 + 法向偏移」算出，换方向不用重推三角函数。 */
static void draw_arrow(lv_obj_t* canvas, int x0, int y0, int x1, int y1,
                       int head, lv_draw_line_dsc_t* ld) {
  float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.5f) return;
  dx /= len;
  dy /= len;
  float nx = -dy, ny = dx;

  lv_point_t shaft[2] = {{(lv_coord_t)x0, (lv_coord_t)y0},
                         {(lv_coord_t)x1, (lv_coord_t)y1}};
  lv_canvas_draw_line(canvas, shaft, 2, ld);

  lv_point_t h[3] = {
      {(lv_coord_t)(x1 - dx * head + nx * head * 0.62f),
       (lv_coord_t)(y1 - dy * head + ny * head * 0.62f)},
      {(lv_coord_t)x1, (lv_coord_t)y1},
      {(lv_coord_t)(x1 - dx * head - nx * head * 0.62f),
       (lv_coord_t)(y1 - dy * head - ny * head * 0.62f)}};
  lv_canvas_draw_line(canvas, h, 3, ld);
}

/* 实心三角形。比 draw_arrow 更醒目，且没有"杆"在细尺寸下糊成一团的毛病。 */
static void draw_tri(lv_obj_t* canvas, int x0, int y0, int x1, int y1, int x2,
                     int y2, lv_color_t color) {
  lv_draw_rect_dsc_t fill;
  lv_draw_rect_dsc_init(&fill);
  fill.bg_color = color;
  fill.bg_opa = LV_OPA_COVER;
  fill.border_width = 0;
  fill.radius = 0;
  lv_point_t tri[3] = {{(lv_coord_t)x0, (lv_coord_t)y0},
                       {(lv_coord_t)x1, (lv_coord_t)y1},
                       {(lv_coord_t)x2, (lv_coord_t)y2}};
  lv_canvas_draw_polygon(canvas, tri, 3, &fill);
}

static void drawWifi(lv_obj_t* canvas, uint16_t S, int level) {
  int cx = S / 2;
  int lw = (int)(S / 16);
  if (lw < 2) lw = 2;

  const int ay   = (int)(S * 0.70f);   // 扇心（最低点）
  const int a0   = 222, a1 = 318;      // 三弧共用同一开口，不再逐圈放大
  const int rOut = (int)(S * 0.48f);
  const int rMid = (int)(S * 0.33f);
  const int rIn  = (int)(S * 0.18f);
  int rDot = (int)(S * 0.10f);
  if (rDot < 2) rDot = 2;

  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.width = (lv_coord_t)lw;
  ld.round_start = 1;
  ld.round_end = 1;
  ld.opa = LV_OPA_COVER;

  /* 由内到外 3 道弧 + 中心点；第 i 道点亮条件是 level > i */
  const int arcR[3] = {rIn, rMid, rOut};
  for (int i = 0; i < 3; i++) {
    ld.color = (i < level) ? lv_color_white() : dim_color();
    arc_polyline(canvas, cx, ay, arcR[i], a0, a1, &ld);
  }
  /* 中心点：作为第 0 格，level>=1 才算连上。
     2026-09-23：这里原来是 lv_canvas_draw_arc(..., 0, 360) 画整圆。
     整圆 + rounded 端帽是 LVGL 8.3 的退化情形，会在 lv_draw_mask_free_param
     里踩到未初始化的 mask 参数 —— WiFi 图标放大到 22px 后实测触发
     LoadProhibited panic（addr2line 实锤：icons.cpp:55 → lv_draw_mask.c:215）。
     改用带圆角的矩形画点，语义一样，但没有退化路径。 */
  lv_draw_rect_dsc_t rd;
  lv_draw_rect_dsc_init(&rd);
  rd.bg_color = (level >= 1) ? lv_color_white() : dim_color();
  rd.bg_opa = LV_OPA_COVER;
  rd.border_width = 0;
  rd.radius = (lv_coord_t)rDot;          /* 半径 = 直径一半 → 视觉上就是圆点 */
  lv_canvas_draw_rect(canvas, cx - rDot, ay - rDot, (lv_coord_t)(rDot * 2),
                      (lv_coord_t)(rDot * 2), &rd);
}

void drawIcon(Icon type, lv_obj_t* canvas, uint16_t S) {
  int cx = S / 2, cy = S / 2;
  int R = S / 2;
  int lw = (int)(S / 16);
  if (lw < 2) lw = 2;

  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = lv_color_white();
  ld.width = (lv_coord_t)lw;
  ld.round_start = 1;
  ld.round_end = 1;
  ld.opa = LV_OPA_COVER;

  lv_draw_arc_dsc_t ad;
  lv_draw_arc_dsc_init(&ad);
  ad.color = lv_color_white();
  ad.width = (lv_coord_t)lw;
  ad.rounded = 1;
  ad.opa = LV_OPA_COVER;

  lv_draw_rect_dsc_t rd;
  lv_draw_rect_dsc_init(&rd);
  rd.bg_opa = LV_OPA_TRANSP;
  rd.border_color = lv_color_white();
  rd.border_width = (lv_coord_t)lw;
  rd.radius = (lv_coord_t)(S / 8);

  lv_point_t seg[3];

  switch (type) {
    case Icon::Battery: {
      /* 电池：外壳（描边）+ 正极帽 + 电量条（实心）。
         电量默认画满；本板无电量检测 ADC，状态栏按 USB 供电处理，
         后续若接了电池分压，改 StatusBar 的电量来源即可。

         2026-09-23：master 反馈"外面加一个小框子"。原来 shell 的 border_width
         直接用了 lw（= S/16，18px 时算出来是 1）—— 1px 白边在黑底上细到看不见，
         整个电池看着就是一个白块。现在描边独立取 ≥2px，内胆再往里缩，
         让"框 + 芯"的层次真正成立。 */
      const int bx = (int)(S * 0.10f), by = (int)(S * 0.28f);
      const int bw = (int)(S * 0.68f), bh = (int)(S * 0.44f);
      int frameW = 2;
      if (S >= 24) frameW = 3;

      lv_draw_rect_dsc_t shell = rd;          // 外壳描边（rd 已是白边+透明底）
      shell.radius = (lv_coord_t)(S * 0.05f);
      shell.border_width = (lv_coord_t)frameW;
      lv_canvas_draw_rect(canvas, bx, by, bw, bh, &shell);

      lv_draw_rect_dsc_t cap = rd;            // 正极小帽（实心）
      cap.bg_color = lv_color_white();
      cap.bg_opa = LV_OPA_COVER;
      cap.border_width = 0;
      cap.radius = 0;
      lv_canvas_draw_rect(canvas, bx + bw + frameW, by + (int)(bh * 0.30f),
                          (lv_coord_t)(S * 0.06f), (lv_coord_t)(bh * 0.40f), &cap);

      lv_draw_rect_dsc_t fill = rd;           // 电量条（实心）
      fill.bg_color = lv_color_white();
      fill.bg_opa = LV_OPA_COVER;
      fill.border_width = 0;
      fill.radius = 0;
      const int pad = frameW + 1;
      lv_canvas_draw_rect(canvas, bx + pad, by + pad,
                          (lv_coord_t)(bw - pad * 2), (lv_coord_t)(bh - pad * 2),
                          &fill);
      break;
    }
    case Icon::Tasks: {
      /* 后台运行指示：2×2 四个小方块（"多任务"的通用符号）。
         ⚠️ 用实心填充而不是细描边 —— 22px 的图标里描边会糊成一团，
         跟 WiFi 格子踩的是同一个坑（线宽才 1~2px）。 */
      const int m = (int)(S * 0.14f);
      const int w = (int)(S * 0.30f);
      const int gap = S - m * 2 - w * 2;
      lv_draw_rect_dsc_t tile = rd;
      tile.bg_color = lv_color_white();
      tile.bg_opa = LV_OPA_COVER;
      tile.border_width = 0;
      tile.radius = (lv_coord_t)(S * 0.06f);
      lv_canvas_draw_rect(canvas, m, m, w, w, &tile);
      lv_canvas_draw_rect(canvas, m + w + gap, m, w, w, &tile);
      lv_canvas_draw_rect(canvas, m, m + w + gap, w, w, &tile);
      lv_canvas_draw_rect(canvas, m + w + gap, m + w + gap, w, w, &tile);
      break;
    }
    case Icon::Bluetooth: {
      /* 蓝牙：居中竖线 + 两条过中心的斜线 + 上下各一条短腿，一笔折线画完
         （Feather 的 bluetooth 就是这条 polyline）。

         旧版把竖线放在 0.36·S、尖角只朝右长，字形既窄（宽 0.34·S）又整体
         偏左，看起来"高瘦且歪在一边"。现在以 x=0.50·S 为轴对称：
         包围盒 宽 0.52·S × 高 0.76·S，中心正好落在画布中心。
         两条斜线的中点都是 (0.50·S, 0.50·S)，即交于竖线中点。 */
      lv_point_t bt[6] = {
          {(lv_coord_t)(S * 0.24f), (lv_coord_t)(S * 0.28f)},  // 左上 → 右下（过中心）
          {(lv_coord_t)(S * 0.76f), (lv_coord_t)(S * 0.72f)},
          {(lv_coord_t)(S * 0.50f), (lv_coord_t)(S * 0.88f)},  // 收回竖线底端
          {(lv_coord_t)(S * 0.50f), (lv_coord_t)(S * 0.12f)},  // 竖线：自下而上
          {(lv_coord_t)(S * 0.76f), (lv_coord_t)(S * 0.28f)},  // 顶端短腿 → 右上
          {(lv_coord_t)(S * 0.24f), (lv_coord_t)(S * 0.72f)}   // 右上 → 左下（过中心）
      };
      lv_canvas_draw_line(canvas, bt, 6, &ld);
      break;
    }
    case Icon::Clock: {
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.85f), 0, 360, &ad);
      seg[0] = P(cx, cy, (int)(R * 0.40f), -55);   // 时针
      seg[1] = {cx, cy};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      seg[0] = P(cx, cy, (int)(R * 0.62f), 60);     // 分针
      seg[1] = {cx, cy};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(S * 0.04f), 0, 360, &ad);
      break;
    }
    case Icon::Settings: {
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.55f), 0, 360, &ad);
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.22f), 0, 360, &ad);
      for (int i = 0; i < 8; i++) {
        int ang = i * 45;
        seg[0] = P(cx, cy, (int)(R * 0.55f), ang);
        seg[1] = P(cx, cy, (int)(R * 0.75f), ang);
        lv_canvas_draw_line(canvas, seg, 2, &ld);
      }
      break;
    }
    case Icon::Back: {
      /* 2026-09-24 master：返回键只要一个三角形，不要那根杆（「棍子」），
         并且要够大 —— iOS 导航栏就是这个形状：实心三角 + 后面的标题文字。
         画法是 lv_canvas_draw_polygon 实心填充：
         查过 vendored 的 lv_draw_sw_polygon.c，它的 mask 走
         lv_draw_mask_remove_custom(mp) 统一回收，**没有** lv_draw_sw_arc.c
         那种「部分分支 free 未初始化 param」的坑，可以放心用。
         顶点取 左中 / 右上 / 右下，占 0.64·S 宽 × 0.70·S 高，垂直居中。 */
      draw_tri(canvas,
               (int)(S * 0.16f), (int)(S * 0.50f),
               (int)(S * 0.80f), (int)(S * 0.15f),
               (int)(S * 0.80f), (int)(S * 0.85f),
               lv_color_white());
      break;
    }
    case Icon::Forward: {
      /* 与 Back 配套，同尺寸镜像。浏览器前进键用它。 */
      draw_tri(canvas,
               (int)(S * 0.84f), (int)(S * 0.50f),
               (int)(S * 0.20f), (int)(S * 0.15f),
               (int)(S * 0.20f), (int)(S * 0.85f),
               lv_color_white());
      break;
    }
    case Icon::Home: {
      /* 房子：人字屋顶（折线）+ 房身（描边矩形） */
      seg[0] = {(lv_coord_t)(S * 0.14f), (lv_coord_t)(S * 0.46f)};
      seg[1] = {(lv_coord_t)(S * 0.50f), (lv_coord_t)(S * 0.16f)};
      seg[2] = {(lv_coord_t)(S * 0.86f), (lv_coord_t)(S * 0.46f)};
      lv_canvas_draw_line(canvas, seg, 3, &ld);
      lv_draw_rect_dsc_t body = rd;
      body.radius = 0;
      lv_canvas_draw_rect(canvas, (lv_coord_t)(S * 0.26f), (lv_coord_t)(S * 0.46f),
                          (lv_coord_t)(S * 0.48f), (lv_coord_t)(S * 0.36f), &body);
      break;
    }
    case Icon::Refresh: {
      /* 环形箭头：3/4 圆弧（40°→320°，右上留缺口）+ 末端箭头。
         弧走折线（同 WiFi，避开 lv_draw_arc 的退化分支）。 */
      const int r = (int)(S * 0.32f);
      arc_polyline(canvas, cx, cy, r, 40, 320, &ld);
      float a = 320.0f * kPi / 180.0f;
      float ex = cx + r * cosf(a), ey = cy + r * sinf(a);
      float tdx = -sinf(a), tdy = cosf(a);   /* 切线（角度增大方向） */
      const int head = (int)(S * 0.20f);
      const float tipX = ex + tdx * head * 0.45f;
      const float tipY = ey + tdy * head * 0.45f;
      const float nx = -tdy, ny = tdx;
      lv_point_t h[3] = {
          {(lv_coord_t)(tipX - tdx * head + nx * head * 0.62f),
           (lv_coord_t)(tipY - tdy * head + ny * head * 0.62f)},
          {(lv_coord_t)tipX, (lv_coord_t)tipY},
          {(lv_coord_t)(tipX - tdx * head - nx * head * 0.62f),
           (lv_coord_t)(tipY - tdy * head - ny * head * 0.62f)}};
      lv_canvas_draw_line(canvas, h, 3, &ld);
      break;
    }
    case Icon::ExitDoor: {
      /* 退出 = 箭头指着一扇门（Feather 的 log-out 语义）：
         门在右（描边矩形 + 门把手），箭头在左，尖端刚好抵到门框。 */
      lv_draw_rect_dsc_t door = rd;
      door.radius = 0;
      const int dx0 = (int)(S * 0.44f), dy0 = (int)(S * 0.18f);
      const int dw = (int)(S * 0.40f), dh = (int)(S * 0.64f);
      lv_canvas_draw_rect(canvas, dx0, dy0, (lv_coord_t)dw, (lv_coord_t)dh, &door);
      int kr = (int)(S * 0.05f);
      if (kr < 1) kr = 1;
      draw_dot(canvas, (int)(S * 0.50f), (int)(S * 0.50f), kr, lv_color_white());
      draw_arrow(canvas, (int)(S * 0.08f), cy, (int)(S * 0.40f), cy,
                 (int)(S * 0.18f), &ld);
      break;
    }
    case Icon::Download: {
      /* 下载：向下箭头 + 底线。竖杆从上到下，末端两只箭头翼，底部一条横线（托盘）。 */
      const int top = (int)(S * 0.26f);
      const int bot = (int)(S * 0.68f);
      lv_point_t stem[2] = {{cx, top}, {cx, bot}};
      lv_canvas_draw_line(canvas, stem, 2, &ld);
      const int head = (int)(S * 0.22f);
      lv_point_t wing[3] = {{(lv_coord_t)(cx - head), (lv_coord_t)(bot - head)},
                            {(lv_coord_t)cx, (lv_coord_t)bot},
                            {(lv_coord_t)(cx + head), (lv_coord_t)(bot - head)}};
      lv_canvas_draw_line(canvas, wing, 3, &ld);
      const int y0 = (int)(S * 0.78f);
      lv_point_t base[2] = {{(lv_coord_t)(S * 0.22f), (lv_coord_t)y0},
                            {(lv_coord_t)(S * 0.78f), (lv_coord_t)y0}};
      lv_canvas_draw_line(canvas, base, 2, &ld);
      break;
    }
    case Icon::More: {
      /* 三个点（更多）。横向排布，间距 0.26·S，垂直居中。 */
      int r = (int)(S * 0.085f);
      if (r < 2) r = 2;
      const int xs[3] = {(int)(S * 0.24f), (int)(S * 0.50f), (int)(S * 0.76f)};
      for (int i = 0; i < 3; i++) draw_dot(canvas, xs[i], cy, r, lv_color_white());
      break;
    }
    case Icon::Wifi: {
      drawWifi(canvas, S, 3);   /* 默认满格；要做信号强度用 icon_create_wifi() */
      break;
    }
    case Icon::Weather: {
      int sx = cx, sy = cy - (int)(R * 0.05f);
      lv_canvas_draw_arc(canvas, sx, sy, (lv_coord_t)(R * 0.22f), 0, 360, &ad);
      for (int i = 0; i < 8; i++) {
        int ang = i * 45;
        seg[0] = P(sx, sy, (int)(R * 0.30f), ang);
        seg[1] = P(sx, sy, (int)(R * 0.44f), ang);
        lv_canvas_draw_line(canvas, seg, 2, &ld);
      }
      break;
    }
    case Icon::Switch: {
      lv_canvas_draw_rect(canvas, cx - (lv_coord_t)(R * 0.60f), cy - (lv_coord_t)(R * 0.22f),
                          (lv_coord_t)(R * 1.20f), (lv_coord_t)(R * 0.44f), &rd);
      lv_canvas_draw_arc(canvas, cx + (lv_coord_t)(R * 0.30f), cy, (lv_coord_t)(R * 0.16f), 0, 360, &ad);
      break;
    }
    case Icon::Terminal: {
      lv_canvas_draw_rect(canvas, cx - (lv_coord_t)(R * 0.60f), cy - (lv_coord_t)(R * 0.42f),
                          (lv_coord_t)(R * 1.20f), (lv_coord_t)(R * 0.72f), &rd);
      seg[0] = {cx - (lv_coord_t)(R * 0.35f), cy - (lv_coord_t)(R * 0.05f)};
      seg[1] = {cx - (lv_coord_t)(R * 0.18f), cy + (lv_coord_t)(R * 0.12f)};
      seg[2] = {cx - (lv_coord_t)(R * 0.35f), cy + (lv_coord_t)(R * 0.29f)};
      lv_canvas_draw_line(canvas, seg, 3, &ld);
      seg[0] = {cx - (lv_coord_t)(R * 0.08f), cy - (lv_coord_t)(R * 0.05f)};
      seg[1] = {cx - (lv_coord_t)(R * 0.08f), cy + (lv_coord_t)(R * 0.24f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      break;
    }
    case Icon::Music: {
      lv_canvas_draw_arc(canvas, cx - (lv_coord_t)(R * 0.18f), cy + (lv_coord_t)(R * 0.22f),
                         (lv_coord_t)(R * 0.16f), 0, 360, &ad);
      seg[0] = {cx - (lv_coord_t)(R * 0.02f), cy + (lv_coord_t)(R * 0.18f)};
      seg[1] = {cx - (lv_coord_t)(R * 0.02f), cy - (lv_coord_t)(R * 0.42f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      seg[0] = {cx - (lv_coord_t)(R * 0.02f), cy - (lv_coord_t)(R * 0.42f)};
      seg[1] = {cx + (lv_coord_t)(R * 0.22f), cy - (lv_coord_t)(R * 0.28f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      break;
    }
    case Icon::Power: {
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.40f), 290, 360, &ad);
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.40f), 0, 250, &ad);
      seg[0] = {cx, cy};
      seg[1] = {cx, cy - (lv_coord_t)(R * 0.42f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      break;
    }
    case Icon::Game: {
      lv_canvas_draw_rect(canvas, cx - (lv_coord_t)(R * 0.55f), cy - (lv_coord_t)(R * 0.35f),
                          (lv_coord_t)(R * 1.10f), (lv_coord_t)(R * 0.70f), &rd);
      lv_canvas_draw_arc(canvas, cx - (lv_coord_t)(R * 0.28f), cy, (lv_coord_t)(R * 0.08f), 0, 360, &ad);
      seg[0] = {cx + (lv_coord_t)(R * 0.15f), cy - (lv_coord_t)(R * 0.12f)};
      seg[1] = {cx + (lv_coord_t)(R * 0.35f), cy + (lv_coord_t)(R * 0.08f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      seg[0] = {cx + (lv_coord_t)(R * 0.35f), cy - (lv_coord_t)(R * 0.12f)};
      seg[1] = {cx + (lv_coord_t)(R * 0.15f), cy + (lv_coord_t)(R * 0.08f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      break;
    }
    case Icon::Browser: {
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.50f), 0, 360, &ad);
      seg[0] = {cx - (lv_coord_t)(R * 0.50f), cy};
      seg[1] = {cx + (lv_coord_t)(R * 0.50f), cy};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      lv_canvas_draw_arc(canvas, cx, cy, (lv_coord_t)(R * 0.25f), 0, 360, &ad);
      seg[0] = {cx, cy - (lv_coord_t)(R * 0.50f)};
      seg[1] = {cx, cy - (lv_coord_t)(R * 0.25f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      seg[0] = {cx, cy + (lv_coord_t)(R * 0.25f)};
      seg[1] = {cx, cy + (lv_coord_t)(R * 0.50f)};
      lv_canvas_draw_line(canvas, seg, 2, &ld);
      break;
    }
  }
}

}  // namespace

lv_obj_t* icon_create(lv_obj_t* parent, Icon type, uint16_t size) {
  if (size < 16) size = 16;
  lv_obj_t* canvas = lv_canvas_create(parent);
  uint32_t bufBytes = (uint32_t)size * size * 4;  // TRUE_COLOR_ALPHA: 4B/px (ARGB8888)
  // 从 PSRAM 分配 canvas 缓冲，不占用 LVGL 128KB 内存池
  // 格式是 ARGB8888（4B/px）：必须保留 alpha，否则画布底色会露出来
  // （2026-09-24 曾改成不透明 16bpp 省 blend，结果所有图标都带黑底框，已回退）
  void* buf = heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
  if (!buf) return canvas;
  lv_canvas_set_buffer(canvas, buf, (lv_coord_t)size, (lv_coord_t)size,
                       LV_IMG_CF_TRUE_COLOR_ALPHA);
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);
  drawIcon(type, canvas, size);
  return canvas;
}

/* WiFi 信号强度图标：level 0..3。未点亮的弧用暗色画出，格子形状始终可见。 */
lv_obj_t* icon_create_wifi(lv_obj_t* parent, uint16_t size, int level) {
  if (size < 16) size = 16;
  if (level < 0) level = 0;
  if (level > 3) level = 3;
  lv_obj_t* canvas = lv_canvas_create(parent);
  uint32_t bufBytes = (uint32_t)size * size * 4;  // ARGB8888
  void* buf = heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
  if (!buf) return canvas;
  lv_canvas_set_buffer(canvas, buf, (lv_coord_t)size, (lv_coord_t)size,
                       LV_IMG_CF_TRUE_COLOR_ALPHA);
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);
  drawWifi(canvas, size, level);
  return canvas;
}

/* 就地重绘已有的 WiFi 画布（信号变化时用，不重新分配 PSRAM 缓冲） */
void icon_wifi_set_level(lv_obj_t* canvas, uint16_t size, int level) {
  if (!canvas || !lv_obj_is_valid(canvas)) return;
  if (level < 0) level = 0;
  if (level > 3) level = 3;
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);
  drawWifi(canvas, size, level);
  lv_obj_invalidate(canvas);
}

/* 就地换图标类型：同一个画布擦干净重画，不重新 malloc PSRAM。
   切换按钮「更多(三点) ↔ 返回(左箭头)」就是靠它在两态之间切。 */
void icon_set_type(lv_obj_t* canvas, Icon type, uint16_t size) {
  if (!canvas || !lv_obj_is_valid(canvas)) return;
  if (size < 16) size = 16;
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);
  drawIcon(type, canvas, size);
  lv_obj_invalidate(canvas);
}
