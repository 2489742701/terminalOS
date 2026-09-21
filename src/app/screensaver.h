#ifndef APP_SCREENSAVER_H
#define APP_SCREENSAVER_H

#include <lvgl.h>

// 息屏 / 锁屏状态机：ACTIVE -> DIM(暗显时间) -> OFF(关背光全黑)
// - 长时间无触摸：ACTIVE 超时进入 DIM
// - DIM 态再静置一段时间：进入 OFF（关背光，最省电）
// - OFF 态任意触摸唤醒到 DIM；DIM 态任意方向滑动解锁回到 ACTIVE
class ScreenSaver {
 public:
  // 初始化软时钟并构建锁屏界面；mainScreen 为正常界面，用于解锁后切回
  static void init(lv_obj_t* mainScreen);

  // 每帧调用：处理空闲超时切换与 OFF 态触摸唤醒
  static void tick();

  // 任意触摸按下时调用，重置空闲计时
  static void notifyActivity();

  // 任意方向滑动后调用，回到 ACTIVE
  static void unlock();

  // 立即进入锁屏（DIM），用于应用内息屏按钮
  static void sleepNow();

 private:
  enum State { ACTIVE, DIM, OFF };

  static State state;
  static lv_obj_t* mainScr;
  static lv_obj_t* returnScr;  // 进入 DIM 时的活跃屏，解锁后切回
  static lv_obj_t* dimScr;
  static lv_obj_t* clockLabel;
  static lv_obj_t* dateLabel;
  static lv_obj_t* clockCanvas;
  static unsigned long lastActivityMs;
  static unsigned long lastClockMs;
  static unsigned long lastTapMs;
  static int pressX;
  static int pressY;
  static bool unlocked;

  static void enterActive();
  static void enterDim(bool captureReturnScr);
  static void enterOff();
  static void updateClock();
  static void drawAnalogClock();
  static void buildDimScreen();
  static void gesture_event_cb(lv_event_t* e);
  static void initSoftClock();
};

#endif
