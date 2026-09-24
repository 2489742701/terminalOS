#include "settings.h"
#include "settings_menu.h"
#include "settings_store.h"
#include "nav.h"
#include "status_bar.h"
#include "font_zh.h"
#include "screensaver.h"
#include "../hal/display.h"
#include "../hal/ntp_time.h"
#include "../hal/touch.h"
#include "../hal/geoip.h"
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
 *
 * ── 分组口径（改分组 / 加项前先看这段）─────────────────────────────────
 *   判断标准就一条：**改了它，影响范围是 屏 / 网络 / 浏览器 / 系统 的哪一个**。
 *   拿不准的先放系统设置，别为了凑组硬塞。
 *   · 显示与亮度：只影响"这块屏怎么亮"
 *   · 网络与连接：WiFi 及其带来的联网信息（IP / 定位）
 *   · 浏览器设置：只影响浏览器的数据与行为
 *   · 系统设置  ：整机级（时间 / 输入 / 诊断入口）
 *   · 关于本机  ：**只读**，设备是什么、还剩多少资源
 * ══════════════════════════════════════════════════════════════════════════ */

namespace {

lv_obj_t* g_statusLab = nullptr;
lv_timer_t* g_ntpTimer = nullptr;
SwipeState g_swipe;

/* 手动校时的"等待中"状态。sync 在后台任务里跑，这里靠定时器轮询结果，
   不在 UI 线程里阻塞等网络。 */
static bool s_pending = false;
static uint32_t s_pendingStart = 0;
static uint32_t s_seenUnix = 0;

/* valueFn 必须返回静态缓冲：label 会在 refresh_values() 时被反复读。
   ⚠️ 每行一个独立缓冲 —— 共用一份会出现"值互相覆盖"的鬼畜现象。 */
static char s_timeSrcBuf[32];
static char s_cacheBuf[32];
static char s_dlBuf[32];
static char s_wifiBuf[40];
static char s_ipBuf[24];
static char s_rssiBuf[24];
static char s_geoBuf[32];
static char s_vpBuf[20];
static char s_idleBuf[20];
static char s_memBuf[24];
static char s_upBuf[28];
static char s_calBuf[40];

/* ── 循环切换型选项（点一次换下一个）─────────────────────────────────── */
/* 息屏超时用 **Slider 自定义**（0~600 秒，0 = 常亮），不再用预设档循环。
   换掉循环档的两个理由：
     1) 默认档紧挨着「常亮」时，用户点一下就永不熄屏，看着像坏了；
     2) 预设档满足不了"我想设 90 秒"这类需求（master 要的就是自定义）。
   滑块是连续动作，拖动时右侧实时显示数值，既直观也不会一次误触关掉息屏。 */

static const int kVpOpts[] = {0, 720, 1024, 1280};  // 0 = 自动（读 meta viewport）
static const char* const kVpNames[] = {"自动", "720 px", "1024 px", "1280 px"};
static const int kVpCount = 4;

static int indexOfInt(int v, const int* arr, int n, int dflt) {
  for (int i = 0; i < n; i++) if (arr[i] == v) return i;
  return dflt;
}

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
/* 常量直接返回字面量即可（不用缓冲）。"x" 用 ASCII：font_zh_16 里没有 U+00D7 */
const char* chipValue() { return "ESP32-S3"; }
const char* screenValue() { return "480x480 ST7701S"; }

const char* wifiStatusValue() {
  if (WiFi.status() == WL_CONNECTED)
    snprintf(s_wifiBuf, sizeof(s_wifiBuf), "已连接 %s", WiFi.SSID().c_str());
  else
    snprintf(s_wifiBuf, sizeof(s_wifiBuf), "未连接");
  return s_wifiBuf;
}
const char* ipValue() {
  if (WiFi.status() == WL_CONNECTED)
    snprintf(s_ipBuf, sizeof(s_ipBuf), "%s", WiFi.localIP().toString().c_str());
  else
    snprintf(s_ipBuf, sizeof(s_ipBuf), "-");
  return s_ipBuf;
}
const char* rssiValue() {
  if (WiFi.status() == WL_CONNECTED)
    snprintf(s_rssiBuf, sizeof(s_rssiBuf), "%d dBm", (int)WiFi.RSSI());
  else
    snprintf(s_rssiBuf, sizeof(s_rssiBuf), "-");
  return s_rssiBuf;
}
const char* geoValue() {
  if (GeoIP::valid())
    snprintf(s_geoBuf, sizeof(s_geoBuf), "%s %s", GeoIP::region(), GeoIP::city());
  else
    snprintf(s_geoBuf, sizeof(s_geoBuf), "未定位");
  return s_geoBuf;
}
const char* vpValue() {
  int i = indexOfInt(BrowserScreen_getViewport(), kVpOpts, kVpCount, 0);
  snprintf(s_vpBuf, sizeof(s_vpBuf), "%s", kVpNames[i]);
  return s_vpBuf;
}
const char* idleValue() {
  unsigned long sec = ScreenSaver::idleTimeout() / 1000;
  if (sec == 0) snprintf(s_idleBuf, sizeof(s_idleBuf), "常亮");
  else if (sec < 60) snprintf(s_idleBuf, sizeof(s_idleBuf), "%lu 秒", sec);
  else snprintf(s_idleBuf, sizeof(s_idleBuf), "%lu 分 %lu 秒", sec / 60, sec % 60);
  return s_idleBuf;
}
const char* memValue() {
  snprintf(s_memBuf, sizeof(s_memBuf), "%u KB 空闲", (unsigned)(ESP.getFreeHeap() / 1024));
  return s_memBuf;
}
const char* uptimeValue() {
  uint32_t s = millis() / 1000;
  snprintf(s_upBuf, sizeof(s_upBuf), "%u 时 %02u 分 %02u 秒",
           (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
  return s_upBuf;
}
const char* calValue() {
  int x0, x1, y0, y1;
  bool swap;
  Touch::getCal(x0, x1, y0, y1, swap);
  snprintf(s_calBuf, sizeof(s_calBuf), "%d,%d / %d,%d%s",
           x0, x1, y0, y1, swap ? " 已交换" : "");
  return s_calBuf;
}

void setStatus(const char* s) {
  if (g_statusLab && lv_obj_is_valid(g_statusLab)) lv_label_set_text(g_statusLab, s);
}

/* 左边缘滑入 = 返回。⚠️ 不能直接回桌面：设置是多级菜单，
   在二级页滑一下应该回一级（settings_menu_back 内部按 depth 处理，
   depth==0 时才回桌面）。swipe_back_act 要 void(*)() 的裸函数指针，
   settings_menu_back 是 lv_event_cb_t（e 恒为 nullptr），这里包一层。 */
static void settings_back_action() { settings_menu_back(nullptr); }

void swipe_cb(lv_event_t* e) {
  swipe_back_act_any(e, g_swipe, settings_back_action);
}

/* 跳到另一屏（系统设置里的诊断入口统一走这里）。
   ⚠️ 那些屏的返回键是 nav_back_home（回桌面），不会退回设置页 ——
      这是现状，别指望它像子菜单一样能返回。 */
static void openScreen(lv_obj_t** target, const char* name) {
  if (!*target) nav_open(target);
  if (!*target) {
    char m[48];
    snprintf(m, sizeof(m), "内存不足，打不开%s", name);
    setStatus(m);
    return;
  }
  nav_go_anim(*target, LV_SCR_LOAD_ANIM_OVER_RIGHT, 300);
}

static void desktop_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  openScreen(&nav_desktop, "桌面图标");
}
static void wifi_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  openScreen(&nav_wifi, "WiFi");
}
static void sysinfo_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  openScreen(&nav_sysinfo, "系统信息");
}
static void taskmgr_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  openScreen(&nav_taskmgr, "后台管理");
}
static void touchtest_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  openScreen(&nav_touchtest, "触摸校准");
}

