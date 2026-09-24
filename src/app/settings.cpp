#include "settings.h"
#include "icons.h"
#include "nav.h"
#include "status_bar.h"
#include "font_zh.h"
#include "screensaver.h"
#include "../hal/display.h"
#include "../hal/ntp_time.h"
#include <lvgl.h>
#include <WiFi.h>
#include "browser_screen.h"

namespace {

lv_obj_t* g_statusLab = nullptr;
lv_obj_t* g_timeSrcVal = nullptr;   /* "时间源" 那行的值 */
lv_obj_t* g_cacheVal = nullptr;     /* 浏览器缓存 */
lv_obj_t* g_dlVal = nullptr;        /* 已下载页面 */
lv_timer_t* g_ntpTimer = nullptr;
SwipeState g_swipe;

/* 手动校时的"等待中"状态。sync 在后台任务里跑，这里靠定时器轮询结果，
   不在 UI 线程里阻塞等网络。 */
static bool s_pending = false;
static uint32_t s_pendingStart = 0;
static uint32_t s_seenUnix = 0;

void swipe_cb(lv_event_t* e) {
  swipe_detect(e, g_swipe, nav_launcher, SWIPE_H);
}

void refreshTimeSourceLabel() {
  if (!g_timeSrcVal || !lv_obj_is_valid(g_timeSrcVal)) return;
  if (NtpTime::isSynced()) {
    uint32_t ago = NtpTime::sinceSyncMs() / 1000;
    char v[32];
    if (ago < 60) snprintf(v, sizeof(v), "NTP 刚刚同步");
    else if (ago < 3600) snprintf(v, sizeof(v), "NTP %u 分钟前", (unsigned)(ago / 60));
    else snprintf(v, sizeof(v), "NTP %u 小时前", (unsigned)(ago / 3600));
    lv_label_set_text(g_timeSrcVal, v);
  } else {
    lv_label_set_text(g_timeSrcVal, "软时钟（未校时）");
  }
}

/* 存储占用：页面缓存 + LittleFS 里下载下来的页面 */
static void refreshStorageLabel() {
  int pages = 0, dl = 0;
  size_t cb = 0, db = 0;
  BrowserScreen_cacheInfo(&pages, &cb, &dl, &db);
  char v[48];
  if (g_cacheVal && lv_obj_is_valid(g_cacheVal)) {
    snprintf(v, sizeof(v), "%d 页 / %u KB", pages, (unsigned)(cb / 1024));
    lv_label_set_text(g_cacheVal, v);
  }
  if (g_dlVal && lv_obj_is_valid(g_dlVal)) {
    snprintf(v, sizeof(v), "%d 个 / %u KB", dl, (unsigned)(db / 1024));
    lv_label_set_text(g_dlVal, v);
  }
}

static void clear_cache_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  BrowserScreen_clearCache();
  refreshStorageLabel();
  if (g_statusLab && lv_obj_is_valid(g_statusLab))
    lv_label_set_text(g_statusLab, "浏览器缓存已清理");
}

/* 破坏性操作：3 秒内再点一次才真删 */
static uint32_t s_dlArmUntil = 0;
static void clear_dl_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  uint32_t now = millis();
  if (s_dlArmUntil == 0 || now > s_dlArmUntil) {
    s_dlArmUntil = now + 3000;
    if (g_statusLab && lv_obj_is_valid(g_statusLab))
      lv_label_set_text(g_statusLab, "再点一次确认清空下载");
    return;
  }
  s_dlArmUntil = 0;
  int n = BrowserScreen_clearDownloads();
  refreshStorageLabel();
  char m[48];
  if (n < 0) snprintf(m, sizeof(m), "存储不可用");
  else snprintf(m, sizeof(m), "已删除 %d 个页面", n);
  if (g_statusLab && lv_obj_is_valid(g_statusLab)) lv_label_set_text(g_statusLab, m);
}

void ntp_status_cb(lv_timer_t* t) {
  (void)t;
  refreshTimeSourceLabel();
  refreshStorageLabel();
  if (!s_pending || !g_statusLab || !lv_obj_is_valid(g_statusLab)) return;

  uint32_t nowUnix = NtpTime::lastSyncUnix();
  if (nowUnix != s_seenUnix) {
    s_pending = false;
    s_seenUnix = nowUnix;
    lv_label_set_text(g_statusLab, "校时成功");
  } else if (millis() - s_pendingStart > 12000) {
    s_pending = false;
    lv_label_set_text(g_statusLab, "校时失败：连不上 NTP 服务器");
  }
}

