#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#define GFX_BL 38

Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
  39, 48, 47, 18, 17, 16, 21,
  11, 12, 13, 14, 0,
  8, 20, 3, 46, 9, 10,
  4, 5, 6, 7, 15
);

Arduino_ST7701_RGBPanel *gfx = new Arduino_ST7701_RGBPanel(
  bus, GFX_NOT_DEFINED, 0, true, 480, 480,
  st7701_type1_init_operations, sizeof(st7701_type1_init_operations), true,
  10, 8, 50, 10, 8, 20
);

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== DISPLAY TEST ===");

  gfx->begin(16000000);

  Serial.println("[TEST] gfx begin ok");

  gfx->fillScreen(BLACK);

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  Serial.println("[TEST] backlight on");

  gfx->fillRect(0, 0, 160, 160, RED);
  gfx->fillRect(160, 0, 160, 160, GREEN);
  gfx->fillRect(320, 0, 160, 160, BLUE);
  gfx->fillRect(0, 160, 160, 160, YELLOW);
  gfx->fillRect(160, 160, 160, 160, CYAN);
  gfx->fillRect(320, 160, 160, 160, MAGENTA);
  gfx->fillRect(0, 320, 480, 160, WHITE);
  Serial.println("[TEST] color blocks drawn");
}

void loop() {
  Serial.println("[TEST] alive");
  delay(2000);
}
