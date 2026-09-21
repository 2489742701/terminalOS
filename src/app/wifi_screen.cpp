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
lv_obj_t* g_kb = nullptr;
lv_obj_t* g_pwdArea = nullptr;
lv_obj_t* g_pwdLabel = nullptr;
String g_targetSsid;
bool g_scanning = false;
bool g_connected = false;

void back_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
}

void swipe_cb(lv_event_t* e) {
  swipe_detect(e, g_swipe, nav_launcher, SWIPE_H);
}

void updateStatus() {
  if (g_connected) {
    String ip = WiFi.localIP().toString();
    String txt = "已连接 " + String(WiFi.SSID()) + "\nIP:" + ip;
    lv_label_set_text(g_statusLab, txt.c_str());
  } else {
    lv_label_set_text(g_statusLab, "未连接");
  }
}

void kb_ready_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_READY) return;
  const char* pwd = lv_textarea_get_text(g_pwdArea);
  WiFi.disconnect();
  WiFi.begin(g_targetSsid.c_str(), pwd);
  if (g_kb) { lv_obj_del(g_kb); g_kb = nullptr; }
  lv_label_set_text(g_statusLab, "连接中...");
}

void kb_cancel_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CANCEL) return;
  if (g_kb) { lv_obj_del(g_kb); g_kb = nullptr; }
}

void connect_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char* ssid = (const char*)lv_event_get_user_data(e);
  g_targetSsid = String(ssid);

  if (g_kb) lv_obj_del(g_kb);
  lv_obj_t* scr = lv_scr_act();
  g_kb = lv_keyboard_create(scr);
  lv_obj_set_size(g_kb, 480, 260);
  lv_obj_align(g_kb, LV_ALIGN_BOTTOM_MID, 0, 0);

  g_pwdArea = lv_textarea_create(scr);
  lv_obj_set_size(g_pwdArea, 360, 40);
  lv_obj_align(g_pwdArea, LV_ALIGN_TOP_MID, 0, 180);
  lv_textarea_set_placeholder_text(g_pwdArea, "输入密码");
  lv_textarea_set_password_mode(g_pwdArea, true);
  lv_obj_set_style_text_font(g_pwdArea, &font_zh_16, 0);
  lv_obj_set_style_bg_color(g_pwdArea, lv_color_hex(0x111111), 0);

  g_pwdLabel = lv_label_create(scr);
  lv_label_set_text(g_pwdLabel, ssid);
  lv_obj_set_style_text_color(g_pwdLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(g_pwdLabel, &font_zh_16, 0);
  lv_obj_align(g_pwdLabel, LV_ALIGN_TOP_MID, 0, 150);

  lv_keyboard_set_textarea(g_kb, g_pwdArea);
  lv_obj_add_event_cb(g_kb, kb_ready_cb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(g_kb, kb_cancel_cb, LV_EVENT_CANCEL, NULL);
}

void scan_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_scanning) return;
  g_scanning = true;
  lv_label_set_text(g_statusLab, "扫描中...");
  lv_obj_clear_flag(g_scanBtn, LV_OBJ_FLAG_CLICKABLE);
}

void fillList() {
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
    bool openNet = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

    lv_obj_t* btn = lv_btn_create(g_list);
    lv_obj_set_width(btn, 430);
    lv_obj_set_height(btn, 44);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x161616), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 4, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);

    char buf[64];
    int bars = (rssi > -50) ? 4 : (rssi > -65) ? 3 : (rssi > -75) ? 2 : 1;
    snprintf(buf, sizeof(buf), "%s  %s  %d%%", ssid.c_str(), openNet ? "开" : "锁", bars * 25);
    lv_obj_t* lab = lv_label_create(btn);
    lv_label_set_text(lab, buf);
    lv_obj_set_style_text_color(lab, lv_color_white(), 0);
    lv_obj_set_style_text_font(lab, &font_zh_16, 0);
    lv_obj_align(lab, LV_ALIGN_LEFT_MID, 4, 0);

    static char ssidStore[10][33];
    strncpy(ssidStore[i], ssid.c_str(), 32);
    ssidStore[i][32] = 0;
    lv_obj_add_event_cb(btn, connect_cb, LV_EVENT_CLICKED, (void*)ssidStore[i]);
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
  lv_obj_set_size(g_list, 460, 360);
  lv_obj_align(g_list, LV_ALIGN_TOP_MID, 0, 55);
  lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_list, 0, 0);
  lv_obj_set_style_pad_all(g_list, 4, 0);
  lv_obj_clear_flag(g_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(g_list, LV_OBJ_FLAG_EVENT_BUBBLE);

  g_statusLab = lv_label_create(scr);
  lv_label_set_text(g_statusLab, "未连接");
  lv_obj_set_style_text_color(g_statusLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(g_statusLab, &font_zh_16, 0);
  lv_obj_align(g_statusLab, LV_ALIGN_BOTTOM_LEFT, 20, -16);
  lv_obj_add_flag(g_statusLab, LV_OBJ_FLAG_EVENT_BUBBLE);

  g_scanBtn = lv_btn_create(scr);
  lv_obj_set_size(g_scanBtn, 100, 40);
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

  return scr;
}

void WifiScreen_tick() {
  if (g_scanning) {
    int n = WiFi.scanNetworks();
    WiFi.scanDelete();
    fillList();
  }
  if (g_connected != (WiFi.status() == WL_CONNECTED)) {
    g_connected = (WiFi.status() == WL_CONNECTED);
    if (g_statusLab && !g_kb) updateStatus();
  }
}
