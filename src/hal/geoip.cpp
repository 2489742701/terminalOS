#include "geoip.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */

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

/* 英文名 -> 中文。
   背景：能给中文的 ip-api.com 在**设备侧连不上**（HTTP -1，PC 上正常）；
   能连上的 ip.sb 只给英文名（Nanjing）。
   反向 geocoding 三源在设备侧实测全灭：nominatim -1 / bigdatacloud 403 /
   ip-api-http -1。所以走**本地映射表** —— 零网络、零失败点、够用。
   表外城市保留英文（比显示"北京"强）。 */
struct ZhMap { const char* en; const char* zh; };
static const ZhMap kZhMap[] = {
  {"Beijing","北京"},     {"Shanghai","上海"},   {"Guangzhou","广州"},
  {"Shenzhen","深圳"},    {"Nanjing","南京"},    {"Hangzhou","杭州"},
  {"Suzhou","苏州"},      {"Wuhan","武汉"},      {"Chengdu","成都"},
  {"Chongqing","重庆"},   {"Tianjin","天津"},    {"Xi'an","西安"},
  {"Xian","西安"},        {"Qingdao","青岛"},    {"Dalian","大连"},
  {"Shenyang","沈阳"},    {"Harbin","哈尔滨"},   {"Zhengzhou","郑州"},
  {"Changsha","长沙"},    {"Ningbo","宁波"},     {"Wuxi","无锡"},
  {"Xiamen","厦门"},      {"Fuzhou","福州"},     {"Jinan","济南"},
  {"Hefei","合肥"},       {"Kunming","昆明"},    {"Nanning","南宁"},
  {"Foshan","佛山"},      {"Dongguan","东莞"},   {"Nantong","南通"},
  {"Changzhou","常州"},   {"Shijiazhuang","石家庄"}, {"Taiyuan","太原"},
  {"Lanzhou","兰州"},     {"Guiyang","贵阳"},    {"Haikou","海口"},
  {"Sanya","三亚"},       {"Wenzhou","温州"},    {"Zhuhai","珠海"},
  {"Zhongshan","中山"},   {"Huizhou","惠州"},    {"Xuzhou","徐州"},
  {"Yantai","烟台"},      {"Weifang","潍坊"},    {"Luoyang","洛阳"},
  {"Nanchang","南昌"},    {"Hohhot","呼和浩特"}, {"Urumqi","乌鲁木齐"},
  {"Lhasa","拉萨"},       {"Xining","西宁"},     {"Yinchuan","银川"},
  {"Hong Kong","香港"},   {"Macau","澳门"},      {"Taipei","台北"},
  {"Tokyo","东京"},       {"Osaka","大阪"},      {"Seoul","首尔"},
  {"Singapore","新加坡"}, {"Bangkok","曼谷"},    {"Kuala Lumpur","吉隆坡"},
  {"New York","纽约"},    {"Los Angeles","洛杉矶"}, {"San Francisco","旧金山"},
  {"London","伦敦"},      {"Paris","巴黎"},      {"Berlin","柏林"},
  {"Sydney","悉尼"},      {"Melbourne","墨尔本"},{"Toronto","多伦多"},
};
const char* zhFromEn(const char* en) {
  if (!en || !*en) return nullptr;
  for (size_t i = 0; i < sizeof(kZhMap) / sizeof(kZhMap[0]); i++) {
    if (strcasecmp(en, kZhMap[i].en) == 0) return kZhMap[i].zh;
  }
  return nullptr;
}

/* 中文城市名：ip-api 能直接给中文，但它在**设备侧连不上**（HTTP -1，
   PC 上同一 URL 正常），实际一直走 ip.sb，而 ip.sb 只给英文名（Nanjing）。
   所以补一步：用 open-meteo 自带的 reverse geocoding 反查同一个坐标换回中文。
   选它是因为跟天气同一个服务商、同一个 HTTPS 通道，DNS/连通性表现一致。 */
