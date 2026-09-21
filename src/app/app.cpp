#include "app.h"
#include "../hal/display.h"
#include "../hal/touch.h"
#include "../config/pins.h"
#include <lvgl.h>
#include "screensaver.h"
#include "nav.h"
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


// LVGL 显示缓冲（PSRAM 双缓冲）
static lv_disp_draw_buf_t drawBuf;
static lv_color_t* dispBuf1 = nullptr;
static lv_color_t* dispBuf2 = nullptr;

// 全局屏幕指针（导航）
lv_obj_t* nav_launcher = nullptr;
lv_obj_t* nav_clock = nullptr;
lv_obj_t* nav_settings = nullptr;
lv_obj_t* nav_wifi = nullptr;
lv_obj_t* nav_game = nullptr;
lv_obj_t* nav_browser = nullptr;
lv_obj_t* nav_draw = nullptr;
lv_obj_t* nav_memory = nullptr;
lv_obj_t* nav_sysinfo = nullptr;
lv_obj_t* nav_weather = nullptr;

void App::dispFlush(lv_disp_drv_t* disp, const lv_area_t* area,
                    lv_color_t* colorP) {
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
  if (now - lastMs >= 10) {
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
  // 1. 初始化显示
  if (!Display::begin()) return false;

  // 2. 初始化触摸
  Touch::begin();

  // 3. 初始化 LVGL
  lv_init();
  Serial.printf("[App] LV_COLOR_DEPTH=%d LV_COLOR_16_SWAP=%d sizeof(lv_color_t)=%d\n", LV_COLOR_DEPTH, LV_COLOR_16_SWAP, (int)sizeof(lv_color_t));

  // 4. 分配显示缓冲（PSRAM 双缓冲，每缓冲 120 行 ≈ 1/4 屏）
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
  Serial.printf("[App] buf pixels=%u bytes=%u ptr=%p\n", (unsigned)bufSize,
                (unsigned)(sizeof(lv_color_t) * bufSize), dispBuf1);
  lv_disp_draw_buf_init(&drawBuf, dispBuf1, dispBuf2, bufSize);

  // 5. 注册 LVGL 显示驱动
  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = SCREEN_WIDTH;
  dispDrv.ver_res = SCREEN_HEIGHT;
  dispDrv.flush_cb = dispFlush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  // 6. 注册 LVGL 触摸驱动
  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchRead;
  lv_indev_drv_register(&indevDrv);


  // 7. 构建各屏并接线导航（黑底 + 白色线条图标）
  nav_clock = ClockScreen_create();
  nav_settings = SettingsScreen_create();
  nav_wifi = WifiScreen_create();
  nav_game = GameScreen_create();
  nav_browser = BrowserScreen_create();
  nav_draw = DrawScreen_create();
  nav_memory = MemoryScreen_create();
  nav_sysinfo = SysInfoScreen_create();
  nav_weather = WeatherScreen_create();
  nav_launcher = LauncherScreen_create();
  lv_obj_t* welcome = WelcomeScreen_create();
  lv_scr_load(welcome);

  // 8. 息屏/锁屏：解锁后回到 launcher（或由 returnScr 回到进入 DIM 时的屏）
  ScreenSaver::init(nav_launcher);

  Serial.println("[App] init ok");
  return true;
}

void App::loop() {
  lv_timer_handler();
  ScreenSaver::tick();
  lv_obj_t* act = lv_scr_act();
  if (act == nav_clock) ClockScreen_update();
  if (act == nav_wifi) WifiScreen_tick();
  if (act == nav_game) GameScreen_tick();
  if (act == nav_browser) BrowserScreen_tick();
  if (act == nav_sysinfo) SysInfoScreen_tick();
  if (act == nav_weather) WeatherScreen_tick();
}