/* 息屏超时滑块：单位是**秒**，0 = 常亮（永不熄屏）。 */
static void idle_slider_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
  int sec = lv_slider_get_value(sl);
  ScreenSaver::setIdleTimeout((unsigned long)sec * 1000);
  SettingsStore::saveIdle((unsigned long)sec * 1000);
  settings_menu_refresh_values();
  char m[56];
  if (sec == 0) snprintf(m, sizeof(m), "常亮：屏幕不再熄灭，注意烧屏");
  else snprintf(m, sizeof(m), "息屏超时：%s", idleValue());
  setStatus(m);
}

static void vp_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  int i = indexOfInt(BrowserScreen_getViewport(), kVpOpts, kVpCount, 0);
  i = (i + 1) % kVpCount;
  BrowserScreen_setViewport(kVpOpts[i]);
  SettingsStore::saveViewport(kVpOpts[i]);
  settings_menu_refresh_values();
  char m[40];
  snprintf(m, sizeof(m), "排版视口：%s（下次加载生效）", kVpNames[i]);
  setStatus(m);
}

/* 页面服务器：把 LittleFS 里存下来的页面用 HTTP 共享出去（PC 访问设备 IP） */
static void serve_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  BrowserScreen_serve(on);
  setStatus(on ? "页面服务器已开启" : "页面服务器已关闭");
}

static void refreshCalUi(const char* what) {
  settings_menu_refresh_values();
  char m[48];
  snprintf(m, sizeof(m), "%s -> %s", what, calValue());
  setStatus(m);
}
static void calFlipX_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  Touch::flipX(); refreshCalUi("翻转 X");
}
static void calFlipY_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  Touch::flipY(); refreshCalUi("翻转 Y");
}
static void calSwap_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  Touch::swapXY(); refreshCalUi("交换 XY");
}
static void calReset_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  Touch::resetCal(); refreshCalUi("恢复默认");
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
  SettingsStore::saveAutoSync(on);
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

void brightness_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
  int val = lv_slider_get_value(slider);
  Display::setBacklightLevel((uint8_t)val);
  SettingsStore::saveBrightness(val);
}

