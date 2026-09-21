#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include <Arduino_GFX_Library.h>

// 显示驱动封装：ST7701S RGB 并行屏
// 提供 gfx 单例供 app 层使用，屏蔽底层总线细节
class Display {
 public:
  // 初始化显示硬件，返回是否成功
  static bool begin();

  // 获取 GFX 绘图对象单例
  static Arduino_GFX* getGfx();

  // 开关背光（全亮 / 全灭）
  static void setBacklight(bool on);

  // 设置背光亮度 0~100（PWM 调光，息屏 DIM 态用）
  static void setBacklightLevel(uint8_t percent);

 private:
  // 初始化背光 PWM（LEDC），幂等
  static void initBacklightPWM();

  static Arduino_ESP32RGBPanel* bus;
  static Arduino_ST7701_RGBPanel* panel;
  static Arduino_GFX* gfx;
  static bool initialized;
};

#endif