void calib_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!g_statusLab) return;
  if (WiFi.status() != WL_CONNECTED) {
    lv_label_set_text(g_statusLab, "未联网，先连 WiFi 再校时");
    return;
  }
  s_pending = true;
  s_pendingStart = millis();
  s_seenUnix = NtpTime::lastSyncUnix();
  NtpTime::requestSync();
  lv_label_set_text(g_statusLab, "正在校时…");
}

void autosync_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  NtpTime::setAutoSync(on);
  if (on) NtpTime::requestSync();
  if (g_statusLab && lv_obj_is_valid(g_statusLab)) {
    lv_label_set_text(g_statusLab, on ? "已开启自动校时" : "已关闭自动校时");
  }
}

/* 屏被删（切走）时把定时器一起收掉，否则它会继续写已销毁的 label */
void scr_delete_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  if (g_ntpTimer) { lv_timer_del(g_ntpTimer); g_ntpTimer = nullptr; }
  g_statusLab = nullptr;
  g_timeSrcVal = nullptr;
  g_cacheVal = nullptr;
  g_dlVal = nullptr;
}

void sleep_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  ScreenSaver::sleepNow();
}

/* 桌面图标：单独一屏放得下（设置页本身已排到 y=424，塞不进开关列表） */
void desktop_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!nav_desktop) nav_open(&nav_desktop);
  if (!nav_desktop) {
    if (g_statusLab && lv_obj_is_valid(g_statusLab))
      lv_label_set_text(g_statusLab, "内存不足，打不开桌面设置");
    return;
  }
  nav_go_anim(nav_desktop, LV_SCR_LOAD_ANIM_OVER_RIGHT, 300);
}

void brightness_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
  int val = lv_slider_get_value(slider);
  Display::setBacklightLevel((uint8_t)val);
}

