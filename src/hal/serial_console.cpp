#include "serial_console.h"

#if SERIAL_CONSOLE_ENABLED

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include "src/core/lv_refr.h"
#include "app/font_zh.h"   /* lv_refr_now：绕过 20ms 节流立即刷新 */
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include "../app/nav.h"
#include "../app/browser_screen.h"
#include "../app/weather_screen.h"
#include "../app/settings_menu.h"
#include "../app/screensaver.h"
#include "../app/settings_store.h"
#include "../app/news.h"
#include "../browser_engine/include/layout_engine.h"
#include "../hal/battery.h"
#include "../hal/sd_card.h"
#include "../hal/geoip.h"
#include "../hal/touch.h"
#include "../hal/display.h"

namespace SerialConsole {

static const int CMD_BUF_SIZE = 256;
static char s_buf[CMD_BUF_SIZE];
static int s_bufLen = 0;
static bool s_inited = false;

/* 屏幕名 → 屏幕指针变量 查找表
   注意存的是"指针的指针"：Activity 改成懒创建后，屏幕对象在首次进入时才建立，
   nav_open() 会写回这个变量。若像以前那样在这里缓存 lv_obj_t* 快照，
   懒创建的屏永远是 nullptr，串口命令会误报 "screen is null"。 */
struct ScreenEntry {
  const char* name;
  lv_obj_t** scr;
};
static ScreenEntry s_screens[] = {
  {"launcher",  &nav_launcher},
  {"clock",     &nav_clock},
  {"settings",  &nav_settings},
  {"wifi",      &nav_wifi},
  {"game",      &nav_game},
  {"browser",   &nav_browser},
  {"draw",      &nav_draw},
  {"memory",    &nav_memory},
  {"2048",      &nav_2048},
  {"sysinfo",   &nav_sysinfo},
  {"weather",   &nav_weather},
  {"calendar",  &nav_calendar},
  {"games",     &nav_games},
  {"desktop",   &nav_desktop},
{"taskmgr",   &nav_taskmgr},
  {"touchtest", &nav_touchtest},
};
static const int s_screenCount = sizeof(s_screens) / sizeof(s_screens[0]);

static void printHelp() {
  Serial.println("=== Serial Console Commands ===");
  Serial.println("help              - show this help");
  Serial.println("nav <screen>      - navigate to screen");
  Serial.println("back              - 走真实返回桌面路径(诊断后台是否被清)");
  Serial.println("navlist           - 列出仍在内存里的 Activity(后台列表)");
  Serial.println("  screens: launcher clock settings wifi game 2048 browser draw memory sysinfo weather games desktop taskmgr touchtest");
  Serial.println("reboot              - 重启设备（验证 NVS 持久化用）");
  Serial.println("sstore            - 打印 NVS 里记住的设置（息屏/亮度/自动校时/视口）");
  Serial.println("sleep [ms]        - 息屏诊断：看状态/超时/已空闲多久；sleep 30000 直接设超时(0=永不)");
  Serial.println("setpage <id>|back - 设置页二级菜单诊断：直接跳页 / 回上一级");
  Serial.println("browser <url>     - open browser and load URL");
  Serial.println("vp <width>|vp 0   - browser layout viewport (0=auto, read <meta viewport>)");
  Serial.println("flat on|off       - browser 平铺排版 on=不建容器全部平铺 off=还原CSS版面");
Serial.println("flatdump <n>      - 平铺诊断 dump 的行上限（查排在第60行之后的内容，如「下一页」）");
Serial.println("dl                - 把当前页存进 LittleFS；ls = 列出已存页面");
Serial.println("serve|servestop   - 把已存页面用 HTTP 共享出去（PC 浏览器访问设备 IP 看）");
  Serial.println("wifi              - show WiFi status");
  Serial.println("bat               - probe on-board PMIC (battery gauge) + dump regs");
  Serial.println("mem               - show memory info (DRAM + PSRAM, with totals)");
  Serial.println("perf [n]          - render benchmark: n full-screen refreshes -> us/frame");
  Serial.println("perfbw            - bandwidth probe: 定位 flush 那 26ms 到底是谁吃的");
  Serial.println("perfx hide <label|img|canvas> - 隐藏该类控件，配合 perf 做差分定位 draw 开销");
  Serial.println("perftxt [rows]      - 文字渲染微观基准：每字符 us（含同字对照判 cache miss）");
  Serial.println("perflet [n]         - 把画一个汉字拆成查找/位图/缓冲/解包逐项计时");
  Serial.println("perf [n] [h]        - h=只脏顶部 h 行（局部刷新），默认 480=整屏");
  Serial.println("reboot            - restart device");
  Serial.println("version           - show version info");
  Serial.println("news [platform]   - 拉热点新闻(news.orz.ai), 省略则 baidu");
  Serial.println("bnews [platform]  - 走 UI 路径拉热点(后台任务 + 重绘, 验渲染不崩)");
  Serial.println("ime <pinyin>      - 中文输入法验证, 如 ime zhong");
  Serial.println("weather           - 建天气屏 + 拉一次, 打印 HTTP 码和返回体");
  Serial.println("wxauto [on|off]   - 天气后台每小时自动更新开关(off=完全停止)");
  Serial.println("geo|geo reset     - IP 定位; reset = 清缓存强制重定位");
  Serial.println("geotest           - 设备侧实测各反向 geocoding 源(选源用)");
  Serial.println("sd [path] [depth] - TF 卡探测/列目录(懒挂载, 不敲就不碰 SPI)");
  Serial.println("sdbench [KB]      - TF 卡读写速度实测, 默认 256KB");
  Serial.println("traw [s]          - 触摸裸坐标 dump(量四角, 诊断偏移)");
  Serial.println("tcal x0 x1 y0 y1  - 触摸两点校准(默认 0 480 0 480)");
  Serial.println("tprobe            - 读 GT911 配置的 X/Y_OUTPUT_MAX");
  Serial.println("tswap [on|off]    - 触摸 X/Y 交换(面板贴反时试这个)");
  Serial.println("time              - show uptime");
  Serial.println("===============================");
}

static void cmdNav(const char* arg) {
  if (!arg || strlen(arg) == 0) {
    Serial.println("Usage: nav <screen>");
    Serial.println("  screens: launcher clock settings wifi game browser draw memory sysinfo weather");
    return;
  }
  for (int i = 0; i < s_screenCount; i++) {
    if (strcmp(arg, s_screens[i].name) == 0) {
      /* 回主页 = 退出当前应用：销毁除 Launcher 外的所有 Activity */
      if (s_screens[i].scr == &nav_launcher) {
        nav_back_home();
        Serial.println("[Console] nav -> launcher (released others)");
        return;
      }
      /* 懒创建：未加载的屏在这里按需建立 */
      lv_obj_t* scr = nav_open(s_screens[i].scr);
      if (scr) {
        nav_go_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 300);
        Serial.printf("[Console] nav -> %s\n", arg);
      } else {
        Serial.printf("[Console] open '%s' failed\n", arg);
      }
      return;
    }
  }
  Serial.printf("[Console] unknown screen: %s\n", arg);
}

