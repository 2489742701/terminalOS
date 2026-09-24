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

/* ── 两点校准端点 ──────────────────────────────────────────────────────
 * 旧代码是写死的 map(points.x, 480, 0, 0, 479)：
 *   ⚠️ 库里 setRotation(ROTATION_NORMAL) 已经做过一次 x = width - x，
 *      这里又 map(480, 0, ...) 翻了一次 —— 两次对消，等于裸值 ×479/480。
 *   ⚠️ 更糟的是端点写死 480：若 GT911 配置里的 X/Y_OUTPUT_MAX 不是 480，
 *      裸值就根本不落在 0..480，于是整屏偏移/缩放都不对。
 * 现在改成可校准的两点线性映射。默认 0..480 是为了**逐字节复刻旧行为**
 * （= 裸值 ×479/480），不是实测值；真值用 `traw` 量四角后 `tcal` 填。
 * ⚠️ x1 != x0 / y1 != y0：map() 除零返回 INT32_MIN，坐标直接飞出屏幕。 */
static int s_rawX0 = 0, s_rawX1 = 480;
static int s_rawY0 = 0, s_rawY1 = 480;

bool Touch::rawXY(int& rx, int& ry) {
  if (!ts || !ts->isTouched) return false;
  /* 库里 ROTATION_NORMAL 已做 width-x / height-y，先反回裸值 */
  rx = (int)SCREEN_WIDTH  - (int)ts->points[0].x;
  ry = (int)SCREEN_HEIGHT - (int)ts->points[0].y;
  return true;
}

static bool s_swapXY = false;

bool Touch::touched(int& x, int& y) {
  if (!ts) return false;
  ts->read();
  int rx, ry;
  if (!rawXY(rx, ry)) return false;
  int sx = (s_rawX1 != s_rawX0)
               ? map(rx, s_rawX0, s_rawX1, 0, SCREEN_WIDTH - 1) : 0;
  int sy = (s_rawY1 != s_rawY0)
               ? map(ry, s_rawY0, s_rawY1, 0, SCREEN_HEIGHT - 1) : 0;
  if (s_swapXY) { x = sy; y = sx; }
  else          { x = sx; y = sy; }
  return true;
}

void Touch::setSwap(bool on) {
  s_swapXY = on;
  Serial.printf("[Touch] swapXY = %d\n", (int)on);
}

void Touch::flipX() {
  int t0 = s_rawX0;
  s_rawX0 = s_rawX1;
  s_rawX1 = t0;
  Serial.printf("[Touch] flipX -> X %d..%d\n", s_rawX0, s_rawX1);
}

void Touch::flipY() {
  int t0 = s_rawY0;
  s_rawY0 = s_rawY1;
  s_rawY1 = t0;
  Serial.printf("[Touch] flipY -> Y %d..%d\n", s_rawY0, s_rawY1);
}

void Touch::swapXY() {
  s_swapXY = !s_swapXY;
  Serial.printf("[Touch] swapXY -> %d\n", (int)s_swapXY);
}

void Touch::resetCal() {
  s_rawX0 = 0;   s_rawX1 = 480;
  s_rawY0 = 0;   s_rawY1 = 480;
  s_swapXY = false;
  Serial.println("[Touch] cal reset to default 0..480 / 0..480");
}

void Touch::getCal(int& x0, int& x1, int& y0, int& y1, bool& swap) {
  x0 = s_rawX0; x1 = s_rawX1;
  y0 = s_rawY0; y1 = s_rawY1;
  swap = s_swapXY;
}

void Touch::setCal(int x0, int x1, int y0, int y1) {
  s_rawX0 = x0; s_rawX1 = x1;
  s_rawY0 = y0; s_rawY1 = y1;
  Serial.printf("[Touch] cal: rawX %d..%d -> 0..%d, rawY %d..%d -> 0..%d\n",
                x0, x1, SCREEN_WIDTH - 1, y0, y1, SCREEN_HEIGHT - 1);
}

void Touch::dump(uint32_t ms) {
  if (!initialized && !begin()) return;
  Serial.printf("[Touch] dump %u ms —— 请依次点：左上 / 右上 / 右下 / 左下\n",
                (unsigned)ms);
  uint32_t t0 = millis();
  int lastRx = -1, lastRy = -1;
  while (millis() - t0 < ms) {
    ts->read();
    int rx, ry;
    if (rawXY(rx, ry) && (rx != lastRx || ry != lastRy)) {
      int sx = (s_rawX1 != s_rawX0)
                   ? map(rx, s_rawX0, s_rawX1, 0, SCREEN_WIDTH - 1) : 0;
      int sy = (s_rawY1 != s_rawY0)
                   ? map(ry, s_rawY0, s_rawY1, 0, SCREEN_HEIGHT - 1) : 0;
      int dx = s_swapXY ? sy : sx, dy = s_swapXY ? sx : sy;
      Serial.printf("[Touch] raw=%5d,%5d -> screen=%4d,%4d%s\n",
                    rx, ry, dx, dy, s_swapXY ? " (swap)" : "");
      lastRx = rx; lastRy = ry;
    }
    delay(30);
  }
  Serial.println("[Touch] dump done");
}

void Touch::probe() {
  /* GT911 配置寄存器：0x8047=版本号, 0x8048/49=X_OUTPUT_MAX, 0x804A/4B=Y_OUTPUT_MAX。
     库里 readByteData 是 private，只能自己走一遍 Wire。addr 用库默认的 0x5D。 */
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  auto rd = [](uint16_t reg) -> int {
    Wire.beginTransmission((uint8_t)0x5D);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)0x5D, (uint8_t)1);
    return (int)Wire.read();
  };
  int ver = rd(0x8047);
  int xmax = rd(0x8048) + (rd(0x8049) << 8);
  int ymax = rd(0x804A) + (rd(0x804B) << 8);
  Serial.printf("[Touch] GT911 cfg: ver=0x%02X X_OUTPUT_MAX=%d Y_OUTPUT_MAX=%d"
                " (屏幕 %dx%d；两者不一致 => 裸值不落在 0..%d，就是偏移根因)\n",
                ver, xmax, ymax, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH);
}

bool Touch::released() { return true; }