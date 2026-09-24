#include "weather_screen.h"
#include "../hal/geoip.h"
#include "icons.h"
#include "nav.h"
#include "status_bar.h"
#include "font_zh.h"
#include <lvgl.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>
#include <stdio.h>

namespace {

SwipeState g_swipe;
lv_obj_t* g_tempLab = nullptr;
lv_obj_t* g_descLab = nullptr;
lv_obj_t* g_detailLab = nullptr;
lv_obj_t* g_statusLab = nullptr;
/* ⚠️ g_locLab 必须提到这里：它要在定位/取数完成后同步刷新城市名。
   之前是 create 里的局部变量，建屏时赋一次值就再也不动，
   于是首次进屏（GeoIP 还没定位）写死成默认的「北京」，
   而状态行写的是真城市 —— 同一屏两个城市名打架。 */
lv_obj_t* g_locLab = nullptr;
lv_obj_t* g_refreshBtn = nullptr;
bool g_fetching = false;
bool g_forceLocate = false;
uint32_t g_lastFetch = 0;

/* WMO weather_code -> 中文。open-meteo 给的是 WMO 4677 代码，
   没有这个表就只能显示数字，看不懂。 */
const char* wmoDesc(int code) {
  switch (code) {
    case 0:  return "晴";
    case 1:  return "大部晴朗";
    case 2:  return "局部多云";
    case 3:  return "阴";
    case 45: return "雾";
    case 48: return "冻雾";
    case 51: case 53: case 55: return "毛毛雨";
    case 56: case 57: return "冻毛毛雨";
    case 61: return "小雨";
    case 63: return "中雨";
    case 65: return "大雨";
    case 66: case 67: return "冻雨";
    case 71: return "小雪";
    case 73: return "中雪";
    case 75: return "大雪";
    case 77: return "米雪";
    case 80: return "阵雨";
    case 81: return "强阵雨";
    case 82: return "暴雨";
    case 85: case 86: return "阵雪";
    case 95: return "雷阵雨";
    case 96: case 99: return "雷暴伴冰雹";
    default: return "";
  }
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

void swipe_cb(lv_event_t* e) {
  swipe_detect(e, g_swipe, nav_launcher, SWIPE_H);
}

void refresh_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_fetching = true;
  g_forceLocate = true;      /* 刷新 = 顺便重新定位（IP 变了也能跟上） */
  lv_label_set_text(g_statusLab, "加载中...");
}

/* 从 from 位置之后找 key（JSON 里 current_units 和 current 有同名字段，
   不跳过单位块会把 "°C" 当温度解析成 0）。 */
float extractFloatFrom(const String& json, const char* key, int from) {
  String pat = String("\"") + key + "\":";
  int idx = json.indexOf(pat, from);
  if (idx < 0) return -999;
  idx += pat.length();
  return json.substring(idx).toFloat();
}

/* ══ 数据源 ══
   2026-09-24：原来用的 uapis.cn 在**设备侧 DNS 解析失败**
   （串口实锤：hostByName(): DNS Failed for uapis.cn；同一 URL 在 PC 上 200）。
   换 open-meteo：HTTPS、免 key、响应只有 ~500 B，且在本机一次就连上了。
   ⚠️ 走 WiFiClientSecure + setInsecure()：HTTPClient 默认会验证书，
      不 setInsecure 会握手失败（跟浏览器那边的做法一致）。 */
static String makeWeatherUrl() {
  /* 坐标来自 IP 定位（GeoIP），定位不到就退回北京。
     2026-09-24 之前是把北京写死在 URL 里的，所以 master 在南京也看到北京天气。 */
  double la = GeoIP::lat(), lo = GeoIP::lon();
  char buf[192];
  snprintf(buf, sizeof(buf),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code"
           "&timezone=auto", la, lo);
  return String(buf);
}

bool fetchOnce() {
  WiFiClientSecure cli;
  cli.setInsecure();
  cli.setTimeout(12);
  HTTPClient http;
  http.setTimeout(12000);
  http.setUserAgent("Mozilla/5.0");
  String url = makeWeatherUrl();
  Serial.printf("[Weather] url %s\n", url.c_str());
  if (!http.begin(cli, url)) {
    Serial.println("[Weather] begin failed");
    return false;
  }
  int code = http.GET();
  Serial.printf("[Weather] HTTP %d\n", code);
  if (code != 200) { http.end(); return false; }
  String body = http.getString();
  http.end();
  Serial.printf("[Weather] %u B: %.140s\n", (unsigned)body.length(), body.c_str());

  /* ⚠️ 必须从 "current":{ 之后开始找：前面 current_units 里也有
     temperature_2m（值是 "°C"），不跳过会解析成 0°C。 */
  int cur = body.indexOf("\"current\":{");
  if (cur < 0) { Serial.println("[Weather] no current block"); return false; }

  float temp = extractFloatFrom(body, "temperature_2m", cur);
  if (temp <= -900) { Serial.println("[Weather] no temperature"); return false; }
  float humidity = extractFloatFrom(body, "relative_humidity_2m", cur);
  float feels = extractFloatFrom(body, "apparent_temperature", cur);
  float wcode = extractFloatFrom(body, "weather_code", cur);

  char buf[80];
  snprintf(buf, sizeof(buf), "%.0f°C", temp);
  lv_label_set_text(g_tempLab, buf);
  lv_label_set_text(g_descLab, wmoDesc((int)wcode));
  snprintf(buf, sizeof(buf), "湿度%.0f%% 体感%.0f°", humidity, feels);
  lv_label_set_text(g_detailLab, buf);
  snprintf(buf, sizeof(buf), "WMO %d · %s", (int)wcode, GeoIP::city());
  lv_label_set_text(g_statusLab, buf);
  if (g_locLab) lv_label_set_text(g_locLab, GeoIP::city());
  return true;
}

void fetchWeather() {
  g_fetching = false;   /* 先落闸：fetchOnce 是阻塞的，别让下一帧重入 */
  if (!g_statusLab) return;   /* 屏已销毁，别碰悬空指针 */
  if (WiFi.status() != WL_CONNECTED) {
    lv_label_set_text(g_statusLab, "未连接WiFi");
    g_fetching = false;
    return;
  }
  /* 先定位（有缓存就直接用，不会联网）。放在这里而不是建屏时：
     定位是阻塞 HTTP，建屏时做会卡住首帧。 */
  if (g_forceLocate || !GeoIP::valid()) {
    GeoIP::locate(g_forceLocate);
    g_forceLocate = false;
    /* 定位完就把城市标签掰正：fetchOnce 还要跑十几秒 HTTP，
       而且它可能失败 —— 城市该先显示对，不该跟着天气一起卡住。 */
    if (g_locLab) {
      lv_label_set_text(g_locLab, GeoIP::city());
      /* 实锤：把屏幕上真正的字打出来，光看 GeoIP::city() 不算验证 */
      Serial.printf("[Weather] location label now = '%s'\n",
                    lv_label_get_text(g_locLab));
    }
  }

  /* DNS 在这个网络里会偶发失败（news.orz.ai 也遇到过一次），
     所以失败重试一轮，第二次基本都能成。 */
  for (int attempt = 0; attempt < 2; attempt++) {
    if (fetchOnce()) {
      g_lastFetch = millis();
      return;
    }
    delay(300);
  }
lv_label_set_text(g_statusLab, "天气获取失败");
  Serial.println("[Weather] FAILED (see lines above for HTTP code / body)");
  g_fetching = false;
}



}  // namespace

