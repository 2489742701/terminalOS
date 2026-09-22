#include "wifi_screen.h"
#include "icons.h"
#include "nav.h"
#include "font_zh.h"
#include <lvgl.h>
#include <WiFi.h>

namespace {

SwipeState g_swipe;
lv_obj_t* g_list = nullptr;
lv_obj_t* g_statusLab = nullptr;
lv_obj_t* g_scanBtn = nullptr;
bool g_scanning = false;
bool g_connected = false;

static char g_ssidStore[10][33];
static bool g_encStore[10];
static int g_rssiStore[10];
char g_detailSsid[33] = {0};
bool g_detailEncrypted = false;
int g_detailRssi = 0;

enum DetailState { DETAIL_IDLE, DETAIL_CONNECTING, DETAIL_FAILED };
DetailState g_detailState = DETAIL_IDLE;
lv_obj_t* g_detailScr = nullptr;
lv_obj_t* g_detailStatusLab = nullptr;
lv_obj_t* g_detailKb = nullptr;
lv_obj_t* g_detailTa = nullptr;
lv_obj_t* g_detailConnectBtn = nullptr;
lv_obj_t* g_detailExitBtn = nullptr;
lv_obj_t* g_detailRetryBtn = nullptr;
uint32_t g_connectStartMs = 0;

void back_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
}

void swipe_cb(lv_event_t* e) {
  swipe_detect(e, g_swipe, nav_launcher, SWIPE_H, false, 40);
}

void updateStatus() {
  if (!g_statusLab) return;
  if (g_connected) {
    String ip = WiFi.localIP().toString();
    String txt = "已连接 " + String(WiFi.SSID()) + "\nIP:" + ip;
    lv_label_set_text(g_statusLab, txt.c_str());
  } else {
    lv_label_set_text(g_statusLab, "未连接");
  }
}

void scan_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_scanning) return;
  g_scanning = true;
  lv_label_set_text(g_statusLab, "扫描中...");
  lv_obj_clear_flag(g_scanBtn, LV_OBJ_FLAG_CLICKABLE);
}

void detail_back_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (nav_is_locked()) return;
  g_detailState = DETAIL_IDLE;
  lv_scr_load_anim(nav_wifi, LV_SCR_LOAD_ANIM_OVER_LEFT, 300, 0, true);
  nav_lock_until = lv_tick_get() + 600;
}

void detail_connect_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (nav_is_locked()) return;
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_user_data(e);
  const char* pwd = ta ? lv_textarea_get_text(ta) : "";
  if (g_detailKb) lv_obj_add_flag(g_detailKb, LV_OBJ_FLAG_HIDDEN);
  if (g_detailConnectBtn) lv_obj_add_flag(g_detailConnectBtn, LV_OBJ_FLAG_HIDDEN);
  if (g_detailExitBtn) lv_obj_add_flag(g_detailExitBtn, LV_OBJ_FLAG_HIDDEN);
  if (g_detailStatusLab) {
    lv_label_set_text(g_detailStatusLab, "正在验证...");
    lv_obj_clear_flag(g_detailStatusLab, LV_OBJ_FLAG_HIDDEN);
  }
  WiFi.disconnect();
  WiFi.begin(g_detailSsid, pwd);
  g_detailState = DETAIL_CONNECTING;
  g_connectStartMs = millis();
}

void retry_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (nav_is_locked()) return;
  g_detailState = DETAIL_IDLE;
  if (g_detailStatusLab) lv_obj_add_flag(g_detailStatusLab, LV_OBJ_FLAG_HIDDEN);
  if (g_detailRetryBtn) lv_obj_add_flag(g_detailRetryBtn, LV_OBJ_FLAG_HIDDEN);
  if (g_detailConnectBtn) lv_obj_clear_flag(g_detailConnectBtn, LV_OBJ_FLAG_HIDDEN);
  if (g_detailExitBtn) lv_obj_clear_flag(g_detailExitBtn, LV_OBJ_FLAG_HIDDEN);
  if (g_detailKb) lv_obj_clear_flag(g_detailKb, LV_OBJ_FLAG_HIDDEN);
  if (g_detailTa) lv_textarea_set_text(g_detailTa, "");
}

