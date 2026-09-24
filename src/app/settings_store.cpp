#include "settings_store.h"
#include "screensaver.h"
#include "browser_screen.h"
#include "../hal/display.h"
#include "../hal/ntp_time.h"
#include <Preferences.h>
#include <lvgl.h>
#include <Arduino.h>

namespace {

const char* kNs = "gtset";          // NVS 命名空间
constexpr uint32_t kFlushDelayMs = 800;   // 拖动合并窗口

/* 默认值与各自模块的编译期默认保持一致 —— 没存过就用这些 */
unsigned long s_idleMs     = 300000;   // 与 screensaver.cpp 的 ACTIVE_TIMEOUT_MS 初值一致
int           s_brightness = 100;
bool          s_autoSync   = true;     // NtpTime 默认开
int           s_viewport   = 0;        // 0 = 自动
bool          s_anim       = true;     // 动画默认**开**

lv_timer_t* s_flushTimer = nullptr;
bool s_hasStored = false;   // NVS 里有没有存过（用于 dump 提示）

void writeAll() {
  Preferences p;
  if (!p.begin(kNs, false)) {          // false = 读写
    Serial.println("[SettingsStore] NVS begin(rw) failed");
    return;
  }
  p.putULong("idleMs", s_idleMs);
  p.putInt("bright", s_brightness);
  p.putBool("autosync", s_autoSync);
  p.putInt("viewport", s_viewport);
  p.putBool("anim", s_anim);
  p.end();
  s_hasStored = true;
  Serial.printf("[SettingsStore] saved idle=%lums bright=%d autosync=%d viewport=%d anim=%d\n",
                s_idleMs, s_brightness, (int)s_autoSync, s_viewport, (int)s_anim);
}

/* 延迟落盘：拖动滑块期间只更新内存，松手 800ms 后统一写一次 */
void flush_cb(lv_timer_t* t) {
  (void)t;
  s_flushTimer = nullptr;
  writeAll();
}

void markDirty() {
  if (s_flushTimer) return;            // 已排队；最后一次的值会在 flush 时写走
  s_flushTimer = lv_timer_create(flush_cb, kFlushDelayMs, nullptr);
  if (s_flushTimer) lv_timer_set_repeat_count(s_flushTimer, 1);
}

}  // namespace

/* 全局动画开关。⚠️ **必须在匿名 namespace 外面**：写在里面就是内部链接，
   别的 TU 里 extern 声明能编译过，链接时却是 undefined reference
   （踩了一次：browser_screen.cpp 报 undefined reference to `g_uiAnim'）。
   定义在这是为了让每个屏只看一个 bool 就决定"要不要动"，不用各自去读 NVS。 */
bool g_uiAnim = true;

namespace SettingsStore {

void loadAll() {
  Preferences p;
  /* ⚠️ 必须读写模式（false），不能用只读（true）：
     只读模式打开**还不存在的** namespace 会 nvs_open failed: NOT_FOUND
     —— 也就是第一次开机必然失败，持久化形同虚设。读写模式不存在则创建。 */
  if (!p.begin(kNs, false)) {
    Serial.println("[SettingsStore] NVS begin failed, keep defaults");
    return;
  }
  s_hasStored = true;
  s_idleMs     = p.getULong("idleMs", s_idleMs);
  s_brightness = p.getInt("bright", s_brightness);
  s_autoSync   = p.getBool("autosync", s_autoSync);
  s_viewport   = p.getInt("viewport", s_viewport);
  s_anim       = p.getBool("anim", s_anim);
  p.end();

  /* 应用到各模块 —— 只 load 不 apply 的话，只有进设置页才生效，那就没意义了 */
  ScreenSaver::setIdleTimeout(s_idleMs);
  Display::setBacklightLevel((uint8_t)s_brightness);
  NtpTime::setAutoSync(s_autoSync);
  BrowserScreen_setViewport(s_viewport);
  g_uiAnim = s_anim;

  Serial.printf("[SettingsStore] loaded idle=%lums bright=%d autosync=%d viewport=%d anim=%d\n",
                s_idleMs, s_brightness, (int)s_autoSync, s_viewport, (int)s_anim);
}

unsigned long idleMs() { return s_idleMs; }
int brightness() { return s_brightness; }
bool autoSync() { return s_autoSync; }
int viewport() { return s_viewport; }
bool animEnabled() { return s_anim; }

void saveIdle(unsigned long ms) { s_idleMs = ms; markDirty(); }
void saveBrightness(int pct) { s_brightness = pct; markDirty(); }
void saveAutoSync(bool on) { s_autoSync = on; markDirty(); }
void saveViewport(int w) { s_viewport = w; markDirty(); }
/* ⚠️ 只改内存里的 s_anim 并落盘，**不改 g_uiAnim** —— 开关重启才生效，
   运行中改会让"已经在飞的动画"和"新动画"状态不一致。 */
void saveAnim(bool on) { s_anim = on; markDirty(); }

void dump() {
  const char* src = s_hasStored ? "NVS" : "(defaults, nothing stored yet)";
  if (s_idleMs == 0)
    Serial.printf("[SettingsStore] %s idle=常亮 bright=%d autosync=%d viewport=%d anim=%d\n",
                  src, s_brightness, (int)s_autoSync, s_viewport, (int)s_anim);
  else
    Serial.printf("[SettingsStore] %s idle=%lums bright=%d autosync=%d viewport=%d anim=%d\n",
                  src, s_idleMs, s_brightness, (int)s_autoSync, s_viewport, (int)s_anim);
}

}  // namespace SettingsStore
