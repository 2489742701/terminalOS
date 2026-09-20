#include "display.h"
#include "../config/pins.h"

Arduino_ESP32RGBPanel* Display::bus = nullptr;
Arduino_ST7701_RGBPanel* Display::panel = nullptr;
Arduino_GFXClass* Display::gfx = nullptr;
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

  gfx = new Arduino_GFXClass(panel, panel, nullptr, nullptr);

  if (!gfx->begin(RGB_BUS_SPEED)) {
    Serial.println("[Display] init failed");
    return false;
  }

  setBacklight(true);
  gfx->fillScreen(BLACK);
  initialized = true;
  Serial.println("[Display] init ok");
  return true;
}

Arduino_GFXClass* Display::getGfx() { return gfx; }

void Display::setBacklight(bool on) {
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, on ? HIGH : LOW);
}