void showDetail(const char* ssid, bool encrypted, int rssi) {
  strncpy(g_detailSsid, ssid, 32);
  g_detailSsid[32] = 0;
  g_detailEncrypted = encrypted;
  g_detailRssi = rssi;

  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = icon_create(scr, Icon::Back, 36);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 14, 14);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(back, detail_back_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, ssid);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &font_zh_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  int bars = (rssi > -50) ? 4 : (rssi > -65) ? 3 : (rssi > -75) ? 2 : 1;
  char infoBuf[64];
  snprintf(infoBuf, sizeof(infoBuf), "信号 %d%%  %s", bars * 25, encrypted ? "加密网络" : "开放网络");
  lv_obj_t* infoLab = lv_label_create(scr);
  lv_label_set_text(infoLab, infoBuf);
  lv_obj_set_style_text_color(infoLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(infoLab, &font_zh_16, 0);
  lv_obj_align(infoLab, LV_ALIGN_TOP_MID, 0, 50);

  lv_obj_t* ta = nullptr;
  lv_obj_t* kb = nullptr;

  if (encrypted) {
    lv_obj_t* pwdLabel = lv_label_create(scr);
    lv_label_set_text(pwdLabel, "密码");
    lv_obj_set_style_text_color(pwdLabel, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(pwdLabel, &font_zh_16, 0);
    lv_obj_align(pwdLabel, LV_ALIGN_TOP_LEFT, 60, 80);

    ta = lv_textarea_create(scr);
    lv_obj_set_size(ta, 300, 36);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 102);
    lv_textarea_set_placeholder_text(ta, "输入密码");
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_style_text_font(ta, &font_zh_16, 0);
    lv_obj_set_style_text_color(ta, lv_color_white(), 0);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(ta, lv_color_white(), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 8, 0);

    kb = lv_keyboard_create(scr);
    lv_obj_set_size(kb, 480, 240);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta);
  }

  int btnY = encrypted ? 148 : 80;

  g_detailStatusLab = lv_label_create(scr);
  lv_label_set_text(g_detailStatusLab, "");
  lv_obj_set_style_text_color(g_detailStatusLab, lv_color_hex(0xFFAA00), 0);
  lv_obj_set_style_text_font(g_detailStatusLab, &font_zh_16, 0);
  lv_obj_align(g_detailStatusLab, LV_ALIGN_TOP_MID, 0, btnY + 8);
  lv_obj_add_flag(g_detailStatusLab, LV_OBJ_FLAG_HIDDEN);

  g_detailConnectBtn = lv_btn_create(scr);
  lv_obj_set_size(g_detailConnectBtn, 100, 36);
  lv_obj_align(g_detailConnectBtn, LV_ALIGN_TOP_LEFT, 80, btnY);
  lv_obj_set_style_bg_opa(g_detailConnectBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(g_detailConnectBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(g_detailConnectBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(g_detailConnectBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(g_detailConnectBtn, 1, 0);
  lv_obj_set_style_radius(g_detailConnectBtn, 8, 0);
  lv_obj_add_event_cb(g_detailConnectBtn, detail_connect_cb, LV_EVENT_CLICKED, ta);
  lv_obj_t* clab = lv_label_create(g_detailConnectBtn);
  lv_label_set_text(clab, "进入");
  lv_obj_set_style_text_color(clab, lv_color_white(), 0);
  lv_obj_set_style_text_font(clab, &font_zh_16, 0);
  lv_obj_center(clab);

  g_detailExitBtn = lv_btn_create(scr);
  lv_obj_set_size(g_detailExitBtn, 100, 36);
  lv_obj_align(g_detailExitBtn, LV_ALIGN_TOP_LEFT, 200, btnY);
  lv_obj_set_style_bg_opa(g_detailExitBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(g_detailExitBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(g_detailExitBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(g_detailExitBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(g_detailExitBtn, 1, 0);
  lv_obj_set_style_radius(g_detailExitBtn, 8, 0);
  lv_obj_add_event_cb(g_detailExitBtn, detail_back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* elab = lv_label_create(g_detailExitBtn);
  lv_label_set_text(elab, "退出");
  lv_obj_set_style_text_color(elab, lv_color_white(), 0);
  lv_obj_set_style_text_font(elab, &font_zh_16, 0);
  lv_obj_center(elab);

  g_detailRetryBtn = lv_btn_create(scr);
  lv_obj_set_size(g_detailRetryBtn, 100, 36);
  lv_obj_align(g_detailRetryBtn, LV_ALIGN_TOP_LEFT, 80, btnY);
  lv_obj_set_style_bg_opa(g_detailRetryBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(g_detailRetryBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(g_detailRetryBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(g_detailRetryBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(g_detailRetryBtn, 1, 0);
  lv_obj_set_style_radius(g_detailRetryBtn, 8, 0);
  lv_obj_add_event_cb(g_detailRetryBtn, retry_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* rlab = lv_label_create(g_detailRetryBtn);
  lv_label_set_text(rlab, "重输");
  lv_obj_set_style_text_color(rlab, lv_color_white(), 0);
  lv_obj_set_style_text_font(rlab, &font_zh_16, 0);
  lv_obj_center(rlab);
  lv_obj_add_flag(g_detailRetryBtn, LV_OBJ_FLAG_HIDDEN);

  g_detailScr = scr;
  g_detailKb = kb;
  g_detailTa = ta;
  g_detailState = DETAIL_IDLE;

  lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_OVER_RIGHT, 300, 0, false);
  nav_lock_until = lv_tick_get() + 600;
}

void item_click_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (nav_is_locked()) return;
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (idx < 0 || idx >= 10) return;
  showDetail(g_ssidStore[idx], g_encStore[idx], g_rssiStore[idx]);
}

void fillList() {
  if (!g_list) return;
  lv_obj_clean(g_list);
  int n = WiFi.scanComplete();
  if (n <= 0) {
    lv_obj_add_flag(g_scanBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(g_statusLab, "未找到网络");
    g_scanning = false;
    return;
  }
  for (int i = 0; i < n && i < 10; i++) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    bool enc = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

    strncpy(g_ssidStore[i], ssid.c_str(), 32);
    g_ssidStore[i][32] = 0;
    g_encStore[i] = enc;
    g_rssiStore[i] = rssi;

    lv_obj_t* btn = lv_btn_create(g_list);
    lv_obj_set_width(btn, 440);
    lv_obj_set_height(btn, 50);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x161616), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 6, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(btn, item_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    lv_obj_t* ssidLab = lv_label_create(btn);
    lv_label_set_text(ssidLab, ssid.c_str());
    lv_obj_set_style_text_color(ssidLab, lv_color_white(), 0);
    lv_obj_set_style_text_font(ssidLab, &font_zh_16, 0);
    lv_obj_align(ssidLab, LV_ALIGN_LEFT_MID, 4, 0);

    if (enc) {
      lv_obj_t* lock = icon_create(btn, Icon::Lock, 18);
      lv_obj_align(lock, LV_ALIGN_RIGHT_MID, -60, 0);
    }

    int bars = (rssi > -50) ? 4 : (rssi > -65) ? 3 : (rssi > -75) ? 2 : 1;
    char sigBuf[8];
    snprintf(sigBuf, sizeof(sigBuf), "%d%%", bars * 25);
    lv_obj_t* sigLab = lv_label_create(btn);
    lv_label_set_text(sigLab, sigBuf);
    lv_obj_set_style_text_color(sigLab, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(sigLab, &font_zh_16, 0);
    lv_obj_align(sigLab, LV_ALIGN_RIGHT_MID, -8, 0);
  }
  lv_obj_add_flag(g_scanBtn, LV_OBJ_FLAG_CLICKABLE);
  updateStatus();
  g_scanning = false;
}

}  // namespace

lv_obj_t* WifiScreen_create() {
  WiFi.mode(WIFI_STA);

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

  lv_obj_t* back = icon_create(scr, Icon::Back, 36);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 14, 14);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(back, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(back, back_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "无线网络");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &font_zh_24, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

  g_list = lv_obj_create(scr);
  lv_obj_set_size(g_list, 460, 340);
  lv_obj_align(g_list, LV_ALIGN_TOP_MID, 0, 55);
  lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_list, 0, 0);
  lv_obj_set_style_pad_all(g_list, 4, 0);
  lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
  lv_obj_add_flag(g_list, LV_OBJ_FLAG_EVENT_BUBBLE);

  g_statusLab = lv_label_create(scr);
  lv_label_set_text(g_statusLab, "未连接");
  lv_obj_set_style_text_color(g_statusLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(g_statusLab, &font_zh_16, 0);
  lv_obj_align(g_statusLab, LV_ALIGN_BOTTOM_LEFT, 20, -16);
  lv_obj_add_flag(g_statusLab, LV_OBJ_FLAG_EVENT_BUBBLE);

  g_scanBtn = lv_btn_create(scr);
  lv_obj_set_size(g_scanBtn, 80, 36);
  lv_obj_align(g_scanBtn, LV_ALIGN_BOTTOM_RIGHT, -20, -12);
  lv_obj_set_style_bg_opa(g_scanBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(g_scanBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(g_scanBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(g_scanBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(g_scanBtn, 1, 0);
  lv_obj_set_style_radius(g_scanBtn, 8, 0);
  lv_obj_add_event_cb(g_scanBtn, scan_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(g_scanBtn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t* sl = lv_label_create(g_scanBtn);
  lv_label_set_text(sl, "扫描");
  lv_obj_set_style_text_color(sl, lv_color_white(), 0);
  lv_obj_set_style_text_font(sl, &font_zh_16, 0);
  lv_obj_center(sl);

  g_connected = (WiFi.status() == WL_CONNECTED);
  updateStatus();

  g_scanning = true;
  lv_label_set_text(g_statusLab, "扫描中...");
  lv_obj_clear_flag(g_scanBtn, LV_OBJ_FLAG_CLICKABLE);

  return scr;
}

void WifiScreen_tick() {
  if (g_scanning) {
    WiFi.scanNetworks();
    fillList();
    WiFi.scanDelete();
  }
  if (g_detailState == DETAIL_CONNECTING) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      g_detailState = DETAIL_IDLE;
      g_connected = true;
      if (g_statusLab) updateStatus();
      lv_scr_load_anim(nav_wifi, LV_SCR_LOAD_ANIM_OVER_LEFT, 300, 0, true);
      nav_lock_until = lv_tick_get() + 600;
    } else if (millis() - g_connectStartMs > 10000) {
      g_detailState = DETAIL_FAILED;
      WiFi.disconnect();
      if (g_detailStatusLab) {
        lv_label_set_text(g_detailStatusLab, "连接失败");
        lv_obj_clear_flag(g_detailStatusLab, LV_OBJ_FLAG_HIDDEN);
      }
      if (g_detailRetryBtn) lv_obj_clear_flag(g_detailRetryBtn, LV_OBJ_FLAG_HIDDEN);
      if (g_detailExitBtn) lv_obj_clear_flag(g_detailExitBtn, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (g_connected != (WiFi.status() == WL_CONNECTED)) {
    g_connected = (WiFi.status() == WL_CONNECTED);
    if (g_statusLab) updateStatus();
  }
}

bool WifiScreen_isConnecting() {
  return g_detailState == DETAIL_CONNECTING;
}
