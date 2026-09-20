#include "app.h"
#include "../hal/display.h"
#include "../hal/touch.h"
#include "../config/pins.h"
#include <lvgl.h>

// LVGL 显示缓冲（双缓冲，每缓冲 100 行）
static lv_disp_draw_buf_t drawBuf;
static lv_color_t* dispBuf1 = nullptr;
static lv_color_t* dispBuf2 = nullptr;

// 极客终端主界面（临时验证用，后续替换为 Launcher 菜单）
static lv_obj_t* mainLabel;
static lv_obj_t* touchLabel;

void App::dispFlush(lv_disp_drv_t* disp, const lv_area_t* area,
                    lv_color_t* colorP) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  Display::getGfx()->draw16bitRGBBitmap(area->x1, area->y1,
                                        (uint16_t*)&colorP->full, w, h);
  lv_disp_flush_ready(disp);
}

void App::touchRead(lv_indev_drv_t* indev, lv_indev_data_t* data) {
  int x, y;
  if (Touch::hasSignal() && Touch::touched(x, y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
    // 实时显示触摸坐标（验证用）
    if (touchLabel) {
      lv_label_set_text_fmt(touchLabel, "TOUCH  X:%d  Y:%d", x, y);
    }
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

  // 4. 分配显示缓冲（PSRAM）
  uint32_t bufSize = SCREEN_WIDTH * 100;
  dispBuf1 = (lv_color_t*)heap_caps_malloc(sizeof(lv_color_t) * bufSize,
                                           MALLOC_CAP_SPIRAM);
  dispBuf2 = (lv_color_t*)heap_caps_malloc(sizeof(lv_color_t) * bufSize,
                                           MALLOC_CAP_SPIRAM);
  if (!dispBuf1 || !dispBuf2) {
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
  lv_disp_drv_register(&dispDrv);

  // 6. 注册 LVGL 触摸驱动
  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchRead;
  lv_indev_drv_register(&indevDrv);

  // 7. 构建验证界面（极客风）
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A0A), LV_PART_MAIN);

  // 标题
  mainLabel = lv_label_create(scr);
  lv_label_set_text(mainLabel,
                    "#00FF00 GEEK# #FFFFFF TERMINAL#\n"
                    "#888888 ESP32-S3 | 480x480 | ST7701#\n\n"
                    "#00FF00 HARDWARE OK#\n"
                    "#00FF00 LVGL 8.3 READY#\n\n"
                    "#FFFF00 TOUCH TO TEST#");
  lv_label_set_recolor(mainLabel, true);
  lv_obj_set_style_text_font(mainLabel, &lv_font_montserrat_18, 0);
  lv_obj_align(mainLabel, LV_ALIGN_TOP_MID, 0, 40);

  // 触摸坐标显示
  touchLabel = lv_label_create(scr);
  lv_label_set_text(touchLabel, "TOUCH  --");
  lv_obj_set_style_text_font(touchLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(touchLabel, lv_color_hex(0x00FFFF), 0);
  lv_obj_align(touchLabel, LV_ALIGN_BOTTOM_MID, 0, -40);

  Serial.println("[App] init ok");
  return true;
}

void App::loop() { lv_timer_handler(); }