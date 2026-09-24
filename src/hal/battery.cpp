#include "battery.h"
#include <Arduino.h>
#include <Wire.h>

bool   Battery::s_probed  = false;
bool   Battery::s_present = false;
uint8_t Battery::s_addr   = 0;

/* 已知电源管理 IC 的 7 位 I2C 地址（只扫这几个，避免扫全总线引发挂死）。
   0x75 IP5306 / CK9016 / LP7810 —— 国产 SOP-8 充电宝 IC，最常见
   0x6B BQ25890 / IP2326 系列充电管理
   0x34 AXP192（M5Stack 老款）
   0x35 AXP2101
   0x75 与 GT911(0x5D/0x14) 不冲突。                                   */
static const uint8_t kPmicAddrs[] = {0x75, 0x6B, 0x34, 0x35};

static uint8_t readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0)   /* false = 不发 STOP，直接 repeated start */
    return 0xFF;
  if (Wire.requestFrom((int)addr, 1) != 1)
    return 0xFF;
  return (uint8_t)Wire.read();
}

bool Battery::probe() {
  if (s_probed)
    return s_present;
  s_probed = true;
  s_present = false;

  for (uint8_t a : kPmicAddrs) {
    Wire.beginTransmission(a);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      s_addr = a;
      s_present = true;
      Serial.printf("[Battery] PMIC found at 0x%02X\n", a);
      break;
    }
  }
  if (!s_present)
    Serial.println("[Battery] no PMIC on I2C (0x75/0x6B/0x34/0x35) -> USB powered");
  return s_present;
}

bool Battery::available() { return probe() && s_present; }

int Battery::percent() {
  if (!probe() || !s_present)
    return -1;

  /* IP5306 系（0x75）：0x78 是电量和充放电状态。
     社区逆向出的映射（M5Stack / TTGO 等大量项目在用）：
       bit7  = 1 充电中
       bit[7:4] 的高 4 位按 00/80/C0/E0 对应 100/75/50/25 %
     ⚠️ 这份映射是社区逆向结果、非原厂公开文档，需要实机对照校准。
       用串口 `bat` 打印原始寄存器值，和万用表量到的电量比对后再定。 */
  if (s_addr == 0x75) {
    uint8_t v = readReg(0x75, 0x78);
    if (v == 0xFF)
      return -1;
    switch (v & 0xF0) {
      case 0x00: return 100;
      case 0x80: return 75;
      case 0xC0: return 50;
      case 0xE0: return 25;
      default:   return 0;
    }
  }

  /* 其他 PMIC：暂时只报告"存在"，不给百分比，避免拿错的寄存器乱显示。 */
  return -1;
}

void Battery::dump() {
  Serial.println("=== Battery / PMIC probe ===");
  if (!probe() || !s_present) {
    Serial.println("  no PMIC detected -> battery percent unavailable (USB)");
    Serial.println("  (IO35/36/37 在厂家 IO 表中无功能标注；若板上有电源 IC，");
    Serial.println("   请把芯片丝印发我，或确认它是否挂在 I2C 上)");
    return;
  }
  Serial.printf("  PMIC addr = 0x%02X\n", s_addr);
  for (uint8_t r = 0x70; r <= 0x7F; r++) {
    uint8_t v = readReg(s_addr, r);
    Serial.printf("  reg 0x%02X = 0x%02X\n", r, v);
  }
  int p = percent();
  Serial.printf("  percent = %d\n", p);
}
