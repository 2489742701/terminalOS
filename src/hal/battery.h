#ifndef HAL_BATTERY_H
#define HAL_BATTERY_H

#include <stdint.h>

/**
 * 电池 / 电源管理 IC 探测。
 *
 * 背景（2026-09-23 更正）：
 *   之前代码里写死"本板没有电量检测硬件"——这个结论**证据不足**。
 *   厂家的 IO 分配表里 IO35/36/37 三个脚没有任何功能标注，板子上也确实焊了
 *   一颗八角芯片；它有可能是充电/电量计 IC（IP5306 之类，SOP-8，走 I2C），
 *   也可能只是 NS4168 音频功放（厂家资料里明确列了这颗，同为八角封装）。
 *   所以正确做法是让板子自己回答：扫 I2C 看有没有已知的电源 IC。
 *
 * 设计：
 *   - 与 GT911 共用同一条 I2C（Wire 已在 Touch::begin() 里 begin(19,45)）。
 *   - 只探测白名单地址，且只在首次查询时扫一次，之后用缓存结果。
 *   - 探测不到 -> percent() 返回 -1，UI 维持"USB 供电"显示，行为与之前一致。
 */
class Battery {
 public:
  // 电量百分比 0~100；<0 = 未检测到电源 IC 或读数不可用。
  static int percent();

  // 是否已经检测到电源 IC
  static bool available();

  // 串口诊断：打印探测结果与关键寄存器原始值（用于校准电量映射）
  static void dump();

 private:
  static bool probe();
  static bool s_probed;
  static bool s_present;
  static uint8_t s_addr;
};

#endif  // HAL_BATTERY_H