bool tryGeocodeZh(double la, double lo) {
  WiFiClientSecure cli;
  cli.setInsecure();
  cli.setTimeout(12);
  HTTPClient http;
  http.setTimeout(12000);
  http.setUserAgent("Mozilla/5.0");
  char url[200];
  snprintf(url, sizeof(url),
           "https://geocoding-api.open-meteo.com/v1/search"
           "?latitude=%.4f&longitude=%.4f&count=1&language=zh&format=json",
           la, lo);
  if (!http.begin(cli, url)) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    Serial.printf("[GeoIP] geocode HTTP %d\n", code);
    return false;
  }
  String body = http.getString();
  http.end();
  if (body.indexOf("\"results\"") < 0) {
    Serial.printf("[GeoIP] geocode no results: %.80s\n", body.c_str());
    return false;
  }
  char name[32];
  if (!jsonStr(body, "name", name, sizeof(name)) || !name[0]) return false;
  snprintf(s_city, sizeof(s_city), "%s", name);
  char adm[32];
  if (jsonStr(body, "admin1", adm, sizeof(adm)))
    snprintf(s_region, sizeof(s_region), "%s", adm);
  Serial.printf("[GeoIP] geocode zh: %s (%s)\n", s_city, s_region);
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
      {   /* 英文名换中文：本地表优先（零网络），表外再试网络反向查询 */
        const char* zh = zhFromEn(s_city);
        if (zh) snprintf(s_city, sizeof(s_city), "%s", zh);
        else tryGeocodeZh(s_lat, s_lon);
      }
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

/* 设备侧实测各个反向 geocoding 源（PC 走代理，PC 上的结果不作数）。
   geotest 命令调用，打印每个源的 HTTP 码和返回体开头，好据此选源。

   ⚠️ 踩过：第一版用 strdup 存 URL，最后统一 free() —— 但第三个源是
   **字符串字面量**（不是堆内存），free 它直接
   assert failed: heap_caps_free ... "free() target pointer is outside heap areas"
   整机重启。现在改成固定数组，彻底不碰堆。 */
void probe() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[GeoIP] probe: no wifi");
    return;
  }
  struct Src { const char* name; char url[320]; bool https; };
  Src srcs[3];
  srcs[0].https = true;
  srcs[0].name = "nominatim";
  snprintf(srcs[0].url, sizeof(srcs[0].url),
           "https://nominatim.openstreetmap.org/reverse?format=jsonv2"
           "&lat=%.4f&lon=%.4f&accept-language=zh-CN&zoom=10", s_lat, s_lon);
  srcs[1].https = true;
  srcs[1].name = "bigdatacloud";
  snprintf(srcs[1].url, sizeof(srcs[1].url),
           "https://api.bigdatacloud.net/data/reverse-geocode"
           "?latitude=%.4f&longitude=%.4f&localityLanguage=zh", s_lat, s_lon);
  srcs[2].https = false;
  srcs[2].name = "ip-api-http";
  snprintf(srcs[2].url, sizeof(srcs[2].url),
           "http://ip-api.com/json/?fields=status,city&lang=zh-CN");

  for (int i = 0; i < 3; i++) {
    WiFiClientSecure* sec = nullptr;
    HTTPClient http;
    http.setTimeout(12000);
    http.setUserAgent("Mozilla/5.0");
    bool okBegin;
    if (srcs[i].https) {
      sec = new WiFiClientSecure();
      sec->setInsecure();
      sec->setTimeout(12);
      okBegin = http.begin(*sec, srcs[i].url);
    } else {
      okBegin = http.begin(srcs[i].url);
    }
    if (!okBegin) {
      Serial.printf("[GeoIP] %-12s begin failed\n", srcs[i].name);
      delete sec;
      continue;
    }
    int code = http.GET();
    String b = (code == 200) ? http.getString() : String("");
    http.end();
    delete sec;
    Serial.printf("[GeoIP] %-12s HTTP %d  %.160s\n", srcs[i].name, code, b.c_str());
  }
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