static void cmdBrowser(const char* arg) {
  if (!arg || strlen(arg) == 0) {
    Serial.println("Usage: browser <url>");
    Serial.println("  example: browser http://info.cern.ch/");
    return;
  }
  /* 按需创建浏览器 Activity（懒创建，首次调用时才建屏） */
  lv_obj_t* scr = nav_open(&nav_browser);
  if (!scr) {
    Serial.println("[Console] open browser failed");
    return;
  }
  /* 不在浏览器屏就先切过去 */
  if (lv_scr_act() != scr) {
    nav_go_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 300);
    Serial.println("[Console] switching to browser screen");
  }
  BrowserScreen_navigate(arg);
  Serial.printf("[Console] browser -> %s\n", arg);
}

static void cmdWifi() {
  Serial.printf("[Console] WiFi status: %s\n",
    WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("  SSID: %s\n", WiFi.SSID().c_str());
  }
}

static void cmdMem() {
  /* 用 xPortGetFreeHeapSize() 而非 ESP.getFreeHeap()：
     ESP.getFreeHeap() → heap_caps_get_free_size() 会遍历所有堆并加锁，
     在 ESP32-S3 多堆（DRAM+PSRAM+DMA）环境下锁住 Core1 WiFi 任务导致看门狗超时。
     xPortGetFreeHeapSize() 只读 FreeRTOS 的一个字段，不锁堆。 */
  /* 注意：xPortGetFreeHeapSize() 返回的是含 PSRAM 的"总堆"，
     在 ESP32-S3 上会显示 6MB+ —— 那不是内部 DRAM！
     真实内部 DRAM 要看 heap_caps_get_free_size(MALLOC_CAP_INTERNAL)，
     两者能差 70 倍。之前把它标成 "DRAM" 严重误导排障。 */
  uint32_t freeDram  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  yield();
  uint32_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  yield();
  uint32_t totalHeap = xPortGetFreeHeapSize();
  /* total_size 也是 O(堆个数) 读字段，不遍历块，安全（区别于 get_info） */
  uint32_t allDram   = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
  yield();
  uint32_t allPsram  = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  yield();
  Serial.printf("[Console] DRAM(internal) %u/%u B (used %u, %.0f%%), PSRAM %u/%u B\n",
    (unsigned)freeDram, (unsigned)allDram, (unsigned)(allDram - freeDram),
    allDram ? (float)(allDram - freeDram) * 100.f / (float)allDram : 0.f,
    (unsigned)freePsram, (unsigned)allPsram);
  Serial.printf("[Console] total heap(free, incl.PSRAM): %u\n", (unsigned)totalHeap);
  /* LVGL 对象池（widget 内存）是独立静态池，不走 system DRAM —— 这正是
     "退出浏览器后 DRAM 数字没变"的原因：widget 归还的是这里，不是 DRAM。
     要看 widget 有没有真的释放，必须看这一行。
     lv_mem_monitor 只遍历 LVGL 自己的 tlsf 池，不碰 ESP-IDF 堆锁，安全。 */
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  Serial.printf("[Console] LVGL pool: used %u%% (%u B), free %u B, max block %u B\n",
    (unsigned)mon.used_pct, (unsigned)mon.total_size - (unsigned)mon.free_size,
    (unsigned)mon.free_size, (unsigned)mon.free_biggest_size);
  /* ⚠️ 绝对不要在这里加 heap_caps_get_largest_free_block() / heap_caps_get_info()。
     它们是 O(n) 遍历（multi_heap_get_info → tlsf_walk_pool），持堆锁时间以毫秒计；
     WiFi 收包路径（ppTask → sta_rx_cb → wlanif_input → pbuf_alloc → mem_malloc）
     在另一核等同一把锁 → Interrupt wdt timeout on CPU1 → 整机重启。
     已用 addr2line 实证：PC 停在 multi_heap_get_info_tlsf，见 docs/06。
     heap_caps_get_free_size() 只 O(1) 读字段，实测安全。 */
}

