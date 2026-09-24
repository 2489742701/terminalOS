#include "app.h"
#include "../hal/display.h"
#include "../hal/touch.h"
#include "../hal/serial_console.h"
#include "../config/pins.h"
#include <lvgl.h>
#include "screensaver.h"
#include "settings_store.h"
#include "nav.h"
#include "app_registry.h"
#include "welcome.h"
#include "launcher.h"
#include "clock.h"
#include "settings.h"
#include "wifi_screen.h"
#include "game_screen.h"
#include "browser_screen.h"
#include "draw_screen.h"
#include "memory_screen.h"
#include "sysinfo_screen.h"
#include "weather_screen.h"
#include "taskmgr_screen.h"
#include <WiFi.h>
#include <esp_heap_caps.h>
#include "../hal/ntp_time.h"

/* ── 开机水位线：内部 DRAM 到底被谁吃掉 ──
 * 只调 heap_caps_get_free_size()（O(堆个数) 读字段，不遍历块，不长时间持锁）。
 * ⚠️ 绝不用 heap_caps_get_info() / get_largest_free_block()：O(块数) 持堆锁，
 *    会撞上另一核 WiFi 收包的 malloc → wdt 重启（docs/06 已实证）。 */
namespace {
uint32_t g_markPrev = 0;
}
static void memMark(const char* stage) {
  uint32_t f = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  Serial.printf("[Mem] %-24s DRAM free=%7u  (%+d)\n", stage, (unsigned)f,
                g_markPrev ? (int)(f - g_markPrev) : 0);
  g_markPrev = f;
}

// LVGL 显示缓冲
static lv_disp_draw_buf_t drawBuf;
static lv_color_t* dispBuf1 = nullptr;
static lv_color_t* dispBuf2 = nullptr;
/* direct mode：draw_buf 直接指向 panel 的 framebuffer，LVGL 在显存上作画，
   flush 不再搬运。实测能省掉整帧 25ms（占原先 73ms 的三分之一）。
   为 false 时回退到老的 "PSRAM 双缓冲 + 拷贝" 路径。 */
static bool g_directMode = false;

// 全局屏幕指针（导航）
lv_obj_t* nav_launcher = nullptr;
lv_obj_t* nav_clock = nullptr;
lv_obj_t* nav_settings = nullptr;
lv_obj_t* nav_wifi = nullptr;
lv_obj_t* nav_game = nullptr;
lv_obj_t* nav_browser = nullptr;
lv_obj_t* nav_draw = nullptr;
lv_obj_t* nav_memory = nullptr;
lv_obj_t* nav_2048 = nullptr;
lv_obj_t* nav_sysinfo = nullptr;
lv_obj_t* nav_weather = nullptr;
lv_obj_t* nav_calendar = nullptr;
lv_obj_t* nav_games = nullptr;
lv_obj_t* nav_desktop = nullptr;
lv_obj_t* nav_taskmgr = nullptr;
lv_obj_t* nav_touchtest = nullptr;

