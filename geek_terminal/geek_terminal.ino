/* Geek Terminal - ESP32-4848S040 极客多功能终端 */
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

#define GFX_BL 38

Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
  39 /* CS */, 48 /* SCK */, 47 /* SDA */,
  18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
  11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
  8 /* G0 */, 20 /* G1 */, 3 /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
  4 /* B0 */, 5 /* B1 */, 6 /* B2 */, 7 /* B3 */, 15 /* B4 */
);

Arduino_ST7701_RGBPanel *gfx = new Arduino_ST7701_RGBPanel(
  bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */,
  true /*IPS */, 480 /* width */, 480 /* height */,
  st7701_type1_init_operations, sizeof(st7701_type1_init_operations), true /* BGR */,
  10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
  10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */
);

#include "touch.h"

static uint32_t screenWidth, screenHeight;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
  lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  if (touch_has_signal()) {
    if (touch_touched()) {
      data->state = LV_INDEV_STATE_PR;
      data->point.x = touch_last_x;
      data->point.y = touch_last_y;
    } else if (touch_released()) {
      data->state = LV_INDEV_STATE_REL;
    }
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void buildUi() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a0a), LV_PART_MAIN);

  /* 标题 */
  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "GEEK TERMINAL");
  lv_obj_set_style_text_color(title, lv_color_hex(0x00ff88), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

  /* 副标题 */
  lv_obj_t *sub = lv_label_create(scr);
  lv_label_set_text(sub, "ESP32-S3 480x480 ST7701");
  lv_obj_set_style_text_color(sub, lv_color_hex(0x888888), LV_PART_MAIN);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 75);

  /* 状态卡片 */
  lv_obj_t *card = lv_obj_create(scr);
  lv_obj_set_size(card, 400, 200);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 20);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1a1a2e), LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x00ff88), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);

  lv_obj_t *info = lv_label_create(card);
  lv_label_set_text(info,
    "#00ff88 CPU:#ffffff ESP32-S3\n"
    "#00ff88 RAM:#ffffff 512KB+8MB\n"
    "#00ff88 Flash:#ffffff 16MB\n"
    "#00ff88 Display:#ffffff ST7701 RGB\n"
    "#00ff88 Touch:#ffffff GT911\n"
    "#00ff88 Status:#00ff88 READY");
  lv_label_set_recolor(info, true);
  lv_obj_align(info, LV_ALIGN_TOP_LEFT, 10, 10);

  /* 底部提示 */
  lv_obj_t *hint = lv_label_create(scr);
  lv_label_set_text(hint, "v0.1 | LVGL 8.x | Arduino");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x444444), LV_PART_MAIN);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -15);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Geek Terminal Boot ===");

  touch_init();
  gfx->begin(16000000);
  gfx->fillScreen(BLACK);

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  lv_init();
  screenWidth = gfx->width();
  screenHeight = gfx->height();

  disp_draw_buf = (lv_color_t *)heap_caps_malloc(
    sizeof(lv_color_t) * screenWidth * 200, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!disp_draw_buf) {
    Serial.println("[FATAL] LVGL buf alloc failed!");
    while (true) delay(1000);
  }

  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, screenWidth * 200);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  buildUi();
  Serial.println("[BOOT] Geek Terminal ready");
}

void loop() {
  lv_timer_handler();
  delay(5);
}