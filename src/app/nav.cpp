#include "nav.h"

#include <Arduino.h>

#include "clock.h"
#include "settings.h"
#include "wifi_screen.h"
#include "game_screen.h"
#include "browser_screen.h"
#include "draw_screen.h"
#include "memory_screen.h"
#include "game2048_screen.h"
#include "sysinfo_screen.h"
#include "weather_screen.h"
#include "games_screen.h"
#include "desktop_screen.h"
#include "taskmgr_screen.h"
#include "touchtest_screen.h"

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
  uint32_t   usedBytes;   // 创建时吃掉的内部 DRAM（后台管理页显示用）
  uint32_t   lastUsed;    // 最近一次进前台的 millis()，LRU 回收用（0 = 从没进过）
};

/* WiFi 屏例外：App::loop() 里 WifiScreen_isConnecting() 会独立触发 tick，
   连着的时候销毁它会导致 tick 操作悬空指针，所以连接期间不释放。 */
bool wifiCanRelease() { return !WifiScreen_isConnecting(); }

/* 浏览器同理：后台 fetch 任务在别的任务里直接操作 LVGL 对象，任务没停就
   lv_obj_del 整棵树 = 它手里变悬空指针，再写一次就把对象树写坏（实机崩在
   lv_obj_get_screen）。BrowserScreen_close() 内部已经会等任务退出，这里再
   兜一层：万一还停不下来，宁可这次不释放，也别把树写坏。 */
bool browserCanRelease() { return !BrowserScreen_isBusy(); }

ActivityEntry s_table[] = {
    {"clock",    &nav_clock,    ClockScreen_create,    nullptr,         nullptr},
    {"settings", &nav_settings, SettingsScreen_create, nullptr,         nullptr},
    {"wifi",     &nav_wifi,     WifiScreen_create,     wifiCanRelease,  nullptr},
    {"game",     &nav_game,     GameScreen_create,     nullptr,         nullptr},
    /* 浏览器必须挂 onDestroy：布局树是引擎 malloc 出来的，不在 LVGL 对象树里，
       lv_obj_del() 管不到它。不回调就会每次退出泄漏几十 KB（实测退出后 DRAM 零回收）。 */
    {"browser",  &nav_browser,  BrowserScreen_create,  browserCanRelease, BrowserScreen_close},
    {"draw",     &nav_draw,     DrawScreen_create,     nullptr,         nullptr},
    {"memory",   &nav_memory,   MemoryScreen_create,   nullptr,         nullptr},
    /* 2048：纯回合制（滑动才走一步），没有 tick，不用在 App::loop 里挂东西 */
    {"2048",     &nav_2048,     Game2048Screen_create, nullptr,         nullptr},
    {"sysinfo",  &nav_sysinfo,  SysInfoScreen_create,  nullptr,         nullptr},
    {"weather",  &nav_weather,  WeatherScreen_create,  nullptr,         nullptr},
    /* 游戏栏目本身也是一个 Activity：从它进去的子游戏退出时要回来，
       所以它必须能按需创建（见 nav_games_or_home）。 */
    {"games",    &nav_games,    GamesScreen_create,    nullptr,         nullptr},
    {"desktop",  &nav_desktop,  DesktopScreen_create,  nullptr,         nullptr},
    {"taskmgr",  &nav_taskmgr,   TaskMgrScreen_create,   nullptr,         nullptr,         0},
    /* 触摸测试：诊断用，不进 Launcher，只走串口 `nav touchtest` 和画板底部入口 */
    {"touchtest", &nav_touchtest, TouchTestScreen_create, nullptr,        nullptr},
};

const int s_count = (int)(sizeof(s_table) / sizeof(s_table[0]));

ActivityEntry* find(lv_obj_t** target) {
  for (int i = 0; i < s_count; i++) {
    if (s_table[i].screen == target) return &s_table[i];
  }
  return nullptr;
}

}  // namespace

/* ── 后台 LRU 兜底 ──────────────────────────────────────────────────────────
 * 回桌面不再清后台之后（见 nav_back_home），内存不能无限涨：
 * 超过 MAX_BG 个后台、或者 DRAM 低于 MIN_FREE 时，回收**最久没进前台**的那个。
 * ⚠️ 前台和 keep 那个绝不回收（删当前屏必崩）；canRelease 守卫不通过的也跳过。
 * ⚠️ 浏览器太重，Launcher 进它时仍然走 nav_release_all_except 独占，不走这里。 */
