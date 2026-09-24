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

  /* panel 的帧缓冲首址（PSRAM，480*480*2 = 460800 B）。
     LVGL 开 direct_mode 后可以指着这里当 draw_buf，直接在屏的显存上作画，
     从而省掉 "LVGL 缓冲 -> framebuffer" 那趟 PSRAM->PSRAM 搬运（实测 25ms）。
     未初始化 / 非 RGB 屏时返回 nullptr，调用方必须判空后回退。 */
  static uint16_t* getFramebuffer();

  /* 把 CPU 的 DCache 写回物理内存。
     PSRAM 经 cache 访问，而 LCD 的 GDMA 读的是物理内存 —— 不写回的话
     GDMA 扫出去的还是上一帧的旧数据（表现为"画面不动/残影"）。 */
  static void flushCache(uint32_t addr, uint32_t size);

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