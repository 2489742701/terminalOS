#include "display.h"
#include "../config/pins.h"

static const uint8_t BL_LEDC_CH = 0;  // 背光 LEDC 通道
static bool blPwmReady = false;

Arduino_ESP32RGBPanel* Display::bus = nullptr;
Arduino_ST7701_RGBPanel* Display::panel = nullptr;
Arduino_GFX* Display::gfx = nullptr;
bool Display::initialized = false;

bool Display::begin() {
  if (initialized) return true;

  // RGB 并行总线
  bus = new Arduino_ESP32RGBPanel(
      PIN_CS, PIN_SCK, PIN_SDA,
      PIN_DE, PIN_VSYNC, PIN_HSYNC, PIN_PCLK,
      PIN_R0, PIN_R1, PIN_R2, PIN_R3, PIN_R4,
      PIN_G0, PIN_G1, PIN_G2, PIN_G3, PIN_G4, PIN_G5,
      PIN_B0, PIN_B1, PIN_B2, PIN_B3, PIN_B4);

  // ST7701 面板控制器
  panel = new Arduino_ST7701_RGBPanel(
      bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */,
      true /* IPS */, SCREEN_WIDTH, SCREEN_HEIGHT,
      st7701_type1_init_operations, sizeof(st7701_type1_init_operations),
      true /* BGR */,
      HSYNC_FRONT_PORCH, HSYNC_PULSE_WIDTH, HSYNC_BACK_PORCH,
      VSYNC_FRONT_PORCH, VSYNC_PULSE_WIDTH, VSYNC_BACK_PORCH);

  gfx = panel;

  gfx->begin(RGB_BUS_SPEED);

  setBacklight(true);
  gfx->fillScreen(BLACK);
  initialized = true;
  Serial.println("[Display] init ok");
  return true;
}

Arduino_GFX* Display::getGfx() { return gfx; }

void Display::initBacklightPWM() {
  if (blPwmReady) return;
  ledcSetup(BL_LEDC_CH, 5000, 8);  // 5kHz, 8-bit 分辨率
  ledcAttachPin(PIN_BL, BL_LEDC_CH);
  blPwmReady = true;
}

void Display::setBacklight(bool on) {
  initBacklightPWM();
  ledcWrite(BL_LEDC_CH, on ? 255 : 0);
}

void Display::setBacklightLevel(uint8_t percent) {
  initBacklightPWM();
  if (percent > 100) percent = 100;
  ledcWrite(BL_LEDC_CH, (uint32_t)percent * 255 / 100);
}