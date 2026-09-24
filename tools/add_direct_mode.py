"""
add_direct_mode.py - 让 LVGL 直接画进 panel framebuffer（消灭 460KB 的 PSRAM 拷贝）

背景（已由 perfbw 实测钉死）：
  flush 恒定 26ms 与屏内容无关，实测 gfx->draw16bitRGBBitmap() 和裸
  memcpy(PSRAM->PSRAM) 完全同速（25481 vs 25065 us）—— 它压根不是在"推屏"，
  而是在 LVGL 的 PSRAM 缓冲和 panel 的 PSRAM 帧缓冲之间做一次纯软件搬运，
  460800 B 一读一写全走 OPI，还被 LCD 的 GDMA 抢走一半带宽（18 MB/s）。
  让 LVGL 直接画在 framebuffer 上，这趟搬运整个消失。

改三个文件（用 python 替换而非 Edit 工具：本项目 CRLF 上 Edit 报成功不落盘已踩多次）：
  1. src/hal/display.h    - 加 getFramebuffer() / flushCache()
  2. src/hal/display.cpp  - 实现
  3. src/app/app.cpp      - direct_mode 分支 + dispFlush 分支
"""
import io, os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))


def rw(path, fn):
    """本项目源码是 CRLF；先在 \n 空间里做替换，写回时还原成原换行风格。"""
    p = os.path.join(ROOT, path)
    raw = io.open(p, 'r', encoding='utf-8', newline='').read()
    crlf = '\r\n' in raw
    s = raw.replace('\r\n', '\n')
    o = s
    s = fn(s)
    if s != o:
        if crlf:
            s = s.replace('\n', '\r\n')
        io.open(p, 'w', encoding='utf-8', newline='').write(s)
        print('WROTE ' + p)
    else:
        print('NO CHANGE ' + p)
    return s


# ---------------- 1) display.h ----------------
def f_h(s):
    old = """  // 获取 GFX 绘图对象单例
  static Arduino_GFX* getGfx();
"""
    new = """  // 获取 GFX 绘图对象单例
  static Arduino_GFX* getGfx();

  /* panel 的帧缓冲首址（PSRAM，480*480*2 = 460800 B）。
     LVGL 开 direct_mode 后可以指着这里当 draw_buf，直接在屏的显存上作画，
     从而省掉 "LVGL 缓冲 -> framebuffer" 那趟 PSRAM->PSRAM 搬运（实测 25ms）。
     未初始化 / 非 RGB 屏时返回 nullptr，调用方必须判空后回退。 */
  static uint16_t* getFramebuffer();

  /* 把 CPU 的 DCache 写回物理内存。
     PSRAM 经 cache 访问，而 LCD 的 GDMA 读的是物理内存 —— 不写回的话
     GDMA 扫出去的还是上一帧的旧数据（表现为"画面不动/残影"）。 */
  static void flushCache(uint32_t addr, uint32_t size);
"""
    assert old in s, 'display.h anchor1'
    return s.replace(old, new, 1)


# ---------------- 2) display.cpp ----------------
def f_cpp(s):
    old = '#include "display.h"\n#include "../config/pins.h"\n'
    new = ('#include "display.h"\n#include "../config/pins.h"\n'
           '#include <Arduino_DataBus.h>   /* Cache_WriteBack_Addr */\n')
    assert old in s, 'display.cpp anchor1'
    s = s.replace(old, new, 1)

    old2 = 'Arduino_GFX* Display::getGfx() { return gfx; }\n'
    new2 = old2 + """
uint16_t* Display::getFramebuffer() {
  /* getFramebuffer() 是 Arduino_ST7701_RGBPanel 的方法，Arduino_GFX 基类没有。
     这里 panel 就是那个具体类型，直接调即可。 */
  return panel ? panel->getFramebuffer() : nullptr;
}

void Display::flushCache(uint32_t addr, uint32_t size) {
  Cache_WriteBack_Addr(addr, size);
}
"""
    assert old2 in s, 'display.cpp anchor2'
    return s.replace(old2, new2, 1)


