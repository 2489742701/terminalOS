#include "geoip.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <stdio.h>
#include <string.h>

namespace {

const uint32_t kMaxAgeSec = 24 * 3600;   /* 缓存一天：IP 不会天天变 */

char s_city[32] = "北京";
char s_region[32] = "";
double s_lat = 39.9042;    /* 兜底北京 */
double s_lon = 116.4074;
bool s_valid = false;
uint32_t s_ts = 0;         /* 0 = 没有缓存 */

/* 从 JSON 里抠一个字符串字段（值里没有转义，简单找引号即可） */
bool jsonStr(const String& j, const char* key, char* out, int cap) {
  String pat = String("\"") + key + "\":\"";
  int i = j.indexOf(pat);
  if (i < 0) return false;
  i += pat.length();
  int e = j.indexOf('"', i);
  if (e < 0 || e - i >= cap) return false;
  memcpy(out, j.c_str() + i, (size_t)(e - i));
  out[e - i] = '\0';
  return true;
}

double jsonNum(const String& j, const char* key) {
  String pat = String("\"") + key + "\":";
  int i = j.indexOf(pat);
  if (i < 0) return -9999.0;
  i += pat.length();
  return j.substring(i).toFloat();
}

void saveCache() {
  Preferences p;
  if (!p.begin("geoip", false)) return;   /* 只读模式：命名空间不存在就放弃（避免刷屏） */
  p.putString("city", s_city);
  p.putString("region", s_region);
  p.putDouble("lat", s_lat);
  p.putDouble("lon", s_lon);
  p.putUInt("ts", s_ts);
  p.end();
}

void loadCache() {
  Preferences p;
  if (!p.begin("geoip", true)) return;    /* 只读：没这个命名空间时不报错刷屏 */
  String c = p.getString("city", "");
  String r = p.getString("region", "");
  double la = p.getDouble("lat", -9999.0);
  double lo = p.getDouble("lon", -9999.0);
  uint32_t ts = p.getUInt("ts", 0);
  p.end();
  if (c.length() && la > -9000.0) {
    snprintf(s_city, sizeof(s_city), "%s", c.c_str());
    snprintf(s_region, sizeof(s_region), "%s", r.c_str());
    s_lat = la;
    s_lon = lo;
    s_ts = ts;
    s_valid = true;
  }
}

/* 主源：ip-api.com（中文） */
bool tryIpApi() {
  HTTPClient http;
  http.setTimeout(10000);
  http.setUserAgent("Mozilla/5.0");
  if (!http.begin("http://ip-api.com/json/?fields=status,country,regionName,city,lat,lon&lang=zh-CN"))
    return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    Serial.printf("[GeoIP] ip-api HTTP %d\n", code);
    return false;
  }
  String body = http.getString();
  http.end();

  if (body.indexOf("\"status\":\"success\"") < 0) {
    Serial.printf("[GeoIP] ip-api not success: %.80s\n", body.c_str());
    return false;
  }
  char city[32], region[32];
  double la = jsonNum(body, "lat");
  double lo = jsonNum(body, "lon");
  if (!jsonStr(body, "city", city, sizeof(city)) || la <= -9000.0) return false;
  if (!jsonStr(body, "regionName", region, sizeof(region))) region[0] = '\0';

  snprintf(s_city, sizeof(s_city), "%s", city);
  snprintf(s_region, sizeof(s_region), "%s", region);
  s_lat = la;
  s_lon = lo;
  Serial.printf("[GeoIP] ip-api: %s %s %.4f,%.4f\n", s_region, s_city, s_lat, s_lon);
  return true;
}

/* 备源：ip.sb（HTTPS，字段英文名） */
bool tryIpSb() {
  WiFiClientSecure cli;
  cli.setInsecure();
  cli.setTimeout(12);
  HTTPClient http;
  http.setTimeout(12000);
  http.setUserAgent("Mozilla/5.0");
  if (!http.begin(cli, "https://api.ip.sb/geoip")) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    Serial.printf("[GeoIP] ip.sb HTTP %d\n", code);
    return false;
  }
  String body = http.getString();
  http.end();
  char city[32];
  double la = jsonNum(body, "latitude");
  double lo = jsonNum(body, "longitude");
  if (!jsonStr(body, "city", city, sizeof(city)) || la <= -9000.0) return false;
  snprintf(s_city, sizeof(s_city), "%s", city);
  s_region[0] = '\0';
  s_lat = la;
  s_lon = lo;
  Serial.printf("[GeoIP] ip.sb: %s %.4f,%.4f\n", s_city, s_lat, s_lon);
  return true;
}

}  // namespace

namespace GeoIP {

bool locate(bool force) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[GeoIP] no wifi");
    return s_valid;
  }
  if (!s_ts) loadCache();
  if (!force && s_valid && (millis() / 1000 - s_ts) < kMaxAgeSec) {
    return true;   /* 缓存还新 */
  }
  /* DNS 会偶发失败 -> 两个源各试一次，中间隔一下 */
  for (int attempt = 0; attempt < 2; attempt++) {
    if (tryIpApi()) {
      s_valid = true;
      s_ts = millis() / 1000;
      saveCache();
      return true;
    }
    delay(300);
    if (tryIpSb()) {
      s_valid = true;
      s_ts = millis() / 1000;
      saveCache();
      return true;
    }
    delay(300);
  }
  Serial.println("[GeoIP] both sources failed (keep old / default)");
  return s_valid;
}

bool valid() { return s_valid; }

const char* city() { return s_city; }
const char* region() { return s_region; }
double lat() { return s_lat; }
double lon() { return s_lon; }

uint32_t ageSec() {
  if (!s_ts) return 0xFFFFFFFF;
  return (millis() / 1000) - s_ts;
}

void reset() {
  s_valid = false;
  s_ts = 0;
  snprintf(s_city, sizeof(s_city), "北京");
  s_region[0] = '\0';
  s_lat = 39.9042;
  s_lon = 116.4074;
  Preferences p;
  if (!p.begin("geoip", false)) return;
  p.clear();
  p.end();
}

}  // namespace GeoIP
