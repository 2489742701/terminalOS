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

 private:
  static TAMC_GT911* ts;
  static bool initialized;
};

#endif