/* 动画开关。⚠️ **重启才生效**：只写 NVS，不改运行中的 g_uiAnim ——
   半路改会让"已经在飞的动画"和"新动画"表现不一致（详见 settings_store.h）。
   右侧文字给出当前生效值，让用户知道改完还得重启。 */
const char* animValue() {
  static char b[24];
  bool stored = SettingsStore::animEnabled();   // 下次开机会生效的值
  /* 跟当前生效值(g_uiAnim)不一致才提示重启 —— 不然每次都挂着"重启生效"很吵 */
  snprintf(b, sizeof(b), "%s%s", stored ? "开" : "关",
           stored == g_uiAnim ? "" : " · 重启生效");
  return b;
}
void anim_toggle_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  SettingsStore::saveAnim(on);
  Serial.printf("[Settings] anim=%d (重启后生效，当前生效值=%d)\n", (int)on,
                (int)g_uiAnim);
  settings_menu_refresh_values();
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
     运行时值（亮度 / 自动校时开关）在下面 patch 进去。 */
  static SettingsItem rootItems[] = {
    siNav("显示与亮度", "display"),
    siNav("网络与连接", "network"),
    siNav("浏览器设置", "browser"),
    siNav("系统设置", "system"),
    siNav("关于本机", "about"),
    siEnd(),
  };
  static SettingsItem displayItems[] = {
    siSlider("亮度", 5, 100, 100, brightness_cb),
    siSlider("息屏超时", 0, 600, 300, idle_slider_cb, idleValue),
    siToggle("动画效果", true, anim_toggle_cb, animValue),
    siAction("立即息屏", sleep_event_cb),
    siAction("桌面图标", desktop_event_cb),
    siEnd(),
  };
  static SettingsItem networkItems[] = {
    siAction("WiFi 网络", wifi_event_cb),
    siReadOnly("连接状态", wifiStatusValue),
    siReadOnly("IP 地址", ipValue),
    siReadOnly("信号强度", rssiValue),
    siReadOnly("定位城市", geoValue),
    siEnd(),
  };
  static SettingsItem browserItems[] = {
    siAction("排版视口", vp_cb, vpValue),
    siToggle("页面服务器", false, serve_cb),
    siReadOnly("页面缓存", cacheValue),
    siReadOnly("已下载页面", dlValue),
    siAction("清理缓存", clear_cache_cb),
    siAction("清理下载", clear_dl_cb),
    siEnd(),
  };
  static SettingsItem systemItems[] = {
    siReadOnly("时间源", timeSrcValue),
    siToggle("自动校时", false, autosync_cb),
    siAction("校准时间", calib_event_cb),
    siNav("触摸校准", "touch"),
    siAction("系统信息", sysinfo_event_cb),
    siAction("后台管理", taskmgr_event_cb),
    siEnd(),
  };
  static SettingsItem touchItems[] = {
    siReadOnly("当前参数", calValue),
    siAction("翻转 X", calFlipX_cb),
    siAction("翻转 Y", calFlipY_cb),
    siAction("交换 XY", calSwap_cb),
    siAction("恢复默认", calReset_cb),
    siAction("触摸测试", touchtest_event_cb),
    siEnd(),
  };
  static SettingsItem aboutItems[] = {
    siReadOnly("固件", fwValue),
    siReadOnly("芯片", chipValue),
    siReadOnly("屏幕", screenValue),
    siReadOnly("空闲内存", memValue),
    siReadOnly("运行时长", uptimeValue),
    siEnd(),
  };
  static const SettingsPage pages[] = {
    {"root", "设置", rootItems},
    {"display", "显示与亮度", displayItems},
    {"network", "网络与连接", networkItems},
    {"browser", "浏览器设置", browserItems},
    {"system", "系统设置", systemItems},
    {"touch", "触摸校准", touchItems},
    {"about", "关于本机", aboutItems},
  };

  /* 初值一律从 SettingsStore 读（它开机时已从 NVS 恢复并应用到各模块），
     不再用 s_brightness 这类局部静态 —— 否则每次进设置页都跳回代码默认值。 */
  displayItems[0].vinit = SettingsStore::brightness();
  displayItems[1].vinit = (int)(SettingsStore::idleMs() / 1000);
  displayItems[2].checked = SettingsStore::animEnabled();   /* 动画开关 */
  systemItems[1].checked = SettingsStore::autoSync();

  refreshStorageBufs();
  settings_menu_begin(scr, bar, pages, 7, "root");

  g_statusLab = lv_label_create(scr);
  lv_label_set_text(g_statusLab, "联网后自动对时（NTP），或在「系统设置」里手动校准");
  lv_obj_set_style_text_color(g_statusLab, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(g_statusLab, &font_zh_16, 0);
  lv_obj_set_width(g_statusLab, 456);
  lv_obj_set_style_text_align(g_statusLab, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(g_statusLab, LV_ALIGN_TOP_MID, 0, 428);

  if (!g_ntpTimer) g_ntpTimer = lv_timer_create(ntp_status_cb, 1000, nullptr);

  return scr;
}