static void cmdVersion() {
  Serial.println("=== Version Info ===");
  Serial.printf("  Chip: ESP32-S3 rev %d\n", ESP.getChipRevision());
  Serial.printf("  CPU freq: %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("  Flash: %u KB\n", ESP.getFlashChipSize() / 1024);
  Serial.printf("  SDK: %s\n", ESP.getSdkVersion());
  Serial.println("  App: geek-terminal v1.0");
  Serial.println("====================");
}

static void cmdTime() {
  Serial.printf("[Console] Uptime: %lu ms (%.1f s)\n",
    (unsigned long)millis(), (float)millis() / 1000.0f);
}

/* ── perf：渲染性能 ──
 * 「静止时 fps=0」不是问题（没脏区就不重绘），真正决定"滑动卡不卡"的是
 * **刷满一屏要多久**：帧时间 -> 理论最大 fps = 1e6 / 帧时间(us)。
 *
 * 做法：连续 N 次 invalidate 整屏 + lv_refr_now() 立即刷新（绕过
 * LV_DISP_DEF_REFR_PERIOD=20ms 的节流）。然后**把 flush_cb 换成空函数再跑一遍**，
 * 得到「只绘制、不往屏上搬」的耗时。两者之差 = 实际把像素推到 RGB 屏的时间。
 * 这一步拆分很重要：前者该优化 CPU/PSRAM，后者受限于 PCLK 物理带宽，
 * 再怎么优化代码也没用 —— 别对着错的那一半使劲。 */
static void dummy_flush_cb(lv_disp_drv_t* disp, const lv_area_t* area,
                          lv_color_t* px) {
  (void)area; (void)px;
  lv_disp_flush_ready(disp);
}

static uint32_t perf_run(lv_disp_t* disp, lv_obj_t* scr, int rounds, int regionH) {
  /* regionH<=0 或 >=480 表示整屏；否则只脏顶部 regionH 行。
     真实交互（点一个 tile、滚一小段）只脏一小块，跟全屏差一个数量级。 */
  bool partial = (regionH > 0 && regionH < 480);
  lv_area_t a;
  a.x1 = 0; a.y1 = 0; a.x2 = 479; a.y2 = partial ? (lv_coord_t)(regionH - 1) : 479;

  if (partial) lv_obj_invalidate_area(scr, &a); else lv_obj_invalidate(scr);
  lv_refr_now(disp);            /* 丢掉第一帧（冷启动），避免拉偏平均值 */
  uint32_t t0 = micros();
  for (int i = 0; i < rounds; i++) {
    if (partial) lv_obj_invalidate_area(scr, &a); else lv_obj_invalidate(scr);
    lv_refr_now(disp);
  }
  return micros() - t0;
}

/* ---- perfbw：带宽体检（把 flush 那 26ms 的账算清楚）----
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

/* ---- perfx：按控件类型隐藏，用差分反推 draw 花在哪 ----
   砍掉 flush 的 25ms 之后 draw 占了整帧 98%（桌面 43ms），而纯写缓冲实测
   只要 1.2ms/屏 —— 说明 draw 贵在每个像素上的计算（圆角遮罩 / 文字 /
   图标 blit），不是贵在写内存。到底是哪一类，必须用差分测，不能猜。 */
static int walkSetHidden(lv_obj_t* o, const lv_obj_class_t* cls, bool hide) {
  int n = 0;
  uint32_t cnt = lv_obj_get_child_cnt(o);
  for (uint32_t i = 0; i < cnt; i++) {
    lv_obj_t* c = lv_obj_get_child(o, i);
    if (!c) continue;
    if (lv_obj_check_type(c, cls)) {
      if (hide) lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_clear_flag(c, LV_OBJ_FLAG_HIDDEN);
      n++;
    }
    n += walkSetHidden(c, cls, hide);
  }
  return n;
}

static void cmdPerfx(const char* arg) {
  lv_obj_t* scr = lv_scr_act();
  if (!scr) { Serial.println("[Perfx] no screen"); return; }

  const char* a = arg ? arg : "";
  if (strncmp(a, "show", 4) == 0) {
    int n = 0;
    n += walkSetHidden(scr, &lv_label_class, false);
    n += walkSetHidden(scr, &lv_img_class, false);
#if LV_USE_CANVAS
    n += walkSetHidden(scr, &lv_canvas_class, false);
#endif
    Serial.printf("[Perfx] restored %d objects\n", n);
    return;
  }

  const char* kind = a;
  if (strncmp(a, "hide ", 5) == 0) kind = a + 5;
  else if (strncmp(a, "hide", 4) == 0) kind = a + 4;

  const lv_obj_class_t* cls = nullptr;
  if (strcmp(kind, "label") == 0) cls = &lv_label_class;
  else if (strcmp(kind, "img") == 0) cls = &lv_img_class;
#if LV_USE_CANVAS
  else if (strcmp(kind, "canvas") == 0) cls = &lv_canvas_class;
#endif
  if (!cls) {
    Serial.println("[Perfx] usage: perfx hide label|img|canvas | perfx show");
    return;
  }
  int n = walkSetHidden(scr, cls, true);
  Serial.printf("[Perfx] hid %d '%s' objects -> now run 'perf' and diff\n", n, kind);
}

/* ------------------------------------------------------------------ *
 * perftxt —— 文字渲染微观基准
 *
 * perfx 的差分只证明了"文字占 draw 的 59%"，但那 25ms 可能花在三处之一：
 *   (a) 每字符的光栅化 + alpha 混合（4bpp 抗锯齿）
 *   (b) UTF-8 解码 + 字形查找（3785 字的 sparse 字体）
 *   (c) label 自身的固定开销（布局 / 长文本换行计算）
 * 做法：在当前屏上盖一层不透明 overlay，逐步往里加料测 delta，
 * 原屏的成本是常量，会被减掉。
 * ------------------------------------------------------------------ */
static uint32_t txtBench(int n) {
  lv_disp_t* disp = lv_disp_get_default();
  uint32_t best = 0xFFFFFFFFu;
  for (int i = 0; i < n; i++) {
    lv_obj_update_layout(lv_scr_act());   /* 刚创建的 widget 坐标是陈旧的 */
    lv_obj_invalidate(lv_scr_act());
    uint32_t t0 = micros();
    lv_refr_now(disp);
    uint32_t d = micros() - t0;
    if (d < best) best = d;
  }
  return best;
}

static void cmdPerfTxt(const char* arg) {
  int rows = (arg && *arg) ? atoi(arg) : 0;
  if (rows < 1) rows = 8;
  lv_obj_t* scr = lv_scr_act();

  /* 20 个汉字 / 30 个 ASCII */
  const char* CN = "\xe6\x9e\x81\xe5\xae\xa2\xe7\xbb\x88\xe7\xab\xaf"      /* 极客终端 */
                   "\xe6\x96\x87\xe5\xad\x97\xe6\xb8\xb2\xe6\x9f\x93"      /* 文字渲染 */
                   "\xe5\x9f\xba\xe5\x87\x86\xe6\xb5\x8b\xe8\xaf\x95"      /* 基准测试 */
                   "\xe6\x96\x87\xe6\x9c\xac\xe4\xb8\x80\xe4\xba\x8c"      /* 文本一二 */
                   "\xe4\xb8\x89\xe5\x9b\x9b\xe4\xba\x94\xe5\x85\xad";     /* 三四五六 */
  const char* EN = "PerfTxtBenchMarkText0123456789Ab";
  /* 同一个汉字「极」重复 20 遍 —— 与 CN 只差"是否命中 cache" */
  const char* CN_SAME =
      "\xe6\x9e\x81\xe6\x9e\x81\xe6\x9e\x81\xe6\x9e\x81\xe6\x9e\x81"
      "\xe6\x9e\x81\xe6\x9e\x81\xe6\x9e\x81\xe6\x9e\x81\xe6\x9e\x81"
      "\xe6\x9e\x81\xe6\x9e\x81\xe6\x9e\x81\xe6\x9e\x81\xe6\x9e\x81"
      "\xe6\x9e\x81\xe6\x9e\x81\xe6\x9e\x81\xe6\x9e\x81\xe6\x9e\x81";
  const int CN_N = 20, EN_N = 30;

  Serial.printf("[PerfTxt] rows=%d  scr=%p\n", rows, scr);
  uint32_t t0 = txtBench(4);

  lv_obj_t* ov = lv_obj_create(scr);
  lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(ov, 480, 480);
  lv_obj_set_pos(ov, 0, 0);
  lv_obj_set_style_bg_color(ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ov, 0, 0);
  lv_obj_set_style_pad_all(ov, 0, 0);
  lv_obj_set_style_radius(ov, 0, 0);
  uint32_t t1 = txtBench(4);

  /* --- 中文 font_zh_16 --- */
  lv_obj_t** labs = (lv_obj_t**)malloc(sizeof(lv_obj_t*) * rows);
  if (!labs) { lv_obj_del(ov); Serial.println("[PerfTxt] OOM"); return; }
  for (int i = 0; i < rows; i++) {
    labs[i] = lv_label_create(ov);
    lv_obj_set_pos(labs[i], 6, 6 + i * 22);
    lv_obj_set_style_text_font(labs[i], &font_zh_16, 0);
    lv_obj_set_style_text_color(labs[i], lv_color_white(), 0);
    lv_label_set_text(labs[i], CN);
  }
  uint32_t t2 = txtBench(4);
  for (int i = 0; i < rows; i++) lv_obj_del(labs[i]);

  /* --- 中文「同一个字」重复 20 遍 ---
     对照实验：若明显快于 20 个不同字 ⟹ 瓶颈是字形数据的 flash XIP cache miss
     （font_zh_16 有 3785 个字形 ~484KB，远大于 S3 的 flash cache，随机命中率极低）。
     若两者一样快 ⟹ 瓶颈在每像素混合，跟 cache 无关，减 bpp 才是正解。 */
  for (int i = 0; i < rows; i++) {
    labs[i] = lv_label_create(ov);
    lv_obj_set_pos(labs[i], 6, 6 + i * 22);
    lv_obj_set_style_text_font(labs[i], &font_zh_16, 0);
    lv_obj_set_style_text_color(labs[i], lv_color_white(), 0);
    lv_label_set_text(labs[i], CN_SAME);
  }
  uint32_t t5 = txtBench(4);
  for (int i = 0; i < rows; i++) lv_obj_del(labs[i]);
  /* --- 纯色矩形 320x128 = 40960 px ---
     像素数与 rows*20 个汉字完全相同（16x16x160），但走的是**无 mask 的纯填充**路径。
     拿它跟中文那一项比：若两者接近 ⟹ 慢在 fill 基础设施；若它快很多 ⟹ 慢在 mask 混合。 */
  {
    int w = 320, h = 128;
    while ((uint32_t)(w * h) < (uint32_t)(rows * 20 * 16 * 16) && h < 460) h++;
    lv_obj_t* rect = lv_obj_create(ov);
    lv_obj_clear_flag(rect, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(rect, w, h);
    lv_obj_set_pos(rect, 0, 200);
    lv_obj_set_style_bg_color(rect, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rect, 0, 0);
    lv_obj_set_style_pad_all(rect, 0, 0);
    lv_obj_set_style_radius(rect, 0, 0);
    uint32_t t6 = txtBench(4);
    uint32_t px = (uint32_t)w * h;
    Serial.printf("[PerfTxt] %-22s %6u  (+%u)  %ux%u=%u px\n", "+solid rect",
                  (unsigned)t6, (unsigned)(t6 - t1), w, h, (unsigned)px);
    Serial.printf("[PerfTxt] per-px    solid=%u ns  CN-glyph=1260 ns  ratio=%.1fx\n",
                  (unsigned)(((t6 - t1) * 1000) / px),
                  1260.0 / (double)(((t6 - t1) * 1000) / px));
    lv_obj_del(rect);
  }

  /* --- 英文 montserrat_16（同样 30 字符，4bpp 抗锯齿） --- */
  for (int i = 0; i < rows; i++) {
    labs[i] = lv_label_create(ov);
    lv_obj_set_pos(labs[i], 6, 6 + i * 22);
    lv_obj_set_style_text_font(labs[i], &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(labs[i], lv_color_white(), 0);
    lv_label_set_text(labs[i], EN);
  }
  uint32_t t3 = txtBench(4);
  for (int i = 0; i < rows; i++) lv_obj_del(labs[i]);

  /* --- 空文本：隔离 label 自身固定开销 --- */
  for (int i = 0; i < rows; i++) {
    labs[i] = lv_label_create(ov);
    lv_obj_set_pos(labs[i], 6, 6 + i * 22);
    lv_obj_set_style_text_font(labs[i], &font_zh_16, 0);
    lv_label_set_text(labs[i], "");
  }
  uint32_t t4 = txtBench(4);
  for (int i = 0; i < rows; i++) lv_obj_del(labs[i]);
  free(labs);
  lv_obj_del(ov);

  uint32_t cnCost = t2 - t1;
  uint32_t enCost = t3 - t1;
  uint32_t fixCost = t4 - t1;
  Serial.println("[PerfTxt] === us/frame (min of 4) ===");
  Serial.printf("[PerfTxt] %-22s %6u\n", "orig screen", (unsigned)t0);
  Serial.printf("[PerfTxt] %-22s %6u  (overlay cost %u)\n", "+empty overlay",
                (unsigned)t1, (unsigned)(t1 - t0));
  Serial.printf("[PerfTxt] %-22s %6u  (+%u)\n", "+CN 20x%d font_zh_16",
                (unsigned)t2, (unsigned)cnCost, rows);
  Serial.printf("[PerfTxt] %-22s %6u  (+%u)\n", "+EN 30x%d montserrat16",
                (unsigned)t3, (unsigned)enCost, rows);
  Serial.printf("[PerfTxt] %-22s %6u  (+%u)\n", "+empty label",
                (unsigned)t4, (unsigned)fixCost);
  Serial.printf("[PerfTxt] %-22s %6u  (+%u)   <-- 同字对照\n", "+CN_SAME 20x%d",
                (unsigned)t5, (unsigned)(t5 - t1), rows);
  Serial.printf("[PerfTxt] per-char  CN=%u us  CN_SAME=%u us  EN=%u us   label-fixed=%u us\n",
                (unsigned)((cnCost - fixCost) / (rows * CN_N)),
                (unsigned)(((t5 - t1) - fixCost) / (rows * CN_N)),
                (unsigned)((cnCost - fixCost) / (rows * CN_N)),
                (unsigned)((enCost - fixCost) / (rows * EN_N)),
                (unsigned)(fixCost / rows));
}

/* ------------------------------------------------------------------ *
 * perflet —— 把一个汉字的绘制拆成 API 级逐项计时
 * ------------------------------------------------------------------ */
static void cmdPerfLet(const char* arg) {
  /* executeLine 在无参数时传 nullptr，atoi(NULL) = LoadProhibited */
  int n = (arg && *arg) ? atoi(arg) : 0;
  if (n < 1) n = 100;
  if (n > 5000) n = 5000;

  /* 极客终端文字渲染基准测试文本一二三四五六 */
  static const uint32_t CN[20] = {
    0x6781, 0x5BA2, 0x7EC8, 0x7AEF, 0x6587, 0x5B57, 0x6E32, 0x67D3,
    0x57FA, 0x51C6, 0x6D4B, 0x8BD5, 0x6587, 0x672C, 0x4E00, 0x4E8C,
    0x4E09, 0x56DB, 0x4E94, 0x516D
  };
  static const uint8_t bpp4[16] = {
    0, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255
  };
  const int M = 20;
  uint32_t total = (uint32_t)n * (uint32_t)M;
  Serial.printf("[PerfLet] n=%d x %d chars = %u calls per item\n", n, M, (unsigned)total);

  lv_font_glyph_dsc_t g;

  /* A: 字形查找 */
  uint32_t t = micros();
  for (int i = 0; i < n; i++)
    for (int j = 0; j < M; j++) lv_font_get_glyph_dsc(&font_zh_16, &g, CN[j], 0);
  uint32_t tA = micros() - t;

  /* B: 位图指针 */
  t = micros();
  for (int i = 0; i < n; i++)
    for (int j = 0; j < M; j++) {
      const uint8_t* b = lv_font_get_glyph_bitmap(&font_zh_16, CN[j]);
      (void)b;
    }
  uint32_t tB = micros() - t;

  /* C: 临时缓冲（draw_letter_normal 每个字都 get/release 一次） */
  t = micros();
  for (int i = 0; i < n; i++)
    for (int j = 0; j < M; j++) {
      void* p = lv_mem_buf_get(256);
      lv_mem_buf_release(p);
    }
  uint32_t tC = micros() - t;

  /* D: 4bpp 解包到 mask_buf（B 已含在内，差值是纯解包） */
  uint8_t* mask = (uint8_t*)lv_mem_buf_get(256);
  if (!mask) { Serial.println("[PerfLet] buf OOM"); return; }
  t = micros();
  for (int i = 0; i < n; i++)
    for (int j = 0; j < M; j++) {
      const uint8_t* map_p = lv_font_get_glyph_bitmap(&font_zh_16, CN[j]);
      if (!map_p) continue;
      int m = 0;
      for (int row = 0; row < 16; row++) {
        uint32_t bitmask = 0xF0;
        uint32_t col_bit = 0;
        for (int col = 0; col < 16; col++) {
          uint8_t px = (*map_p & bitmask) >> (4 - col_bit);
          mask[m] = bpp4[px];
          if (col_bit < 4) { col_bit += 4; bitmask = bitmask >> 4; }
          else { col_bit = 0; bitmask = 0xF0; map_p++; }
          m++;
        }
      }
    }
  uint32_t tD = micros() - t;
  lv_mem_buf_release(mask);

#define ROW(tag, us)                                                          \
  Serial.printf("[PerfLet] %-26s %8u us  %6u ns/char\n", tag,                 \
                (unsigned)(us), (unsigned)(((uint64_t)(us) * 1000) / total))
  ROW("A glyph_dsc (查找)", tA);
  ROW("B glyph_bitmap (指针)", tB);
  ROW("C mem_buf get+release", tC);
  ROW("D 解包4bpp(B含在内)", tD);
  Serial.printf("[PerfLet] D-B = 纯解包              %8u us  %6u ns/char\n",
                (unsigned)(tD - tB),
                (unsigned)(((uint64_t)(tD - tB) * 1000) / total));
  Serial.printf("[PerfLet] 实测整字成本 ~303000 ns/char，上面四项之和 = %u ns\n",
                (unsigned)(((uint64_t)(tA + tD + tC) * 1000) / total));
}

static void cmdPerf(const char* arg) {
  int rounds = (arg && *arg) ? atoi(arg) : 0;
  if (rounds <= 0) rounds = 20;
  if (rounds > 200) rounds = 200;
  /* 第二参数：只脏顶部 N 行（局部刷新）。不传 = 整屏。 */
  int regionH = 0;
  if (arg) {
    const char* sp = strchr(arg, ' ');
    if (sp) regionH = atoi(sp + 1);
  }

  lv_disp_t* disp = lv_disp_get_default();
  lv_obj_t* scr = lv_scr_act();
  if (!disp || !scr || !disp->driver) { Serial.println("[Perf] no display"); return; }

  uint32_t tAll = perf_run(disp, scr, rounds, regionH);

  /* LVGL 8.3 没有导出 lv_disp_flush_cb_t 这个 typedef，用 auto 接住原始
     函数指针，别再自己猜类型名了。 */
  auto realFlush = disp->driver->flush_cb;
  disp->driver->flush_cb = dummy_flush_cb;
  uint32_t tDraw = perf_run(disp, scr, rounds, regionH);
  disp->driver->flush_cb = realFlush;

  uint32_t perAll = tAll / (uint32_t)rounds;
  uint32_t perDraw = tDraw / (uint32_t)rounds;
  int32_t perFlush = (int32_t)perAll - (int32_t)perDraw;
  if (perFlush < 0) perFlush = 0;

  const char* rg = (regionH > 0 && regionH < 480) ? "partial" : "full";
  Serial.printf("[Perf] x%d %s(%d rows)  total=%u us/frame (%.1f fps max)\n", rounds, rg,
                (regionH > 0 && regionH < 480) ? regionH : 480,
                (unsigned)perAll, perAll ? (1000000.0 / (double)perAll) : 0.0);
  Serial.printf("[Perf]      draw =%u us  (%.0f%%)   flush=%d us  (%.0f%%)\n",
                (unsigned)perDraw, perAll ? (100.0 * perDraw / perAll) : 0.0,
                (int)perFlush, perAll ? (100.0 * perFlush / perAll) : 0.0);
  Serial.printf("[Perf] DRAM free=%u PSRAM free=%u obj children=%u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned)lv_obj_get_child_cnt(scr));
  Serial.printf("[Perf] CPU=%u MHz  XTAL=%u MHz\n",
                (unsigned)getCpuFrequencyMhz(), (unsigned)getXtalFrequencyMhz());
}

static void cmdReboot() {
  Serial.println("[Console] rebooting...");
  delay(100);
  ESP.restart();
}

/* 触摸 X/Y 交换开关的当前状态（`tswap` 不带参数时取反） */
static bool s_touchSwap = false;

/* 解析并执行一行命令 */
static void executeLine(char* line) {
  /* 去掉末尾的 \r */
  int len = strlen(line);
  while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) {
    line[--len] = 0;
  }
  if (len == 0) return;

  /* 拆分命令和参数（第一个空格） */
  char* cmd = line;
  char* arg = nullptr;
  char* sp = strchr(line, ' ');
  if (sp) {
    *sp = 0;
    arg = sp + 1;
    /* 跳过连续空格 */
    while (*arg == ' ') arg++;
  }

  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    printHelp();
  } else if (strcmp(cmd, "nav") == 0) {
    cmdNav(arg);
  } else if (strcmp(cmd, "back") == 0) {
    /* 走真实的「返回桌面」路径（带动画版）—— 就是左滑退出 / 顶栏返回走的那条，
       用来复现"应用切到后台后是不是被自动结束了"。 */
    nav_back_home_anim();
    Serial.println("[Console] nav_back_home_anim() returned");
  } else if (strcmp(cmd, "navlist") == 0) {
    NavRunningInfo list[16];
    int n = nav_running_list(list, 16);
    Serial.printf("[Nav] running = %d\n", n);
    for (int i = 0; i < n; i++)
      Serial.printf("   %-12s %6u B %s\n", list[i].id, (unsigned)list[i].bytes,
                    list[i].current ? "<- foreground" : "");
    if (n == 0) Serial.println("   (empty: launcher only)");
  } else if (strcmp(cmd, "sstore") == 0) {
    /* 打印 NVS 里记住的设置（息屏 / 亮度 / 自动校时 / 排版视口） */
    SettingsStore::dump();
  } else if (strcmp(cmd, "sleep") == 0) {
    /* 息屏诊断：sleep —— 看当前状态/超时/已空闲多久；sleep 30000 —— 直接设超时(ms)，
       sleep 0 = 永不。排查"怎么还不息屏"先跑这个。 */
    if (arg && *arg) {
      unsigned long ms = strtoul(arg, nullptr, 10);
      ScreenSaver::setIdleTimeout(ms);
      SettingsStore::saveIdle(ms);   /* 同步给 store，否则 dump 是旧值 / 重启丢失 */
    }
    ScreenSaver::dumpStatus();
  } else if (strcmp(cmd, "setpage") == 0) {
    /* 诊断：直接跳到设置页的某个二级页（等价于点那一行）。
       setpage root|display|general ，setpage back = 回上一级 */
    if (arg && strncmp(arg, "back", 4) == 0) { settings_menu_back(nullptr); return; }
    settings_menu_goto(arg && *arg ? arg : "root");
  } else if (strcmp(cmd, "browser") == 0) {
    cmdBrowser(arg);
  } else if (strcmp(cmd, "vp") == 0) {
    /* 设置浏览器排版视口宽度：vp 1024（桌面）/ vp 0（自动，读 <meta viewport>） */
    if (arg && arg[0] == '0' && (arg[1] == '\0' || arg[1] == ' ')) {
      BrowserScreen_setViewport(0);
    } else {
      int w = (arg && *arg) ? atoi(arg) : 0;
      if (w > 0) BrowserScreen_setViewport(w);
      else Serial.printf("[Console] viewport = %d%s\n", BrowserScreen_getViewport(),
                         BrowserScreen_getViewport() == 0 ? " (auto)" : "");
    }
  } else if (strcmp(cmd, "sdtest") == 0) {
    /* SD 读写自检：写一段已知字节模式再读回比对。
       页面缓存读回来内容对不上时先跑它，分清是 SD 层还是 HTML 层。 */
    SDCard::selfTest((arg && *arg) ? (uint32_t)atoi(arg) : 128);
  } else if (strcmp(cmd, "img") == 0) {
    /* 缩略图开关：img / img on / img off。
       图多的页面会把加载时间拉长（每张一次 TLS 握手），关掉可以救回来。 */
    if (!arg || !*arg) {
      Serial.printf("[Img] images %s\n",
                    BrowserScreen_imagesEnabled() ? "ON" : "OFF");
    } else {
      BrowserScreen_setImages(!(strcmp(arg, "off") == 0 ||
                                strcmp(arg, "0") == 0));
    }
  } else if (strcmp(cmd, "thumb") == 0) {
    /* 缩略图长边上限：thumb 96 / thumb 160。只影响重采样，不影响下载。 */
    BrowserScreen_setThumbPx((arg && *arg) ? atoi(arg) : 96);
  } else if (strcmp(cmd, "imgscan") == 0) {
    /* 本页有几个 <img>、几个取到了地址 —— 图片不显示时第一个该跑的命令 */
    BrowserScreen_imgScan();
  } else if (strcmp(cmd, "dram") == 0) {
    /* 内存账：DRAM / PSRAM / LVGL 池 / 页面缓存各占多少 */
    BrowserScreen_memInfo();
  } else if (strcmp(cmd, "albumview") == 0) {
    /* 相册第 n 张的大图：albumview 1 */
    BrowserScreen_albumView((arg && *arg) ? atoi(arg) : 1);
  } else if (strcmp(cmd, "album") == 0) {
    /* 缓存相册：不点屏也能进 */
    BrowserScreen_album();
  } else if (strcmp(cmd, "imgclose") == 0) {
    /* 关掉看图覆盖层（验"点叉叉退不出去"那条链路） */
    BrowserScreen_imgClose();
  } else if (strcmp(cmd, "imgview") == 0) {
    /* 不开屏也能验全屏看图：imgview 1 */
    BrowserScreen_imgView((arg && *arg) ? atoi(arg) : 1);
  } else if (strcmp(cmd, "imgdl") == 0) {
    /* 验另存：imgdl 1 */
    BrowserScreen_imgDownload((arg && *arg) ? atoi(arg) : 1);
  } else if (strcmp(cmd, "imgtest") == 0) {
    /* 单张图连通性+可解码性验证：imgtest https://.../a.jpg */
    BrowserScreen_imgTest(arg);
  } else if (strcmp(cmd, "seg") == 0) {
    /* 分段渲染诊断：seg = 看状态；seg 2 = 跳到第 2 段；seg next / seg prev */
    if (!arg || !*arg) {
      BrowserScreen_segDump();
    } else if (strcmp(arg, "next") == 0) {
      BrowserScreen_segDump();
      int st = BrowserScreen_segStart();
      BrowserScreen_segGo(st + BrowserScreen_segSize());
    } else if (strcmp(arg, "prev") == 0) {
      BrowserScreen_segGo(BrowserScreen_segStart() - BrowserScreen_segSize());
    } else {
      BrowserScreen_segGo(atoi(arg) * BrowserScreen_segSize());
    }
  } else if (strcmp(cmd, "serve") == 0 || strcmp(cmd, "servestop") == 0) {
    /* 起/停内置页面服务：电脑浏览器打开 http://<板子IP>/ 看存下来的页面 */
    BrowserScreen_serve(strcmp(cmd, "serve") == 0);
  } else if (strcmp(cmd, "news") == 0) {
    /* 串口诊断：直接拉一次热点新闻（阻塞 1~2 秒，只在命令行用）。
       用法：news [platform]  —— platform 省略时是 baidu */
    static NewsItem dbgItems[NEWS_MAX_ITEMS];
    const char* plat = (arg && *arg) ? arg : "baidu";
    int n = news_fetch(plat, dbgItems, NEWS_MAX_ITEMS);
    Serial.printf("[News] %s -> %d items\n", plat, n);
    for (int i = 0; i < n && i < 8; i++)
      Serial.printf("  %d. %s\n     %s\n", i + 1, dbgItems[i].title, dbgItems[i].url);
} else if (strcmp(cmd, "bnews") == 0) {
    /* 走 UI 路径拉热点（后台任务 + 搜索首页重绘），用来验证渲染不崩。
       bnews [platform]，platform 省略时 baidu。 */
    const char* plat = (arg && *arg) ? arg : "baidu";
    Serial.printf("[Console] bnews %s\n", plat);
    if (!nav_browser) nav_open(&nav_browser);
    if (nav_browser) { nav_go(nav_browser); BrowserScreen_news(plat); }
} else if (strcmp(cmd, "ime") == 0) {
    /* 中文输入法验证：ime nihao —— 先看浏览器搜索首页有没有建起来 */
    const char* py = (arg && *arg) ? arg : "ni";
    if (!nav_browser) nav_open(&nav_browser);
    if (nav_browser) { nav_go(nav_browser); BrowserScreen_ime(py); }
} else if (strcmp(cmd, "sd") == 0) {
    /* TF/microSD 卡探测：sd（列根目录） / sd /path 2（列指定目录，深度 2）
       ⚠️ 懒挂载：不敲这个命令就不碰 SPI，免得跟 LCD 抢总线。 */
    if (!SDCard::begin()) return;
    const char* p = (arg && *arg) ? arg : "/";
    SDCard::listDir(p, 1);
} else if (strcmp(cmd, "sdbench") == 0) {
    /* SD 卡读写速度实测：sdbench（默认 256KB） / sdbench 512
       用途：量出「应用放 SD 卡、按需加载」读一个 app 要多久，好据实决策。
       ⚠️ arg 可能是 nullptr，atoi 前必须判空。 */
    uint32_t kb = (arg && *arg) ? (uint32_t)atoi(arg) : 256;
    SDCard::bench(kb);
} else if (strcmp(cmd, "weather") == 0) {
    /* 天气诊断：weather —— 建屏 + 拉一次，打印 HTTP 码和返回体 */
    if (!nav_weather) nav_open(&nav_weather);
    if (nav_weather) { nav_go(nav_weather); WeatherScreen_fetchNow(arg); }
} else if (strcmp(cmd, "wxauto") == 0) {
    /* 天气后台自动更新开关。off = 完全停止（任务用 portMAX_DELAY 睡，不轮询） */
    if (!arg || !arg[0]) {
      Serial.printf("[Weather] auto refresh = %s\n",
                    WeatherScreen_auto() ? "on" : "off");
    } else if (strcmp(arg, "on") == 0 || strcmp(arg, "1") == 0) {
      WeatherScreen_setAuto(true);
    } else if (strcmp(arg, "off") == 0 || strcmp(arg, "0") == 0) {
      WeatherScreen_setAuto(false);
    } else {
      Serial.println("Usage: wxauto [on|off]");
    }
  } else if (strcmp(cmd, "geo") == 0) {
    /* IP 定位：geo（用缓存/联网） / geo reset（清缓存强制重定位） */
    if (arg && strncmp(arg, "reset", 5) == 0) {
      GeoIP::reset();
      Serial.println("[GeoIP] cache cleared");
    }
    bool ok = GeoIP::locate(true);
    Serial.printf("[GeoIP] ok=%d city=%s region=%s lat=%.4f lon=%.4f age=%us\n",
                  (int)ok, GeoIP::city(), GeoIP::region(), GeoIP::lat(),
                  GeoIP::lon(), (unsigned)GeoIP::ageSec());
} else if (strcmp(cmd, "geotest") == 0) {
    /* 反向 geocoding 选源：在设备侧实测各源的 HTTP 码。
       PC 走代理（ip.sb 在 PC 上返回 Paris），PC 结果不作数，必须设备侧测。 */
    GeoIP::probe();
} else if (strcmp(cmd, "ls") == 0) {
    BrowserScreen_listPages();
  } else if (strcmp(cmd, "dl") == 0) {
    /* 把当前页 HTML 存进 LittleFS（与底栏「下载」键同一条路径）。
       页面必须还在缓存里（5 分钟 TTL），否则先刷新再 dl。 */
    BrowserScreen_download();
} else if (strcmp(cmd, "traw") == 0) {
    /* 触摸裸坐标 dump：traw（默认 10 秒） / traw 20
       用途：量四角裸值 -> 填 tcal。诊断"触摸偏移"的唯一可信手段。 */
    uint32_t ms = (arg && *arg) ? (uint32_t)atoi(arg) : 10;
    if (ms == 0) ms = 10;
    Touch::dump(ms * 1000);
} else if (strcmp(cmd, "tcal") == 0) {
    /* 触摸两点校准：tcal <rawX0> <rawX1> <rawY0> <rawY1>
       例：四角裸值 X 20..460、Y 15..455 -> tcal 20 460 15 455
       不带参数 = 恢复默认 0 480 0 480。 */
    int a0 = 0, a1 = 480, b0 = 0, b1 = 480;
    if (arg && *arg) {
      if (sscanf(arg, "%d %d %d %d", &a0, &a1, &b0, &b1) != 4) {
        Serial.println("[Console] tcal 需要 4 个数：tcal x0 x1 y0 y1");
        return;
      }
    }
    Touch::setCal(a0, a1, b0, b1);
} else if (strcmp(cmd, "tswap") == 0) {
    /* X/Y 交换：tswap（切换）/ tswap on / tswap off */
    bool on = true;
    if (arg && *arg) on = !(strcmp(arg, "off") == 0 || strcmp(arg, "0") == 0);
    else             on = !s_touchSwap;
    s_touchSwap = on;
    Touch::setSwap(on);
} else if (strcmp(cmd, "tprobe") == 0) {
    /* 读 GT911 配置里的 X/Y_OUTPUT_MAX —— 不是 480 就是偏移根因 */
    Touch::probe();
} else if (strcmp(cmd, "flatdump") == 0) {
    int n = (arg && *arg) ? atoi(arg) : 0;
    if (n <= 0) n = 60;
    layout_set_flat_dump_limit(n);
    Serial.printf("[Console] flat dump limit = %d\n", n);
  } else if (strcmp(cmd, "flat") == 0) {
    /* 浏览器排版模式：flat on（默认，平铺）/ flat off（还原 CSS 版面）。
       改完要重新加载页面才生效。 */
    if (!arg || arg[0] == '\0') {
      Serial.printf("[Console] flat layout = %s\n",
                    layout_get_flat_mode() ? "on (平铺)" : "off (还原 CSS)");
    } else if (strcmp(arg, "on") == 0 || strcmp(arg, "1") == 0) {
      layout_set_flat_mode(true);
      Serial.println("[Console] flat = on：不建容器，全部平铺（重新加载页面生效）");
    } else if (strcmp(arg, "off") == 0 || strcmp(arg, "0") == 0) {
      layout_set_flat_mode(false);
      Serial.println("[Console] flat = off：等比缩放 + 横向钳制（重新加载页面生效）");
    } else {
      Serial.println("[Console] 用法: flat on | flat off");
    }
  } else if (strcmp(cmd, "bat") == 0) {
    /* 探测板载电源管理 IC 并打印寄存器（用于确认有没有电量计 / 校准电量映射） */
    Battery::dump();
  } else if (strcmp(cmd, "wifi") == 0) {
    cmdWifi();
  } else if (strcmp(cmd, "mem") == 0) {
    cmdMem();
  } else if (strcmp(cmd, "perf") == 0) {
    cmdPerf(arg);
  } else if (strcmp(cmd, "perfbw") == 0) {
    cmdPerfBW(arg);
  } else if (strcmp(cmd, "perfx") == 0) {
    cmdPerfx(arg);
  } else if (strcmp(cmd, "perftxt") == 0) {
    cmdPerfTxt(arg);
  } else if (strcmp(cmd, "perflet") == 0) {
    cmdPerfLet(arg);
  } else if (strcmp(cmd, "reboot") == 0) {
    cmdReboot();
  } else if (strcmp(cmd, "version") == 0) {
    cmdVersion();
  } else if (strcmp(cmd, "time") == 0) {
    cmdTime();
  } else {
    Serial.printf("[Console] unknown command: %s (type 'help')\n", cmd);
  }
}

void begin() {
  s_bufLen = 0;
  s_buf[0] = 0;
  s_inited = true;

  /* 屏指针变量已在 s_screens 里静态绑定（存的是 &nav_xxx），无需在此填充。
     应用屏改为懒创建后，这里若缓存快照会在 nav_open 建好屏后仍是 nullptr。 */

  Serial.println("\n[Console] Serial Console ready. Type 'help' for commands.");
}

void tick() {
  if (!s_inited) return;

  /* 逐字符读取串口数据，遇到换行符执行命令 */
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (s_bufLen > 0) {
        s_buf[s_bufLen] = 0;
        executeLine(s_buf);
        s_bufLen = 0;
        s_buf[0] = 0;
      }
    } else if (s_bufLen < CMD_BUF_SIZE - 1) {
      s_buf[s_bufLen++] = c;
    }
    /* 缓冲区满时自动执行，防止溢出 */
    if (s_bufLen >= CMD_BUF_SIZE - 1) {
      s_buf[s_bufLen] = 0;
      executeLine(s_buf);
      s_bufLen = 0;
      s_buf[0] = 0;
    }
  }
}

}  // namespace SerialConsole

#else  /* SERIAL_CONSOLE_ENABLED == 0 */

namespace SerialConsole {
void begin() {}
void tick() {}
}  // namespace SerialConsole

#endif