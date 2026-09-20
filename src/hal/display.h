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
  static Arduino_GFXClass* getGfx();

  // 开关背光
  static void setBacklight(bool on);

 private:
  static Arduino_ESP32RGBPanel* bus;
  static Arduino_ST7701_RGBPanel* panel;
  static Arduino_GFXClass* gfx;
  static bool initialized;
};

#endif