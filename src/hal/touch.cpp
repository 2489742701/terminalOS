#include "touch.h"
#include "../config/pins.h"
#include "../hal/display.h"

TAMC_GT911* Touch::ts = nullptr;
bool Touch::initialized = false;

bool Touch::begin() {
  if (initialized) return true;

  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  ts = new TAMC_GT911(PIN_TOUCH_SDA, PIN_TOUCH_SCL, PIN_TOUCH_INT,
                      PIN_TOUCH_RST, SCREEN_WIDTH, SCREEN_HEIGHT);
  ts->begin();
  ts->setRotation(ROTATION_NORMAL);
  initialized = true;
  Serial.println("[Touch] init ok");
  return true;
}

bool Touch::hasSignal() { return true; }

bool Touch::touched(int& x, int& y) {
  if (!ts) return false;
  ts->read();
  if (!ts->isTouched) return false;
  // GT911 坐标映射：480x480 -> 0..SCREEN
  x = map(ts->points[0].x, 480, 0, 0, SCREEN_WIDTH - 1);
  y = map(ts->points[0].y, 480, 0, 0, SCREEN_HEIGHT - 1);
  return true;
}

bool Touch::released() { return true; }