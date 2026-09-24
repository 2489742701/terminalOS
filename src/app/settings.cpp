#include "settings.h"
#include "settings_menu.h"
#include "nav.h"
#include "status_bar.h"
#include "font_zh.h"
#include "screensaver.h"
#include "../hal/display.h"
#include "../hal/ntp_time.h"
#include <lvgl.h>
#include <WiFi.h>
#include <Arduino.h>
#include "browser_screen.h"

/* ══ 设置页：数据驱动 + 二级菜单 ════════════════════════════════════════════
 * 以前每一项都是手摆绝对坐标（y=86/120/154/…），每项 ~15 行样式样板。
 * 那种结构上加二级菜单 = 每加一项继续堆 15 行，放不下也改不动。
 * 现在内容变成数组：加一项 = 加一行。渲染器见 settings_menu.cpp。
 *
 * 二级菜单 = **同一个屏 + 数据源栈**，不是每级建一个 Activity：
 *   · nav 表不会被撑爆
 *   · 不会每页多一棵常驻对象树（被"10 屏常驻导致 DRAM 不够"坑过）
 * ══════════════════════════════════════════════════════════════════════════ */

namespace {

lv_obj_t* g_statusLab = nullptr;
lv_timer_t* g_ntpTimer = nullptr;
SwipeState g_swipe;

/* 亮度没有 getter，用静态变量记住上次值 —— 否则每次进设置滑块都跳回 100 */
static uint8_t s_brightness = 100;

/* 手动校时的"等待中"状态。sync 在后台任务里跑，这里靠定时器轮询结果，
   不在 UI 线程里阻塞等网络。 */
static bool s_pending = false;
static uint32_t s_pendingStart = 0;
static uint32_t s_seenUnix = 0;

/* valueFn 必须返回静态缓冲：label 会在 refresh_values() 时被反复读 */
static char s_timeSrcBuf[32];
static char s_cacheBuf[32];
static char s_dlBuf[32];

/* ── 各行的"值" ───────────────────────────────────────────────────────── */
const char* timeSrcValue() {
  if (NtpTime::isSynced()) {
    uint32_t ago = NtpTime::sinceSyncMs() / 1000;
    if (ago < 60) snprintf(s_timeSrcBuf, sizeof(s_timeSrcBuf), "NTP 刚刚同步");
    else if (ago < 3600)
      snprintf(s_timeSrcBuf, sizeof(s_timeSrcBuf), "NTP %u 分钟前", (unsigned)(ago / 60));
    else
      snprintf(s_timeSrcBuf, sizeof(s_timeSrcBuf), "NTP %u 小时前", (unsigned)(ago / 3600));
  } else {
    snprintf(s_timeSrcBuf, sizeof(s_timeSrcBuf), "软时钟（未校时）");
  }
  return s_timeSrcBuf;
}

/* 存储占用：页面缓存 + LittleFS 里下载下来的页面 */
static void refreshStorageBufs() {
  int pages = 0, dl = 0;
  size_t cb = 0, db = 0;
  BrowserScreen_cacheInfo(&pages, &cb, &dl, &db);
  snprintf(s_cacheBuf, sizeof(s_cacheBuf), "%d 页 / %u KB", pages, (unsigned)(cb / 1024));
  snprintf(s_dlBuf, sizeof(s_dlBuf), "%d 个 / %u KB", dl, (unsigned)(db / 1024));
}
const char* cacheValue() { return s_cacheBuf; }
const char* dlValue() { return s_dlBuf; }
const char* fwValue() { return "GEEK TERMINAL v0.1"; }

void setStatus(const char* s) {
  if (g_statusLab && lv_obj_is_valid(g_statusLab)) lv_label_set_text(g_statusLab, s);
}

void swipe_cb(lv_event_t* e) {
  swipe_detect(e, g_swipe, nav_launcher, SWIPE_H, false, 40);
}

static void clear_cache_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  BrowserScreen_clearCache();
  refreshStorageBufs();
  settings_menu_refresh_values();
  setStatus("浏览器缓存已清理");
}

/* 破坏性操作：3 秒内再点一次才真删 */
static uint32_t s_dlArmUntil = 0;
static void clear_dl_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  uint32_t now = millis();
  if (s_dlArmUntil == 0 || now > s_dlArmUntil) {
    s_dlArmUntil = now + 3000;
    setStatus("再点一次确认清空下载");
    return;
  }
  s_dlArmUntil = 0;
  int n = BrowserScreen_clearDownloads();
  refreshStorageBufs();
  settings_menu_refresh_values();
  char m[48];
  if (n < 0) snprintf(m, sizeof(m), "存储不可用");
  else snprintf(m, sizeof(m), "已删除 %d 个页面", n);
  setStatus(m);
}

