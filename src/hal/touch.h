#ifndef HAL_TOUCH_H
#define HAL_TOUCH_H

#include <TAMC_GT911.h>

// 触摸驱动封装：GT911 电容触摸
// 提供触摸事件读取，供 LVGL input driver 回调
class Touch {
 public:
  // 初始化触摸硬件
  static bool begin();

  // 是否有触摸信号
  static bool hasSignal();

  // 是否被按下，并更新坐标
  static bool touched(int& x, int& y);

  // 是否释放
  static bool released();

  /* 两点校准：把面板裸坐标 [x0,x1] / [y0,y1] 线性映射到屏幕 0..479。
     用串口 `traw` 量四角裸值，再用 `tcal x0 x1 y0 y1` 填。 */
  static void setCal(int x0, int x1, int y0, int y1);

  /* X/Y 交换开关（面板贴反时裸 X 其实是屏幕 Y）。`tswap` 现场切，
     配合 tcal 的端点顺序可以把翻转/交换/偏移三种情况全试一遍。 */
  static void setSwap(bool on);

  /* 当前按下的**面板裸坐标**（未做屏幕映射）。ts->read() 之后调用。 */
  static bool rawXY(int& rx, int& ry);

  /* 连续打印裸坐标 + 映射后屏幕坐标，用于量四角。ms=持续毫秒。 */
  static void dump(uint32_t ms);

  /* 读 GT911 配置寄存器里的 X/Y_OUTPUT_MAX（库里 readByteData 是 private，
     只能自己走 Wire）。若它不是 480，说明控制器输出范围与屏幕不符 ——
     那就是"触摸偏移"的根因。 */
  static void probe();

 private:
  static TAMC_GT911* ts;
  static bool initialized;
};

#endif