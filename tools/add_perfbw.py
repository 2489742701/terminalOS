"""
add_perfbw.py - 给 serial_console.cpp 注入 `perfbw` 带宽体检命令

为什么用脚本而不是 Edit 工具：本项目 CRLF 文件上 Edit 报成功但不落盘已踩过
多次，这里改成 python 直接做字符串替换，替换完立刻 grep 复核。
"""
import io, os, sys

P = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 '..', 'src', 'hal', 'serial_console.cpp')
P = os.path.normpath(P)

src = io.open(P, 'r', encoding='utf-8', newline='').read()
orig = src

# ---------- 1) include display.h（拿 Display::getGfx）----------
inc_old = '#include "../hal/battery.h"'
inc_new = '#include "../hal/battery.h"\n#include "../hal/display.h"'
if 'display.h' not in src:
    assert inc_old in src, 'include anchor not found'
    src = src.replace(inc_old, inc_new, 1)

# ---------- 2) 插入 cmdPerfBW 函数（放在 cmdPerf 之前）----------
anchor = 'static void cmdPerf(const char* arg) {'
assert anchor in src, 'cmdPerf anchor not found'
assert 'cmdPerfBW' not in src, 'already injected'

FN = r'''/* ---- perfbw：带宽体检（把 flush 那 26ms 的账算清楚）----
   perf 把一帧拆成 draw / flush 两段，但 flush 恒定 26~27ms 且跟屏内容无关，
   这不像"画东西"，更像"搬东西"。嫌疑：dispFlush() 走
   gfx->draw16bitRGBBitmap()，而它的实现本质是 **PSRAM -> PSRAM 的 memcpy**
   （LVGL 的 dispBuf 在 PSRAM，panel 的 framebuffer 也在 PSRAM），
   同时 LCD 的 GDMA 还在后台持续读同一块 PSRAM 往外扫。
   460800 B 一读一写，带宽跟 GDMA 对半分 —— 这就是那 26ms。
   下面逐项量出来，用数据把猜测钉死；别对着错的那一半使劲。 */
static void cmdPerfBW(const char* arg) {
  (void)arg;
  const int CW = 480, CH = 120;            /* 一块 LVGL draw_buf */
  const uint32_t CPN = (uint32_t)CW * CH;  /* 57600 px */
  const uint32_t CB32 = CPN / 2;           /* 28800 个 uint32 */
  const uint32_t CBN = CPN * 2;            /* 115200 B */
  const int REP = 4;                       /* 4 块 = 全屏 480x480 */

  Serial.println("[PerfBW] === bandwidth probe (who eats the 26ms flush?) ===");

  uint32_t* pA = (uint32_t*)heap_caps_malloc(CBN, MALLOC_CAP_SPIRAM);
  uint32_t* pB = (uint32_t*)heap_caps_malloc(CBN, MALLOC_CAP_SPIRAM);
  uint32_t* dB = (uint32_t*)heap_caps_malloc(CBN, MALLOC_CAP_INTERNAL);
  Serial.printf("[PerfBW] chunk=%uB x%d  pA=%p pB=%p dB=%p\n",
                (unsigned)CBN, REP, pA, pB, dB);
  Serial.printf("[PerfBW] DRAM free=%u PSRAM free=%u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  if (!pA || !pB) {
    Serial.println("[PerfBW] PSRAM alloc failed, abort");
    if (pA) heap_caps_free(pA);
    if (pB) heap_caps_free(pB);
    if (dB) heap_caps_free(dB);
    return;
  }

  uint32_t t;
  /* B/us 恰好等于 MB/s：1 B/us = 1e6 B/s */
#define REP_T(tag, us)                                                    \
  Serial.printf("[PerfBW] %-30s %6u us  (%6.1f MB/s)\n", tag,            \
                (unsigned)(us), (double)(CBN * REP) / (double)(us))

  /* 1) memcpy PSRAM->PSRAM —— dispFlush 的真实形态 */
  t = micros();
  for (int r = 0; r < REP; r++) memcpy(pB, pA, CBN);
  REP_T("1 memcpy PSRAM->PSRAM", micros() - t);

  /* 2) memcpy DRAM->PSRAM —— 源放内部能省掉一半的 PSRAM 事务 */
  if (dB) {
    t = micros();
    for (int r = 0; r < REP; r++) memcpy(pB, dB, CBN);
    REP_T("2 memcpy DRAM->PSRAM", micros() - t);
  }

  /* 3) memset PSRAM —— 纯写不读，看写带宽上限 */
  t = micros();
  for (int r = 0; r < REP; r++) memset(pB, 0x5A, CBN);
  REP_T("3 memset PSRAM (w only)", micros() - t);

  /* 4) 手写 32bit 循环 PSRAM->PSRAM —— draw16bitRGBBitmap 现在的写法 */
  t = micros();
  for (int r = 0; r < REP; r++) {
    uint32_t* s = pA;
    uint32_t* d = pB;
    for (uint32_t i = 0; i < CB32; i++) *d++ = *s++;
  }
  REP_T("4 hand32 PSRAM->PSRAM", micros() - t);

  /* 5) 真实 flush 路径：gfx->draw16bitRGBBitmap 推 4 块 */
  {
    Arduino_GFX* g = Display::getGfx();
    if (g) {
      t = micros();
      for (int r = 0; r < REP; r++)
        g->draw16bitRGBBitmap(0, r * CH, (uint16_t*)pA, CW, CH);
      REP_T("5 gfx draw16bitRGBBitmap", micros() - t);
    }
  }

  /* 6) 纯写 32bit 到 PSRAM —— LVGL 绘制（不透明填充）的写带宽 */
  t = micros();
  for (int r = 0; r < REP; r++) {
    uint32_t* d = pA;
    for (uint32_t i = 0; i < CB32; i++) *d++ = 0xC5C5C5C5u;
  }
  REP_T("6 fill32 PSRAM (draw)", micros() - t);

  /* 7) 纯写 32bit 到 DRAM —— draw_buf 若能放内部会快多少 */
  if (dB) {
    t = micros();
    for (int r = 0; r < REP; r++) {
      uint32_t* d = dB;
      for (uint32_t i = 0; i < CB32; i++) *d++ = 0xC5C5C5C5u;
    }
    REP_T("7 fill32 DRAM (draw)", micros() - t);
  }

  /* 8) 读改写 PSRAM —— alpha blend / 文字抗锯齿的真实形态 */
  t = micros();
  for (int r = 0; r < REP; r++) {
    uint32_t* d = pA;
    for (uint32_t i = 0; i < CB32; i++) {
      uint32_t v = *d;
      *d++ = (v & 0xF7DEF7DEu) | 0x08210821u;
    }
  }
  REP_T("8 blend32 PSRAM (r-m-w)", micros() - t);

  heap_caps_free(pA);
  heap_caps_free(pB);
  if (dB) heap_caps_free(dB);

  /* 第 5 项把 framebuffer 写花了，强制重画一帧恢复现场 */
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(lv_disp_get_default());
  Serial.printf("[PerfBW] done  DRAM free=%u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#undef REP_T
}

'''

src = src.replace(anchor, FN + anchor, 1)

# ---------- 3) 命令分发 ----------
disp_old = '  } else if (strcmp(cmd, "perf") == 0) {\n    cmdPerf(arg);'
disp_new = ('  } else if (strcmp(cmd, "perf") == 0) {\n    cmdPerf(arg);\n'
            '  } else if (strcmp(cmd, "perfbw") == 0) {\n    cmdPerfBW(arg);')
assert disp_old in src, 'dispatch anchor not found'
src = src.replace(disp_old, disp_new, 1)

# ---------- 4) help ----------
h_old = '  Serial.println("perf              - measure frame time (draw vs flush)");'
if h_old not in src:
    # 退而求其次：挂在 perf 那行附近没有就找别的锚点
    h_old = None
if h_old:
    src = src.replace(
        h_old,
        h_old + '\n  Serial.println("perfbw            - 带宽体检：定位 flush 那 26ms 是谁吃的");',
        1)

if src != orig:
    io.open(P, 'w', encoding='utf-8', newline='').write(src)
    print('WROTE ' + P)
else:
    print('NO CHANGE')
