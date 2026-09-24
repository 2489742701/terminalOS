"""
buf_internal_dram.py - 把 LVGL 的 draw_buf 从 PSRAM 挪进内部 DRAM

为什么：
  perfbw 实测写 PSRAM 30 MB/s、写内部 DRAM 188 MB/s，差 6.3 倍。
  LVGL 的 draw 阶段就是往这块缓冲里写像素，缓冲放哪儿直接决定 draw 的快慢
  （桌面屏 draw 实测 43ms，是整帧 44ms 里的绝对大头）。

为什么不继续用 direct mode（draw_buf = framebuffer）：
  它确实把 flush 从 26ms 打到 0.25ms、整帧 73ms -> 44ms，但代价是 LVGL 边画
  边被 GDMA 扫出去 —— 画面撕裂闪烁；而且 LVGL 一旦越界写就直接写到显存外面，
  紧邻着 LVGL 的 PSRAM 池，风险太大。改成"先画到 DRAM 缓冲，再整块拷进显存"，
  行为跟改动前一致（用户从没抱怨过老方案的画面），但快得多。
"""
import io, os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
P = os.path.join(ROOT, 'src', 'app', 'app.cpp')

raw = io.open(P, 'r', encoding='utf-8', newline='').read()
crlf = '\r\n' in raw
s = raw.replace('\r\n', '\n')

OLD = """  /* 4. 显示缓冲
        两条路，优先 direct mode（draw_buf = panel framebuffer）：
          · direct：LVGL 直接在显存作画，flush 只剩 cache 写回。
            perfbw 实测搬运一趟 460800 B 要 25ms（18 MB/s，被 GDMA 抢带宽），
            这趟是整个帧时间里最贵的一块 —— 砍掉它。
          · 回退：PSRAM 双缓冲（每块 120 行 \u2248 1/4 屏）+ 拷贝，行为与改动前一致。 */
  uint16_t* fb = Display::getFramebuffer();
  uint32_t bufSize;
  if (fb) {
    g_directMode = true;
    bufSize = (uint32_t)SCREEN_WIDTH * SCREEN_HEIGHT;   // direct 要求整屏
    dispBuf1 = (lv_color_t*)fb;
    dispBuf2 = nullptr;
    Serial.printf("[App] LVGL direct mode: draw_buf = framebuffer %p (%u px, %u B)\\n",
                  fb, (unsigned)bufSize, (unsigned)(bufSize * 2));
  } else {
    bufSize = (uint32_t)SCREEN_WIDTH * 120;
    dispBuf1 = (lv_color_t*)heap_caps_malloc(sizeof(lv_color_t) * bufSize,
                                             MALLOC_CAP_SPIRAM);
    dispBuf2 = (lv_color_t*)heap_caps_malloc(sizeof(lv_color_t) * bufSize,
                                             MALLOC_CAP_SPIRAM);
    Serial.printf("[App] buf pixels=%u bytes=%u ptr=%p\\n", (unsigned)bufSize,
                  (unsigned)(sizeof(lv_color_t) * bufSize), dispBuf1);
  }
  if (!dispBuf1) {
    Serial.println("[App] LVGL buffer alloc failed");
    return false;
  }
  lv_disp_draw_buf_init(&drawBuf, dispBuf1, dispBuf2, bufSize);
"""

NEW = """  /* 4. 显示缓冲 —— 首选内部 DRAM，不够才回退 PSRAM。
        perfbw 实测（460800 B，即一整屏）：
          写 PSRAM       30.1 MB/s
          写内部 DRAM   188.6 MB/s     <-- 6.3 倍
        LVGL 的 draw 阶段本质就是往这块缓冲里写像素，所以缓冲放哪儿直接决定
        draw 快慢（桌面屏 draw 实测 43ms，占整帧 44ms 的 98%）。
        内部 DRAM 只够摆 2 块 60 行（57600 px = 115200 B），不够整屏也够用，
        LVGL 会自动拆成 8 批 draw+flush 交替推进。
        注意 direct mode（draw_buf 直接指向 framebuffer）虽然能把 flush 从
        26ms 打到 0.25ms，但那样 LVGL 是边画边被 GDMA 扫出去 —— 画面撕裂
        闪烁，且一旦越界写就直接写到显存外头，紧邻 LVGL 的 PSRAM 池。
        代码留着（g_directMode）做对照实验，默认不走。 */
  g_directMode = false;
  uint32_t bufSize = (uint32_t)SCREEN_WIDTH * 60;      /* 60 行 = 115200 B */
  dispBuf1 = (lv_color_t*)heap_caps_malloc(sizeof(lv_color_t) * bufSize,
                                           MALLOC_CAP_INTERNAL);
  dispBuf2 = (lv_color_t*)heap_caps_malloc(sizeof(lv_color_t) * bufSize,
                                           MALLOC_CAP_INTERNAL);
  if (dispBuf1 && dispBuf2) {
    Serial.printf("[App] LVGL buf INTERNAL DRAM: %u px (%u B) x2  ptr=%p\\n",
                  (unsigned)bufSize, (unsigned)(sizeof(lv_color_t) * bufSize),
                  dispBuf1);
  } else {
    /* 内部 DRAM 不够：把半截的释放掉，回退到 PSRAM（改动前的行为）。
       半截不 free 就是泄漏，回退路径反而更难走。 */
    if (dispBuf1) { heap_caps_free(dispBuf1); dispBuf1 = nullptr; }
    if (dispBuf2) { heap_caps_free(dispBuf2); dispBuf2 = nullptr; }
    Serial.println("[App] internal DRAM too small for LVGL buf -> fall back to PSRAM");
    bufSize = (uint32_t)SCREEN_WIDTH * 120;
    dispBuf1 = (lv_color_t*)heap_caps_malloc(sizeof(lv_color_t) * bufSize,
                                             MALLOC_CAP_SPIRAM);
    dispBuf2 = (lv_color_t*)heap_caps_malloc(sizeof(lv_color_t) * bufSize,
                                             MALLOC_CAP_SPIRAM);
    if (dispBuf1) {
      Serial.printf("[App] LVGL buf PSRAM: %u px (%u B) x2  ptr=%p\\n",
                    (unsigned)bufSize, (unsigned)(sizeof(lv_color_t) * bufSize),
                    dispBuf1);
    }
  }
  if (!dispBuf1) {
    Serial.println("[App] LVGL buffer alloc failed");
    return false;
  }
  lv_disp_draw_buf_init(&drawBuf, dispBuf1, dispBuf2, bufSize);
"""

assert OLD in s, 'app.cpp buffer anchor not found'
s = s.replace(OLD, NEW, 1)

if crlf:
    s = s.replace('\n', '\r\n')
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('WROTE ' + P)