void ntp_status_cb(lv_timer_t* t) {
  (void)t;
  refreshStorageBufs();
  settings_menu_refresh_values();
  if (!s_pending) return;

  uint32_t nowUnix = NtpTime::lastSyncUnix();
  if (nowUnix != s_seenUnix) {
    s_pending = false;
    s_seenUnix = nowUnix;
    setStatus("校时成功");
  } else if (millis() - s_pendingStart > 12000) {
    s_pending = false;
    setStatus("校时失败：连不上 NTP 服务器");
  }
}

void calib_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("未联网，先连 WiFi 再校时");
    return;
  }
  s_pending = true;
  s_pendingStart = millis();
  s_seenUnix = NtpTime::lastSyncUnix();
  NtpTime::requestSync();
  setStatus("正在校时…");
}

void autosync_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  NtpTime::setAutoSync(on);
  if (on) NtpTime::requestSync();
  setStatus(on ? "已开启自动校时" : "已关闭自动校时");
}

/* 屏被删（切走）时把定时器一起收掉，否则它会继续写已销毁的 label */
void scr_delete_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  if (g_ntpTimer) { lv_timer_del(g_ntpTimer); g_ntpTimer = nullptr; }
  g_statusLab = nullptr;
}

void sleep_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  ScreenSaver::sleepNow();
}

/* 桌面图标：单独一屏放得下（塞不进开关列表） */
void desktop_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!nav_desktop) nav_open(&nav_desktop);
  if (!nav_desktop) {
    setStatus("内存不足，打不开桌面设置");
    return;
  }
  nav_go_anim(nav_desktop, LV_SCR_LOAD_ANIM_OVER_RIGHT, 300);
}

void brightness_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
  int val = lv_slider_get_value(slider);
  s_brightness = (uint8_t)val;
  Display::setBacklightLevel((uint8_t)val);
}

}  // namespace

lv_obj_t* SettingsScreen_create() {
  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(scr, scr_delete_cb, LV_EVENT_DELETE, NULL);

  /* ⚠️ 用 createEx：二级菜单的"返回"要回上一级，不是回桌面 */
  lv_obj_t* bar = StatusBar_createEx(scr, "设置", settings_menu_back);

  /* ── 页面数据 ──────────────────────────────────────────────────────────
     静态局部数组：避免全局静态初始化顺序问题，且屏销毁后仍可复用。
     运行时值（亮度/自动校时开关）在下面 patch 进去。 */
  static SettingsItem rootItems[] = {
    siNav("显示与亮度", "display"),
    siNav("通用", "general"),
    siEnd(),
  };
  static SettingsItem displayItems[] = {
    siSlider("亮度", 5, 100, 100, brightness_cb),
    siAction("息屏", sleep_event_cb),
    siAction("桌面图标", desktop_event_cb),
    siEnd(),
  };
  static SettingsItem generalItems[] = {
    siReadOnly("时间源", timeSrcValue),
    siToggle("自动校时", false, autosync_cb),
    siAction("校准时间", calib_event_cb),
    siReadOnly("固件", fwValue),
    siReadOnly("浏览器缓存", cacheValue),
    siReadOnly("已下载页面", dlValue),
    siAction("清理缓存", clear_cache_cb),
    siAction("清理下载", clear_dl_cb),
    siEnd(),
  };
  static const SettingsPage pages[] = {
    {"root", "设置", rootItems},
    {"display", "显示与亮度", displayItems},
    {"general", "通用", generalItems},
  };

  displayItems[0].vinit = s_brightness;
  generalItems[1].checked = NtpTime::autoSyncEnabled();

  refreshStorageBufs();
  settings_menu_begin(scr, bar, pages, 3, "root");

  g_statusLab = lv_label_create(scr);
  lv_label_set_text(g_statusLab, "联网后自动对时（NTP），或在「通用」里手动校准");
  lv_obj_set_style_text_color(g_statusLab, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(g_statusLab, &font_zh_16, 0);
  lv_obj_set_width(g_statusLab, 456);
  lv_obj_set_style_text_align(g_statusLab, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(g_statusLab, LV_ALIGN_TOP_MID, 0, 428);

  if (!g_ntpTimer) g_ntpTimer = lv_timer_create(ntp_status_cb, 1000, nullptr);

  return scr;
}
