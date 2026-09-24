#pragma once
#include <lvgl.h>

/* ══ 设置页的列表渲染器 ════════════════════════════════════════════════════
 * 为什么要有它：原 settings.cpp 每一项都是手摆绝对坐标（y=86/120/154/…），
 * 每项 ~15 行样式样板。那种结构上加二级菜单 = 每加一项继续堆 15 行，
 * 放不下也改不动。这里把"内容"变成数据：加一项 = 加一行数组元素。
 *
 * 二级菜单 = **同一个屏 + 数据源栈**，不是每级建一个 Activity：
 *   · nav 表不会被撑爆
 *   · 不会每页多一棵常驻对象树（这个项目被"10 屏常驻导致 DRAM 不够"坑过）
 *
 * ⚠️ 三条铁律（都是踩过的）：
 *   1. 切页必须**延迟一拍**：在行点击事件里 lv_obj_clean 会删掉正在分发事件的
 *      那一行 —— 直接崩。这里统一走一次性 lv_timer。
 *   2. 滚动容器必须 LV_SCROLLBAR_MODE_OFF：默认 AUTO 会在暗色 UI 上画一条
 *      浅色滚动条（就是浏览器那条"白线"）。
 *   3. 定时器持有容器指针 → 屏销毁时要注销定时器（LV_EVENT_DELETE）。
 * ══════════════════════════════════════════════════════════════════════════ */

enum class SetType : uint8_t {
  Nav,       // 进子菜单（右侧 › + 可选值）。cb 非空时改为执行 cb（用于跳别的屏）
  Toggle,    // 开关
  Slider,    // 滑块（行内右侧）
  ReadOnly,  // 只读：右侧灰字，值由 valueFn 提供
  Action,    // 点整行执行
};

struct SettingsItem {
  const char* title;
  SetType type;
  const char* pageId;            // Nav：目标页 id
  const char* (*valueFn)();      // ReadOnly / Nav：右侧文字（返回静态缓冲）
  lv_event_cb_t cb;              // Toggle / Slider / Action / Nav(覆盖跳转)
  int vmin, vmax, vinit;         // Slider: 最小/最大/初值
  bool checked;                  // Toggle: 初值
};

/* 用工厂函数而不是聚合初始化：本工程是 gnu++11，
   聚合体带默认成员初始化在 C++14 才合法，别踩。 */
inline SettingsItem siEnd() {
  SettingsItem it = {};
  it.title = nullptr;
  return it;
}
inline SettingsItem siNav(const char* title, const char* pageId,
                          const char* (*valueFn)() = nullptr,
                          lv_event_cb_t cb = nullptr) {
  SettingsItem it = {};
  it.title = title; it.type = SetType::Nav;
  it.pageId = pageId; it.valueFn = valueFn; it.cb = cb;
  return it;
}
inline SettingsItem siToggle(const char* title, bool on, lv_event_cb_t cb) {
  SettingsItem it = {};
  it.title = title; it.type = SetType::Toggle;
  it.cb = cb; it.checked = on;
  return it;
}
/* 带 valueFn 时，滑块左侧会显示当前值（滑块宽度相应缩窄）。
   不传的话拖滑块看不到数字，只有一个条 —— 用户没法知道现在是多少。 */
inline SettingsItem siSlider(const char* title, int vmin, int vmax, int vinit,
                             lv_event_cb_t cb, const char* (*valueFn)() = nullptr) {
  SettingsItem it = {};
  it.title = title; it.type = SetType::Slider;
  it.cb = cb; it.vmin = vmin; it.vmax = vmax; it.vinit = vinit;
  it.valueFn = valueFn;
  return it;
}
inline SettingsItem siReadOnly(const char* title, const char* (*valueFn)()) {
  SettingsItem it = {};
  it.title = title; it.type = SetType::ReadOnly; it.valueFn = valueFn;
  return it;
}
/* 带 valueFn 的版本 = "点整行切换，右侧显示当前值"（如自动息屏 30秒/1分钟/…）。
   切完在 cb 里调 settings_menu_refresh_values() 立刻刷新右侧文字。 */
inline SettingsItem siAction(const char* title, lv_event_cb_t cb,
                             const char* (*valueFn)() = nullptr) {
  SettingsItem it = {};
  it.title = title; it.type = SetType::Action; it.cb = cb; it.valueFn = valueFn;
  return it;
}

struct SettingsPage {
  const char* id;
  const char* title;         // 顶栏显示的名字
  const SettingsItem* items; // 以 siEnd() 结尾
};

/* 建好列表容器并显示 root 页。bar 用于切页时改顶栏标题（可传 nullptr）。 */
void settings_menu_begin(lv_obj_t* scr, lv_obj_t* bar,
                         const SettingsPage* pages, int pageCount,
                         const char* rootId);

/* 只刷新各行的右侧文字（valueFn）—— 不重建、不重置滚动位置。
   定时刷"时间源 / 缓存大小"这类会变的值就调它。 */
void settings_menu_refresh_values();

/* 返回：有上级就回上级（顶栏标题同步改回去），已在 root 就回桌面。
   直接挂给 StatusBar_createEx 的 backCb 即可 —— 所以签名必须是 lv_event_cb_t，
   e 恒为 nullptr（StatusBar 在延迟一拍的定时器里调它），别去读 e。 */
void settings_menu_back(lv_event_t* e);

/* 当前所在层级：0 = 根页。外部（滑动返回）据此决定回桌面还是上一级。 */
int settings_menu_depth();

/* 串口诊断：直接跳到某个页（等价于点那行）。id 不存在会打日志并返回。 */
void settings_menu_goto(const char* id);