static void trimBackground(lv_obj_t* keep) {
  const int      MAX_BG   = 4;              /* 最多留 4 个后台 */
  const uint32_t MIN_FREE = 48 * 1024;      /* DRAM 低于 48KB 就开始收（浏览器任务栈要 8KB） */

  lv_obj_t* act = lv_scr_act();
  int loaded = 0;
  ActivityEntry* victim = nullptr;
  for (int i = 0; i < s_count; i++) {
    ActivityEntry& e = s_table[i];
    if (!*(e.screen)) continue;                                  // 没在内存里
    if (*(e.screen) == act || *(e.screen) == keep) continue;      // 前台 / 马上要开的
    loaded++;
    if (!victim || e.lastUsed < victim->lastUsed) victim = &e;
  }
  if (!victim) return;
  if (loaded <= MAX_BG &&
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= MIN_FREE) return;
  if (victim->canRelease && !victim->canRelease()) {
    Serial.printf("[Nav] trim skip %s (release guard)\n", victim->name);
    return;
  }

  uint32_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (victim->onDestroy) victim->onDestroy();
  lv_obj_del(*(victim->screen));
  *(victim->screen) = nullptr;
  Serial.printf("[Nav] trim %s (bg=%d): DRAM %u -> %u\n", victim->name, loaded,
                (unsigned)before,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

lv_obj_t* nav_open(lv_obj_t** target) {
  if (!target) return nullptr;
  if (*target) {                       // 已加载：刷新 LRU 时间就够了
    ActivityEntry* ex = find(target);
    if (ex) ex->lastUsed = millis();
    return *target;
  }

  ActivityEntry* e = find(target);
  if (!e || !e->create) return nullptr;

  /* 开新的之前先按 LRU 腾一腾，别等到创建失败才想起来收 */
  trimBackground(*target);

  uint32_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  lv_obj_t* scr = e->create();
  *target = scr;
  uint32_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  /* 后台管理页要显示"这个应用占了多大"。这里记的是创建瞬间的 DRAM 差值，
     之后应用自己还会再申请（浏览器布局树等），所以是个下界，不是精确值。 */
  e->usedBytes = (after < before) ? (before - after) : 0;
  Serial.printf("[Nav] open %s: %s, DRAM %u -> %u\n", e->name,
                scr ? "ok" : "FAILED", (unsigned)before, (unsigned)after);
  if (scr) e->lastUsed = millis();
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

lv_obj_t* nav_games_or_home() {
  if (!nav_games) nav_open(&nav_games);
  return nav_games ? nav_games : nav_launcher;
}

void nav_back_home_anim() {
  if (!nav_launcher) { nav_back_home(); return; }
  if (!g_uiAnim) { nav_back_home(); return; }   /* 关动画：一步到位 */
  lv_scr_load_anim(nav_launcher, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 240, 0, false);
  nav_lock_until = lv_tick_get() + 240 + 300;
  /* ⚠️ 这里**不再**延迟销毁旧屏（2026-09-25 改）：
     回桌面 = 切后台，应用要留在内存里，否则后台管理页里"点不点结束都会被结束"
     —— 实测一次 back 就把 clock + weather 一起 release 了。
     内存压力交给 nav_open() 里的 trimBackground() 按 LRU 兜底。 */
}

void nav_back_home() {
  /* ⚠️ 只切屏，不销毁（同上）。真要销毁走 nav_close() / nav_close_all()
     （后台管理页）或 nav_release_all_except()（Launcher 进浏览器时的独占）。 */
  if (nav_launcher) lv_scr_load(nav_launcher);
}

/* ── 后台管理接口实现 ────────────────────────────────────────────────────── */

int nav_running_list(NavRunningInfo* out, int max) {
  if (!out || max <= 0) return 0;
  lv_obj_t* act = lv_scr_act();
  int n = 0;
  for (int i = 0; i < s_count && n < max; i++) {
    ActivityEntry& e = s_table[i];
    if (!*(e.screen)) continue;                    // 没在内存里
    out[n].id = e.name;
    out[n].bytes = e.usedBytes;
    out[n].current = (*(e.screen) == act);
    n++;
  }
  return n;
}

/* 关掉一个 Activity。三条拒绝线，都是踩过坑的：
     1. 前台（当前正在显示的屏）—— lv_obj_del 掉它 = 删掉 LVGL 正在渲染的树；
     2. Launcher —— 唯一的家，删了就回不去了；
     3. 释放守卫（连接中 / 后台 fetch 还在跑）—— 宁可这次不关，也别写坏树。 */
bool nav_close(const char* id) {
  if (!id || !id[0]) return false;
  lv_obj_t* act = lv_scr_act();
  for (int i = 0; i < s_count; i++) {
    ActivityEntry& e = s_table[i];
    if (strcmp(e.name, id) != 0) continue;
    if (!*(e.screen)) return false;                // 本来就没开
    if (*(e.screen) == act) return false;          // 前台，不关
    if (e.canRelease && !e.canRelease()) return false;
    uint32_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (e.onDestroy) e.onDestroy();
    lv_obj_del(*(e.screen));
    *(e.screen) = nullptr;
    e.usedBytes = 0;
    Serial.printf("[Nav] close %s: DRAM %u -> %u\n", id, (unsigned)before,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
  }
  return false;
}

int nav_close_all() {
  lv_obj_t* act = lv_scr_act();
  int n = 0;
  for (int i = 0; i < s_count; i++) {
    ActivityEntry& e = s_table[i];
    if (!*(e.screen)) continue;
    if (*(e.screen) == act) continue;              // 前台留着
    if (e.canRelease && !e.canRelease()) continue;
    if (nav_close(e.name)) n++;
  }
  return n;
}
