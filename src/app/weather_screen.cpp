#include "weather_screen.h"
#include "icons.h"
#include "nav.h"
#include "font_zh.h"
#include <lvgl.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <Arduino.h>
#include <stdio.h>

namespace {

SwipeState g_swipe;
lv_obj_t* g_tempLab = nullptr;
lv_obj_t* g_descLab = nullptr;
lv_obj_t* g_detailLab = nullptr;
lv_obj_t* g_statusLab = nullptr;
lv_obj_t* g_refreshBtn = nullptr;
bool g_fetching = false;
uint32_t g_lastFetch = 0;

const char* wmoDesc(int code) {
  return "";
}

String extractStr(const String& json, const char* key) {
  String pat = String("\"") + key + "\":\"";
  int idx = json.indexOf(pat);
  if (idx < 0) return "";
  idx += pat.length();
  int end = json.indexOf('"', idx);
  if (end < 0) return "";
  return json.substring(idx, end);
}

float extractFloat(const String& json, const char* key) {
  String pat = String("\"") + key + "\":";
  int idx = json.indexOf(pat);
  if (idx < 0) return -999;
  idx += pat.length();
  return json.substring(idx).toFloat();
}

int extractInt(const String& json, const char* key) {
  return (int)extractFloat(json, key);
}

void back_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
}

void swipe_cb(lv_event_t* e) {
  swipe_detect(e, g_swipe, nav_launcher, SWIPE_H);
}

void refresh_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_fetching = true;
  lv_label_set_text(g_statusLab, "加载中...");
}

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    lv_label_set_text(g_statusLab, "未连接WiFi");
    g_fetching = false;
    return;
  }
  HTTPClient http;
  http.setTimeout(8000);
  http.setUserAgent("Mozilla/5.0");

  if (http.begin("http://uapis.cn/api/v1/misc/weather?adcode=110000&extended=true")) {
    int code = http.GET();
    if (code == 200) {
      String body = http.getString();
      http.end();
      float temp = extractFloat(body, "temperature");
      if (temp > -900) {
        String weather = extractStr(body, "weather");
        String windDir = extractStr(body, "wind_direction");
        String windPower = extractStr(body, "wind_power");
        float humidity = extractFloat(body, "humidity");
        float feelsLike = extractFloat(body, "feels_like");
        int aqi = extractInt(body, "aqi");
        String aqiCat = extractStr(body, "aqi_category");
        char buf[64];
        snprintf(buf, sizeof(buf), "%.0f°C", temp);
        lv_label_set_text(g_tempLab, buf);
        lv_label_set_text(g_descLab, weather.c_str());
        snprintf(buf, sizeof(buf), "湿度%.0f%% %s%s 体感%.0f°",
                 humidity, windDir.c_str(), windPower.c_str(), feelsLike);
        lv_label_set_text(g_detailLab, buf);
        snprintf(buf, sizeof(buf), "AQI %d %s", aqi, aqiCat.c_str());
        lv_label_set_text(g_statusLab, buf);
        g_fetching = false;
        g_lastFetch = millis();
        return;
      }
    }
    http.end();
  }

  lv_label_set_text(g_statusLab, "天气获取失败");
  g_fetching = false;
}

}  // namespace

lv_obj_t* WeatherScreen_create() {
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
  lv_label_set_text(title, "天气");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &font_zh_24, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

  lv_obj_t* locLab = lv_label_create(scr);
  lv_label_set_text(locLab, "北京");
  lv_obj_set_style_text_color(locLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(locLab, &font_zh_16, 0);
  lv_obj_align(locLab, LV_ALIGN_TOP_MID, 0, 52);

  g_tempLab = lv_label_create(scr);
  lv_label_set_text(g_tempLab, "--°C");
  lv_obj_set_style_text_color(g_tempLab, lv_color_white(), 0);
  lv_obj_set_style_text_font(g_tempLab, &lv_font_montserrat_48, 0);
  lv_obj_align(g_tempLab, LV_ALIGN_CENTER, 0, -10);

  g_descLab = lv_label_create(scr);
  lv_label_set_text(g_descLab, "--");
  lv_obj_set_style_text_color(g_descLab, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_text_font(g_descLab, &font_zh_24, 0);
  lv_obj_align(g_descLab, LV_ALIGN_CENTER, 0, 50);

  g_detailLab = lv_label_create(scr);
  lv_label_set_text(g_detailLab, "");
  lv_obj_set_style_text_color(g_detailLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(g_detailLab, &font_zh_16, 0);
  lv_obj_align(g_detailLab, LV_ALIGN_CENTER, 0, 90);

  g_statusLab = lv_label_create(scr);
  lv_label_set_text(g_statusLab, WiFi.status() == WL_CONNECTED ? "就绪" : "未连接WiFi");
  lv_obj_set_style_text_color(g_statusLab, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(g_statusLab, &font_zh_16, 0);
  lv_obj_align(g_statusLab, LV_ALIGN_BOTTOM_LEFT, 20, -16);

  g_refreshBtn = lv_btn_create(scr);
  lv_obj_set_size(g_refreshBtn, 80, 36);
  lv_obj_align(g_refreshBtn, LV_ALIGN_BOTTOM_RIGHT, -20, -16);
  lv_obj_set_style_bg_opa(g_refreshBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(g_refreshBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(g_refreshBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(g_refreshBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(g_refreshBtn, 1, 0);
  lv_obj_set_style_radius(g_refreshBtn, 8, 0);
  lv_obj_add_event_cb(g_refreshBtn, refresh_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(g_refreshBtn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t* rl = lv_label_create(g_refreshBtn);
  lv_label_set_text(rl, "刷新");
  lv_obj_set_style_text_color(rl, lv_color_white(), 0);
  lv_obj_set_style_text_font(rl, &font_zh_16, 0);
  lv_obj_center(rl);

  return scr;
}

void WeatherScreen_tick() {
  if (g_fetching) fetchWeather();
}