/* 🔒 屏被销毁时把全局指针还清。之前一个都没清 —— 屏销毁后
   g_tempLab/g_descLab/g_detailLab/g_statusLab/g_refreshBtn 全是悬空指针，
   只要 tick 里 g_fetching 还残留 true 就会往已释放对象里写。
   （跟 clock 屏那只忘记注销的 lv_timer 是同一类错误。） */
void scr_delete_cb(lv_event_t* e) {
  (void)e;
  g_tempLab = nullptr;
  g_descLab = nullptr;
  g_detailLab = nullptr;
  g_statusLab = nullptr;
  g_refreshBtn = nullptr;
  g_locLab = nullptr;
  g_fetching = false;
}

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

  StatusBar_create(scr, "天气");
  lv_obj_add_event_cb(scr, scr_delete_cb, LV_EVENT_DELETE, NULL);

  g_locLab = lv_label_create(scr);
  lv_label_set_text(g_locLab, GeoIP::city());
  lv_obj_set_style_text_color(g_locLab, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(g_locLab, &font_zh_16, 0);
  lv_obj_align(g_locLab, LV_ALIGN_TOP_MID, 0, 52);

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
  if (g_fetching && g_statusLab) fetchWeather();
}

/* 串口入口：weather —— 直接拉一次并打印诊断，不用点屏幕。
   ⚠️ 必须在匿名 namespace 之外：namespace 里的函数是内部链接，
      serial_console.cpp 那边链接不到（踩过：undefined reference）。 */
bool WeatherScreen_fetchNow(const char* adcode) {
  if (!g_statusLab) {
    Serial.println("[Weather] screen not created yet");
    return false;
  }
  (void)adcode;   /* adcode 目前固定北京 110000，见 fetchWeather 里的 s_url */
  g_fetching = true;
  fetchWeather();
  return true;
}