void App::dispFlush(lv_disp_drv_t* disp, const lv_area_t* area,
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

void App::touchRead(lv_indev_drv_t* indev, lv_indev_data_t* data) {
  // I2C 轮询 ~10ms 去抖：窗口内直接复用上次结果，避免中断风暴挤占 PSRAM 带宽
  static uint32_t lastMs = 0;
  static bool lastDown = false;
  static int lastX = 0, lastY = 0;

  uint32_t now = millis();
  if (now - lastMs >= 8) {
    lastMs = now;
    int x, y;
    lastDown = Touch::hasSignal() && Touch::touched(x, y);
    if (lastDown) {
      lastX = x;
      lastY = y;
      ScreenSaver::notifyActivity();  // 任何触摸按下都重置空闲计时
    }
  }

  if (lastDown) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = lastX;
    data->point.y = lastY;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

bool App::begin() {
  Serial.printf("[Mem] internal DRAM heap total=%u B (静态 .bss/.data 不在这本账里)\n",
                (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
  memMark("0 enter App::begin");

  // 1. 初始化显示
  if (!Display::begin()) return false;
  memMark("1 display init");

  // 2. 初始化触摸
  Touch::begin();
  memMark("2 touch init");

  // 3. 初始化 LVGL
  lv_init();
  memMark("3 lv_init");
  Serial.printf("[App] LV_COLOR_DEPTH=%d LV_COLOR_16_SWAP=%d sizeof(lv_color_t)=%d\n", LV_COLOR_DEPTH, LV_COLOR_16_SWAP, (int)sizeof(lv_color_t));
  /* 探针：从 LVGL 池里拿一块，看地址落在哪儿。
     外扩 RAM 映射区 0x3C000000–0x3DFFFFFF；内部 DRAM 在 0x3FC8xxxx。 */
  {
    void *probe = lv_mem_alloc(64);
    uint32_t a = (uint32_t)probe;
    const char *where = (a >= 0x3C000000u && a < 0x3E000000u) ? "PSRAM" : "internal DRAM";
    Serial.printf("[App] LVGL pool probe ptr=%p (%s)\n", probe, probe ? where : "NULL!");
    if (probe) lv_mem_free(probe);
  }

  /* 4. 显示缓冲 —— 首选内部 DRAM，不够才回退 PSRAM。
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
    Serial.printf("[App] LVGL buf INTERNAL DRAM: %u px (%u B) x2  ptr=%p\n",
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
      Serial.printf("[App] LVGL buf PSRAM: %u px (%u B) x2  ptr=%p\n",
                    (unsigned)bufSize, (unsigned)(sizeof(lv_color_t) * bufSize),
                    dispBuf1);
    }
  }
  if (!dispBuf1) {
    Serial.println("[App] LVGL buffer alloc failed");
    return false;
  }
  lv_disp_draw_buf_init(&drawBuf, dispBuf1, dispBuf2, bufSize);

  // 5. 注册 LVGL 显示驱动
  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = SCREEN_WIDTH;
  dispDrv.ver_res = SCREEN_HEIGHT;
  dispDrv.flush_cb = dispFlush;
  dispDrv.draw_buf = &drawBuf;
  if (g_directMode) dispDrv.direct_mode = 1;   // 告诉 LVGL：别拷，就在 fb 上画
  lv_disp_drv_register(&dispDrv);
  memMark(g_directMode ? "4 disp drv (direct mode)" : "4 disp drv (PSRAM buf)");

  // 6. 注册 LVGL 触摸驱动
  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchRead;
  lv_indev_drv_register(&indevDrv);


  /* 7. 只常驻 Launcher；其余应用改为按需创建（见 nav.cpp 的 Activity 注册表）。
        10 屏常驻会把内部 DRAM 压到 ~93KB，浏览器连 48KB 后台任务栈都申请不到。
        浏览器的后台任务必须在屏幕创建之前建好：此时 DRAM 最充足、尚无碎片。 */
  BrowserScreen_preinit();
  memMark("5 browser fetch task");

  /* 桌面图标开关的默认值先落 NVS，再建 launcher ——
     launcher 画磁贴时要读可见性，晚一步就会读到未初始化的命名空间。 */
  appreg_init();
  memMark("5.5 app registry");

  nav_launcher = LauncherScreen_create();
  memMark("6 launcher screen");
  lv_obj_t* welcome = WelcomeScreen_create();
  lv_scr_load(welcome);
  memMark("7 welcome screen");

  WiFi.begin("NETGEAR", "13357728293");
  memMark("8 WiFi.begin");

  /* 8.5 网络校时：建一个常驻后台任务（只建一次）。联网后自动对时，
         每 6 小时重对一次；设置页的「校准时间」按钮只是 notify 唤醒它。
         任务栈 4KB，从内部 DRAM 出 —— 必须早建（此时碎片最少）。 */
  NtpTime::begin();
  memMark("8.5 NtpTime::begin");

  // 8. 息屏/锁屏：解锁后回到 launcher（或由 returnScr 回到进入 DIM 时的屏）
  ScreenSaver::init(nav_launcher);

  /* 8.2 恢复用户设置（NVS）：息屏超时 / 亮度 / 自动校时 / 排版视口。
     必须在 ScreenSaver::init 之后 —— loadAll 会把值写进各模块。 */
  SettingsStore::loadAll();

  Serial.println("[App] init ok");

  // 9. 串口命令监听（调试用，发行版把 SERIAL_CONSOLE_ENABLED 置 0）
  SerialConsole::begin();

  return true;
}

void App::loop() {
  SerialConsole::tick();
  lv_timer_handler();
  ScreenSaver::tick();
  lv_obj_t* act = lv_scr_act();
  /* 诊断：监测 nav_browser 有效性变化 */
  static lv_obj_t* last_act = nullptr;
  if (act != last_act) {
    Serial.printf("[App] screen changed: %p -> %p, nav_browser=%p valid=%d\n",
      last_act, act, nav_browser, nav_browser ? (int)lv_obj_is_valid(nav_browser) : 0);
    last_act = act;
  }
  if (act == nav_clock) ClockScreen_update();
  /* nav_wifi 可能已被释放（按需创建），判空后再 tick */
  if (nav_wifi && (act == nav_wifi || WifiScreen_isConnecting())) WifiScreen_tick();
  if (act == nav_game) GameScreen_tick();
  if (act == nav_browser) BrowserScreen_tick();
  if (act == nav_sysinfo) SysInfoScreen_tick();
  if (act == nav_weather) WeatherScreen_tick();
  if (act == nav_taskmgr) TaskMgrScreen_tick();
}
