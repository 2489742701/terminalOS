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

  // 抑制息屏：加载网页等长耗时操作期间调用，避免中途被锁屏抢走屏幕
  // （切走后浏览器 tick 停摆，渲染永远执行不到，表现为"卡住加载不出来"）
  static void setSuppressed(bool on);

  // 任意方向滑动后调用，回到 ACTIVE
  static void unlock();

  // 立即熄屏（关背光），用于应用内息屏按钮。
  // 会先记下当前屏，唤醒后回锁屏页，滑动解锁再回到原来那屏。
  static void sleepNow();

  /* 自动息屏：无操作多久进 DIM。**0 = 永不息屏**。
     原先是编译期常量（300000），设置页够不着 —— 改成运行时变量才有「自动息屏」这一项。 */
  static void setIdleTimeout(unsigned long ms);
  static unsigned long idleTimeout();

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
  static bool suppressed;  // true = 加载等长耗时操作期间不息屏
  static bool offArmed;         // OFF 态：已检测到"松手"，之后的按下才允许唤醒
  static unsigned long offEnteredMs;  // 进入 OFF 的时刻，用于唤醒去抖

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
