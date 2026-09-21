#include "browser_screen.h"
#include "icons.h"
#include "nav.h"
#include "font_zh.h"
#include <lvgl.h>
#include <WiFi.h>
#include <WiFiClient.h>

namespace {

SwipeState g_swipe;
lv_obj_t* g_urlArea = nullptr;
lv_obj_t* g_content = nullptr;
lv_obj_t* g_status = nullptr;
lv_obj_t* g_goBtn = nullptr;
String g_fetchUrl;
bool g_fetching = false;
String g_pageContent;

void back_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
}

void swipe_cb(lv_event_t* e) {
  swipe_detect(e, g_swipe, nav_launcher);
}

void go_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char* url = lv_textarea_get_text(g_urlArea);
  if (!url || strlen(url) == 0) return;
  g_fetchUrl = String(url);
  g_fetching = true;
  lv_label_set_text(g_status, "加载中...");
  lv_label_set_text(g_content, "");
}

void stripHtml(const String& html, String& out) {
  out.reserve(html.length());
  bool inTag = false;
  bool inScript = false;
  for (size_t i = 0; i < html.length(); i++) {
    char c = html[i];
    if (!inTag && i + 6 < html.length() &&
        (html.substring(i, i + 6).equalsIgnoreCase("<style") ||
         html.substring(i, i + 7).equalsIgnoreCase("<script"))) {
      inScript = true;
    }
    if (inScript && c == '>') { inScript = false; inTag = false; continue; }
    if (c == '<') { inTag = true; continue; }
    if (c == '>') { inTag = false; continue; }
    if (inTag || inScript) continue;
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (c == '&' && i + 5 < html.length()) {
      if (html.substring(i, i + 6) == "&nbsp;") { out += ' '; i += 5; continue; }
      if (html.substring(i, i + 4) == "&lt;") { out += '<'; i += 3; continue; }
      if (html.substring(i, i + 4) == "&gt;") { out += '>'; i += 3; continue; }
      if (html.substring(i, i + 3) == "&amp;") { out += '&'; i += 4; continue; }
    }
    out += c;
  }
  while (out.indexOf("  ") >= 0) out.replace("  ", " ");
  if (out.length() > 2000) out = out.substring(0, 2000);
}

void fetchPage() {
  String url = g_fetchUrl;
  String host, path;
  int port = 80;

  if (url.startsWith("http://")) url = url.substring(7);
  else if (url.startsWith("https://")) {
    lv_label_set_text(g_status, "不支持HTTPS");
    g_fetching = false;
    return;
  }

  int slash = url.indexOf('/');
  if (slash < 0) { host = url; path = "/"; }
  else { host = url.substring(0, slash); path = url.substring(slash); }

  int colon = host.indexOf(':');
  if (colon > 0) { port = host.substring(colon + 1).toInt(); host = host.substring(0, colon); }

  WiFiClient client;
  client.setTimeout(5000);
  if (!client.connect(host.c_str(), port)) {
    lv_label_set_text(g_status, "连接失败");
    g_fetching = false;
    return;
  }
  client.print(String("GET ") + path + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");

  String response;
  uint32_t startMs = millis();
  while (client.connected() || client.available()) {
    if (client.available()) response += (char)client.read();
    if (millis() - startMs > 8000) break;
  }
  client.stop();

  int bodyStart = response.indexOf("\r\n\r\n");
  if (bodyStart < 0) bodyStart = response.indexOf("\n\n");
  String body = (bodyStart >= 0) ? response.substring(bodyStart + 4) : response;

  String text;
  stripHtml(body, text);
  g_pageContent = text;

  if (text.length() > 0) {
    lv_label_set_text(g_content, text.c_str());
    lv_label_set_text(g_status, "已加载");
  } else {
    lv_label_set_text(g_status, "内容为空");
  }
  g_fetching = false;
}

}  // namespace

lv_obj_t* BrowserScreen_create() {
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
  lv_label_set_text(title, "浏览器");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &font_zh_24, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

  g_urlArea = lv_textarea_create(scr);
  lv_obj_set_size(g_urlArea, 320, 36);
  lv_obj_align(g_urlArea, LV_ALIGN_TOP_MID, 0, 60);
  lv_textarea_set_placeholder_text(g_urlArea, "输入网址");
  lv_textarea_set_text(g_urlArea, "example.com");
  lv_obj_set_style_text_font(g_urlArea, &lv_font_montserrat_14, 0);
  lv_obj_set_style_border_color(g_urlArea, lv_color_hex(0x444444), 0);
  lv_obj_set_style_border_width(g_urlArea, 1, 0);
  lv_obj_set_style_radius(g_urlArea, 6, 0);
  lv_obj_set_style_bg_color(g_urlArea, lv_color_hex(0x111111), 0);

  g_goBtn = lv_btn_create(scr);
  lv_obj_set_size(g_goBtn, 80, 36);
  lv_obj_align(g_goBtn, LV_ALIGN_TOP_MID, 180, 60);
  lv_obj_set_style_bg_opa(g_goBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(g_goBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(g_goBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(g_goBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(g_goBtn, 1, 0);
  lv_obj_set_style_radius(g_goBtn, 6, 0);
  lv_obj_add_event_cb(g_goBtn, go_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* gl = lv_label_create(g_goBtn);
  lv_label_set_text(gl, "加载");
  lv_obj_set_style_text_color(gl, lv_color_white(), 0);
  lv_obj_set_style_text_font(gl, &font_zh_16, 0);
  lv_obj_center(gl);

  g_content = lv_label_create(scr);
  lv_label_set_text(g_content, "");
  lv_obj_set_style_text_color(g_content, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_text_font(g_content, &font_zh_16, 0);
  lv_obj_align(g_content, LV_ALIGN_TOP_LEFT, 20, 110);
  lv_obj_set_width(g_content, 440);
  lv_label_set_long_mode(g_content, LV_LABEL_LONG_WRAP);
  lv_obj_add_flag(g_content, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(g_content, LV_OBJ_FLAG_EVENT_BUBBLE);

  g_status = lv_label_create(scr);
  if (WiFi.status() == WL_CONNECTED) {
    lv_label_set_text(g_status, "就绪");
  } else {
    lv_label_set_text(g_status, "未连接WiFi");
  }
  lv_obj_set_style_text_color(g_status, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(g_status, &font_zh_16, 0);
  lv_obj_align(g_status, LV_ALIGN_BOTTOM_LEFT, 20, -16);

  return scr;
}

void BrowserScreen_tick() {
  if (g_fetching) fetchPage();
}