#pragma once
#include <lvgl.h>

// 统一顶栏（高 28px，贴 parent 顶部，透明底）—— 所有 APP 共用
//   左：桌面(root)=电池  |  APP=返回三角形 + 当前 APP 名称（整块可点击 → 回桌面）
//   中：时间 HH:MM AM/PM
//   右：蓝牙 → WiFi → 供电/电量文本（"78%  60fps" / "USB 60fps"）
//
// 电池只出现在桌面顶栏左边；进入 APP 后隐藏，位置让给返回按钮。
// 电量信息不丢 —— 右侧文本里本来就有百分比。
// title 传 nullptr 表示根节点（Launcher）。
// 返回动作统一 nav_back_home()；为避免在点击事件里销毁当前屏，
// 内部走一次性 lv_timer 延迟到下一个 tick 执行。
//
// 自包含：内部用 lv_timer 自动计数/刷新，调用方只需 StatusBar_create()，
// 无需在主循环里手动 tick。开销：全局只 1 个 1s 定时器。
lv_obj_t* StatusBar_create(lv_obj_t* parent, const char* title);

/* 自定义返回动作版。二级菜单要"回上一级"而不是"回桌面"，就得用它。
   ⚠️ backCb 在**延迟一拍的 lv_timer** 里被调用（不在事件回调里），
      所以里面 nav_go / lv_obj_del 都安全；参数 e 恒为 nullptr，别去读它。 */
lv_obj_t* StatusBar_createEx(lv_obj_t* parent, const char* title, lv_event_cb_t backCb);

/* 切二级菜单时改顶栏标题。返回 false = 没改成（bar 为 root 顶栏或结构不符）。 */
bool StatusBar_setTitle(lv_obj_t* bar, const char* title);

// 电量百分比 0~100；返回 <0 表示本板无电量检测硬件（按 USB 供电处理）。
// 若以后给电池分压接了 ADC，改本函数实现即可，UI 会自动显示百分比。
int StatusBar_batteryPercent();

// 蓝牙是否已启用/已连接。目前尚未接入 BLE，恒 false（图标显示为半透明灰）。
bool StatusBar_bluetoothOn();
