#include "nav.h"

#include <Arduino.h>

#include "clock.h"
#include "settings.h"
#include "wifi_screen.h"
#include "game_screen.h"
#include "browser_screen.h"
#include "draw_screen.h"
#include "memory_screen.h"
#include "sysinfo_screen.h"
#include "weather_screen.h"

#include <esp_heap_caps.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Activity 注册表
 *
 * 背景：ESP32 无 MMU / 无换页，启动时一次性创建的每个屏幕都是一棵常驻的
 *       LVGL 对象树。10 屏常驻时内部 DRAM 只剩 ~93KB，导致浏览器后台任务
 *       连 48KB 连续栈都申请不到（Failed to create fetch task）。
 *
 * 策略：Launcher 常驻（轻量），其余应用按需创建、退出/抢占时销毁。
 *       canRelease 为该 Activity 的释放守卫（返回 false 时跳过释放），
 *       用于保护仍在被 tick 依赖的应用。
 * ═══════════════════════════════════════════════════════════════════════════ */

namespace {

typedef lv_obj_t* (*Creator)();
typedef bool (*CanRelease)();
typedef void (*OnDestroy)();

struct ActivityEntry {
  const char* name;
  lv_obj_t** screen;
  Creator create;
  CanRelease canRelease;  // nullptr = 总是可释放
  OnDestroy  onDestroy;   // nullptr = 只需 lv_obj_del（对象树自带内存由 LVGL 回收）
};

/* WiFi 屏例外：App::loop() 里 WifiScreen_isConnecting() 会独立触发 tick，
   连着的时候销毁它会导致 tick 操作悬空指针，所以连接期间不释放。 */
bool wifiCanRelease() { return !WifiScreen_isConnecting(); }

ActivityEntry s_table[] = {
    {"clock",    &nav_clock,    ClockScreen_create,    nullptr,         nullptr},
    {"settings", &nav_settings, SettingsScreen_create, nullptr,         nullptr},
    {"wifi",     &nav_wifi,     WifiScreen_create,     wifiCanRelease,  nullptr},
    {"game",     &nav_game,     GameScreen_create,     nullptr,         nullptr},
    /* 浏览器必须挂 onDestroy：布局树是引擎 malloc 出来的，不在 LVGL 对象树里，
       lv_obj_del() 管不到它。不回调就会每次退出泄漏几十 KB（实测退出后 DRAM 零回收）。 */
    {"browser",  &nav_browser,  BrowserScreen_create,  nullptr,         BrowserScreen_close},
    {"draw",     &nav_draw,     DrawScreen_create,     nullptr,         nullptr},
    {"memory",   &nav_memory,   MemoryScreen_create,   nullptr,         nullptr},
    {"sysinfo",  &nav_sysinfo,  SysInfoScreen_create,  nullptr,         nullptr},
    {"weather",  &nav_weather,  WeatherScreen_create,  nullptr,         nullptr},
};

const int s_count = (int)(sizeof(s_table) / sizeof(s_table[0]));

ActivityEntry* find(lv_obj_t** target) {
  for (int i = 0; i < s_count; i++) {
    if (s_table[i].screen == target) return &s_table[i];
  }
  return nullptr;
}

}  // namespace

lv_obj_t* nav_open(lv_obj_t** target) {
  if (!target) return nullptr;
  if (*target) return *target;  // 已加载

  ActivityEntry* e = find(target);
  if (!e || !e->create) return nullptr;

  uint32_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  lv_obj_t* scr = e->create();
  *target = scr;
  Serial.printf("[Nav] open %s: %s, DRAM %u -> %u\n", e->name,
                scr ? "ok" : "FAILED", (unsigned)before,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  return scr;
}

void nav_release_all_except(lv_obj_t* keep1, lv_obj_t* keep2) {
  for (int i = 0; i < s_count; i++) {
    ActivityEntry& e = s_table[i];
    if (!*(e.screen)) continue;              // 未加载
    if (*(e.screen) == keep1 || *(e.screen) == keep2) continue;
    if (e.canRelease && !e.canRelease()) {
      Serial.printf("[Nav] keep %s (release guard)\n", e.name);
      continue;
    }

    uint32_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (e.onDestroy) e.onDestroy();          // 先放应用自己管着的堆内存
    lv_obj_del(*(e.screen));                 // 再删整棵对象树
    *(e.screen) = nullptr;                   // 断全局指针，避免悬空引用
    Serial.printf("[Nav] release %s: DRAM %u -> %u\n", e.name,
                  (unsigned)before,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  }
}

void nav_back_home() {
  /* ⚠️ 顺序必须是「先切屏，再销毁」。
     lv_obj_del() 掉当前活动屏幕 = LVGL 还在用这棵对象树就把它释放了，
     实测直接 Guru Meditation (LoadProhibited)。
     也不能用带动画的切换：动画期间旧屏仍参与渲染，删了照样崩。
     所以这里用无动画 lv_scr_load 立即切换，换取能马上安全销毁。 */
  if (nav_launcher) lv_scr_load(nav_launcher);
  nav_release_all_except(nav_launcher);
}
