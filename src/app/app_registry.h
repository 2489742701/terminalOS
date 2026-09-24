#pragma once
#include <lvgl.h>
#include "icons.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * 应用注册表
 *
 * 一处登记、三处消费：
 *   · launcher       —— 只画 group==Desktop 且 visible 的项
 *   · games_screen   —— 只画 group==Game 的项（游戏栏目）
 *   · desktop_screen —— 列出 group==Desktop 的项，可逐个开关 visible
 *
 * 可见性落 NVS（Preferences），改完立刻生效：下一帧 launcher 重建即可。
 * ⚠️ 桌面项里至少要留「设置」可见，否则用户再也进不来改回去 ——
 *    desktop_screen 里对「设置」这一行做了硬保护（开关置灰）。
 * ═══════════════════════════════════════════════════════════════════════════ */

enum class AppGroup : uint8_t {
  Desktop,  // 出现在桌面 / 可被隐藏
  Game,     // 只出现在游戏栏目里，不占桌面
};

struct AppEntry {
  const char* id;      // NVS key，也是这一项的唯一身份
  const char* label;   // 显示名（中文）
  Icon icon;
  lv_obj_t** target;   // 指向 nav_xxx 全局指针
  AppGroup group;
  bool defVisible;     // 首次开机（NVS 无记录）时的默认值
};

/* 开机调一次：用可写模式把桌面项的默认值落盘。
   不做这步的话，只读 begin() 会因为命名空间还没建而每次刷
   "nvs_open failed: NOT_FOUND"（打开桌面设置屏一刷就是 8 条）。 */
void appreg_init();

// 注册表条目数
int appreg_count();

// 按下标取条目；越界返回 nullptr
const AppEntry* appreg_at(int i);

// 桌面组里当前可见的条目数（launcher 实际要画的磁贴数）
int appreg_desktop_visible_count();

// 取第 n 个「可见的桌面项」的原下标；没有则返回 -1
int appreg_desktop_visible_index(int n);

// 可见性读写（NVS 持久化）。对 group==Game 的项调用无意义，恒返回 true。
bool appreg_visible(int i);
void appreg_set_visible(int i, bool v);

// 恢复出厂：全部桌面项回到默认可见
void appreg_reset_visible();

// 按 id 找条目下标，找不到返回 -1
int appreg_find(const char* id);