lv_obj_t* makeRow(lv_obj_t* parent, const char* key, const char* val, int y) {
  lv_obj_t* k = lv_label_create(parent);
  lv_label_set_text(k, key);
  lv_obj_set_style_text_color(k, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(k, &font_zh_16, 0);
  lv_obj_align(k, LV_ALIGN_TOP_LEFT, 40, y);

  lv_obj_t* v = lv_label_create(parent);
  lv_label_set_text(v, val);
  lv_obj_set_style_text_color(v, lv_color_white(), 0);
  lv_obj_set_style_text_font(v, &font_zh_16, 0);
  lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -40, y);
  return v;
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

  StatusBar_create(scr, "设置");

  g_timeSrcVal = makeRow(scr, "时间源", "软时钟（未校时）", 86);
  makeRow(scr, "固件", "GEEK TERMINAL v0.1", 120);

  /* 自动校时开关：联网后后台任务会自动对时，并每 6 小时重对一次 */
  lv_obj_t* asLab = lv_label_create(scr);
  lv_label_set_text(asLab, "自动校时");
  lv_obj_set_style_text_color(asLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(asLab, &font_zh_16, 0);
  lv_obj_align(asLab, LV_ALIGN_TOP_LEFT, 40, 154);

  lv_obj_t* sw = lv_switch_create(scr);
  lv_obj_set_size(sw, 50, 26);
  lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, -40, 152);
  lv_obj_add_event_cb(sw, autosync_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_flag(sw, LV_OBJ_FLAG_EVENT_BUBBLE);
  if (NtpTime::autoSyncEnabled()) lv_obj_add_state(sw, LV_STATE_CHECKED);

  // 校准时间按钮
  lv_obj_t* btn = lv_btn_create(scr);
  lv_obj_set_size(btn, 180, 40);
  lv_obj_align(btn, LV_ALIGN_TOP_MID, -100, 192);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, lv_color_white(), 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_radius(btn, 10, 0);
  lv_obj_add_event_cb(btn, calib_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t* blab = lv_label_create(btn);
  lv_label_set_text(blab, "校准时间");
  lv_obj_set_style_text_color(blab, lv_color_white(), 0);
  lv_obj_set_style_text_font(blab, &font_zh_16, 0);
  lv_obj_center(blab);

  // 息屏按钮
  lv_obj_t* sbtn = lv_btn_create(scr);
  lv_obj_set_size(sbtn, 180, 40);
  lv_obj_align(sbtn, LV_ALIGN_TOP_MID, 100, 192);
  lv_obj_set_style_bg_opa(sbtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(sbtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(sbtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(sbtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(sbtn, 1, 0);
  lv_obj_set_style_radius(sbtn, 10, 0);
  lv_obj_add_event_cb(sbtn, sleep_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(sbtn, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t* slab = lv_label_create(sbtn);
  lv_label_set_text(slab, "息屏");
  lv_obj_set_style_text_color(slab, lv_color_white(), 0);
  lv_obj_set_style_text_font(slab, &font_zh_16, 0);
  lv_obj_center(slab);

  // 亮度滑块
  lv_obj_t* brLab = lv_label_create(scr);
  lv_label_set_text(brLab, "亮度");
  lv_obj_set_style_text_color(brLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(brLab, &font_zh_16, 0);
  lv_obj_align(brLab, LV_ALIGN_TOP_LEFT, 40, 252);

  lv_obj_t* slider = lv_slider_create(scr);
  lv_obj_set_width(slider, 280);
  lv_obj_align(slider, LV_ALIGN_TOP_MID, 30, 254);
  lv_slider_set_range(slider, 5, 100);
  lv_slider_set_value(slider, 100, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
  lv_obj_add_event_cb(slider, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_flag(slider, LV_OBJ_FLAG_EVENT_BUBBLE);

  /* ── 浏览器缓存 / 下载：看得见占比，也删得掉 ── */
  g_cacheVal = makeRow(scr, "浏览器缓存", "0 页 / 0 KB", 306);
  g_dlVal = makeRow(scr, "已下载页面", "0 个 / 0 KB", 340);

  lv_obj_t* cBtn = lv_btn_create(scr);
  lv_obj_set_size(cBtn, 130, 40);
  lv_obj_align(cBtn, LV_ALIGN_TOP_MID, -130, 372);
  lv_obj_set_style_bg_opa(cBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(cBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(cBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(cBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(cBtn, 1, 0);
  lv_obj_set_style_radius(cBtn, 10, 0);
  lv_obj_add_event_cb(cBtn, clear_cache_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(cBtn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t* cLab = lv_label_create(cBtn);
  lv_label_set_text(cLab, "清理缓存");
  lv_obj_set_style_text_color(cLab, lv_color_white(), 0);
  lv_obj_set_style_text_font(cLab, &font_zh_16, 0);
  lv_obj_center(cLab);

  lv_obj_t* dBtn = lv_btn_create(scr);
  lv_obj_set_size(dBtn, 130, 40);
  lv_obj_align(dBtn, LV_ALIGN_TOP_MID, 0, 372);
  lv_obj_set_style_bg_opa(dBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(dBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(dBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(dBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(dBtn, 1, 0);
  lv_obj_set_style_radius(dBtn, 10, 0);
  lv_obj_add_event_cb(dBtn, clear_dl_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(dBtn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t* dLab = lv_label_create(dBtn);
  lv_label_set_text(dLab, "清理下载");
  lv_obj_set_style_text_color(dLab, lv_color_white(), 0);
  lv_obj_set_style_text_font(dLab, &font_zh_16, 0);
  lv_obj_center(dLab);

  lv_obj_t* kBtn = lv_btn_create(scr);
  lv_obj_set_size(kBtn, 130, 40);
  lv_obj_align(kBtn, LV_ALIGN_TOP_MID, 130, 372);
  lv_obj_set_style_bg_opa(kBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(kBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(kBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(kBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(kBtn, 1, 0);
  lv_obj_set_style_radius(kBtn, 10, 0);
  lv_obj_add_event_cb(kBtn, desktop_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(kBtn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t* kLab = lv_label_create(kBtn);
  lv_label_set_text(kLab, "桌面图标");
  lv_obj_set_style_text_color(kLab, lv_color_white(), 0);
  lv_obj_set_style_text_font(kLab, &font_zh_16, 0);
  lv_obj_center(kLab);

  g_statusLab = lv_label_create(scr);
  lv_label_set_text(g_statusLab, "联网后自动对时（NTP），或点上方按钮手动校准");
  lv_obj_set_style_text_color(g_statusLab, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(g_statusLab, &font_zh_16, 0);
  lv_obj_align(g_statusLab, LV_ALIGN_TOP_MID, 0, 424);

  refreshTimeSourceLabel();
  refreshStorageLabel();
  if (!g_ntpTimer) g_ntpTimer = lv_timer_create(ntp_status_cb, 1000, nullptr);

  return scr;
}
