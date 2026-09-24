#pragma once
#include <stdint.h>

/* ══ 用户设置的持久化（NVS / Preferences）══════════════════════════════════
 * 以前设置页改完一重启就回默认值（息屏超时、亮度、自动校时、排版视口），
 * 每次开机都要重设一遍。这里把它们存进 NVS（命名空间 "gtset"）。
 *
 * 用法：
 *   · App::init() 末尾调一次 `loadAll()` —— 读出来并**应用到各模块**
 *     （ScreenSaver / Display / NtpTime / BrowserScreen），没存过的项保持代码默认值。
 *   · 设置页的回调里调 `saveXxx()` —— 只更新内存里的值并标脏。
 *
 * ⚠️ 为什么写入要延迟：滑块拖动会连续触发几十次 VALUE_CHANGED，
 *    每次都写 NVS = 几十次 flash 写入（慢 + 磨损）。所以标脏后由一次性
 *    lv_timer 在 800ms 后统一落盘 —— 松手才真正写。
 * ══════════════════════════════════════════════════════════════════════════ */

namespace SettingsStore {

/* 开机调用一次：读 NVS 并应用到各模块。重复调用无副作用（只是重读一遍）。 */
void loadAll();

/* ── 读（设置页用来填初值）── */
unsigned long idleMs();     // 息屏超时，0 = 常亮
int brightness();           // 背光 5~100
bool autoSync();            // 自动校时
int viewport();             // 浏览器排版视口，0 = 自动

/* ── 写（设置页回调里调；落盘延迟 800ms 合并）── */
void saveIdle(unsigned long ms);
void saveBrightness(int pct);
void saveAutoSync(bool on);
void saveViewport(int w);

/* 诊断：串口打印当前记住的值 */
void dump();

}  // namespace SettingsStore