# ---------------- 3) app.cpp ----------------
def f_app(s):
    # 3a) 加 direct mode 标志
    old = """// LVGL 显示缓冲（PSRAM 双缓冲）
static lv_disp_draw_buf_t drawBuf;
static lv_color_t* dispBuf1 = nullptr;
static lv_color_t* dispBuf2 = nullptr;
"""
    new = """// LVGL 显示缓冲
static lv_disp_draw_buf_t drawBuf;
static lv_color_t* dispBuf1 = nullptr;
static lv_color_t* dispBuf2 = nullptr;
/* direct mode：draw_buf 直接指向 panel 的 framebuffer，LVGL 在显存上作画，
   flush 不再搬运。实测能省掉整帧 25ms（占原先 73ms 的三分之一）。
   为 false 时回退到老的 "PSRAM 双缓冲 + 拷贝" 路径。 */
static bool g_directMode = false;
"""
    assert old in s, 'app.cpp anchor1'
    s = s.replace(old, new, 1)

    # 3b) dispFlush 分支
    old2 = """void App::dispFlush(lv_disp_drv_t* disp, const lv_area_t* area,
                    lv_color_t* colorP) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  Display::getGfx()->draw16bitRGBBitmap(area->x1, area->y1,
                                        (uint16_t*)&colorP->full, w, h);
  lv_disp_flush_ready(disp);
}
"""
    new2 = """void App::dispFlush(lv_disp_drv_t* disp, const lv_area_t* area,
                    lv_color_t* colorP) {
  if (g_directMode) {
    /* 像素已经在 framebuffer 里了，这里只做一件事：把这段的 CPU cache 写回
       物理 PSRAM，让 GDMA 扫到新数据。
       注意按"整行"写回而不是按矩形宽度 —— framebuffer 里行间距是整屏宽，
       只回写 w*h 会漏掉每行末尾。写法与 Arduino_GFX 内部一致。
       成本约几百 us，相比搬运的 25ms 可以忽略。 */
    uint32_t y1 = (uint32_t)area->y1;
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t* fb = Display::getFramebuffer();
    if (fb) Display::flushCache((uint32_t)(fb + y1 * SCREEN_WIDTH),
                                (uint32_t)SCREEN_WIDTH * h * 2);
    lv_disp_flush_ready(disp);
    return;
  }

  /* 回退路径：把 LVGL 缓冲搬进 framebuffer（PSRAM->PSRAM，实测 25ms） */
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  Display::getGfx()->draw16bitRGBBitmap(area->x1, area->y1,
                                        (uint16_t*)&colorP->full, w, h);
  lv_disp_flush_ready(disp);
}
"""
    assert old2 in s, 'app.cpp anchor2'
    s = s.replace(old2, new2, 1)

    # 3c) 缓冲分配：优先 direct mode
    old3 = """  // 4. 分配显示缓冲（PSRAM 双缓冲，每缓冲 120 行 ≈ 1/4 屏）
  //    缓冲太小会让 LVGL 拆成大量小批次刷写，加剧 PSRAM 带宽争抢
  uint32_t bufSize = SCREEN_WIDTH * 120;
  dispBuf1 = (lv_color_t*)heap_caps_malloc(sizeof(lv_color_t) * bufSize,
                                           MALLOC_CAP_SPIRAM);
  dispBuf2 = (lv_color_t*)heap_caps_malloc(sizeof(lv_color_t) * bufSize,
                                           MALLOC_CAP_SPIRAM);
  if (!dispBuf1 || !dispBuf2) {
    Serial.println("[App] LVGL buffer alloc failed");
    return false;
  }
  Serial.printf("[App] buf pixels=%u bytes=%u ptr=%p\\n", (unsigned)bufSize,
                (unsigned)(sizeof(lv_color_t) * bufSize), dispBuf1);
  lv_disp_draw_buf_init(&drawBuf, dispBuf1, dispBuf2, bufSize);
"""
    new3 = """  /* 4. 显示缓冲
        两条路，优先 direct mode（draw_buf = panel framebuffer）：
          · direct：LVGL 直接在显存作画，flush 只剩 cache 写回。
            perfbw 实测搬运一趟 460800 B 要 25ms（18 MB/s，被 GDMA 抢带宽），
            这趟是整个帧时间里最贵的一块 —— 砍掉它。
          · 回退：PSRAM 双缓冲（每块 120 行 ≈ 1/4 屏）+ 拷贝，行为与改动前一致。 */
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
    assert old3 in s, 'app.cpp anchor3'
    s = s.replace(old3, new3, 1)

    # 3d) 驱动注册打开 direct_mode
    old4 = """  dispDrv.flush_cb = dispFlush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);
  memMark("4 disp drv (PSRAM buf)");
"""
    new4 = """  dispDrv.flush_cb = dispFlush;
  dispDrv.draw_buf = &drawBuf;
  if (g_directMode) dispDrv.direct_mode = 1;   // 告诉 LVGL：别拷，就在 fb 上画
  lv_disp_drv_register(&dispDrv);
  memMark(g_directMode ? "4 disp drv (direct mode)" : "4 disp drv (PSRAM buf)");
"""
    assert old4 in s, 'app.cpp anchor4'
    return s.replace(old4, new4, 1)


rw('src/hal/display.h', f_h)
rw('src/hal/display.cpp', f_cpp)
rw('src/app/app.cpp', f_app)
print('DONE')
