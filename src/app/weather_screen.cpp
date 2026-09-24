#include "weather_screen.h"
#include "../hal/geoip.h"
#include "../hal/sd_card.h"
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
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* ══ 天气页 ════════════════════════════════════════════════════════════════
 * 2026-09-25 重做：以前只显示了「温度 + 描述 + 湿度体感」三个数，
 * 而 open-meteo 一次能给 14 个实况字段 + 13 组 7 日预报（共 ~2.1KB）。
 * master：「展示所有 JSON 能拉到的内容，能下滑看近 7 日预报」。
 *
 * 结构：上半屏固定显示温度/描述/体感，下半屏是一个**可下滑**容器，
 *   依次放「当前实况」网格 →「今日」→「近 7 日预报」。
 *
 * 三条老规矩仍然成立（别改回去）：
 *   · 抓取走**常驻后台任务**，UI 线程只负责贴标签；
 *   · 任务里绝不碰 LVGL；
 *   · 屏销毁时把所有全局指针清干净。
 * ══════════════════════════════════════════════════════════════════════════ */

/* ⚠️ 类型和全局状态**必须在匿名 namespace 之外**：
   WeatherScreen_create / WeatherScreen_tick / scr_delete_cb 要被 nav.cpp、
   serial_console.cpp 链接（外部链接），放进 namespace 会变内部链接 →
   链接期 undefined reference；可它们又要读写这些指针。
   所以全部提到外层，用 static 把链接性收在本 TU 内。 */

constexpr int DAY_N = 7;
constexpr int HOUR_N = 24;
constexpr int CELL_N = 16;

struct DayFc {
  char md[8];        // "09-25"
  int week;          // 0=周日
  int code;
  float tmax, tmin, tappMax, tappMin;
  float precip, pop, wind, gust, uv;
  char sunrise[8], sunset[8];
};
/* 逐时：只取 24 条（forecast_hours=24），就是"今日 0~23 点" */
struct HourFc {
  char hh[4];      // "14"
  float temp;
  int code;
  float pop;
};
struct WeatherResult {
  bool ok;
  char city[32];
  char curTime[8];             // "02:45"
  float temp, humidity, feels, wcode;
  float precip, cloud, pressMsl, pressSurf;
  float wind, windDir, gust;
  float elevation;
  float isDay;                 // 1=白天 0=夜间
  DayFc days[DAY_N];
  int dayCount;
  HourFc hours[HOUR_N];
  int hourCount;
};

static SwipeState g_swipe;

/* ── 后台自动更新（master 2026-09-25）──────────────────────────────────────
 * 开关为 false 时任务用 portMAX_DELAY 等通知 —— 不轮询、不联网、不占 CPU，
 * 是真正的"完全停止"，不是"每小时醒来发现开关关了又睡"。 */
static volatile bool g_auto = true;
static const uint32_t AUTO_MS = 60u * 60u * 1000u;   /* 1 小时 */

/* SD 缓存：整块存原始 JSON，读回来直接复用现成的解析器，
   不用再写一套序列化。3KB 一次，对 SD 毫无压力。 */
#define WX_CACHE_PATH "/gt/weather.json"

/* ── 上半屏 ── */
static lv_obj_t* g_locLab = nullptr;      // 城市
static lv_obj_t* g_updLab = nullptr;      // 更新时间
static lv_obj_t* g_tempLab = nullptr;     // 大温度
static lv_obj_t* g_descLab = nullptr;     // 天气描述
static lv_obj_t* g_subLab = nullptr;      // 体感 / 湿度
static lv_obj_t* g_statusLab = nullptr;   // 底部状态（错误提示）
static lv_obj_t* g_refreshBtn = nullptr;
static lv_obj_t* g_scroll = nullptr;

/* ── 下半屏：「当前实况」网格 ── */
static const char* kCellNames[CELL_N] = {
    "体感", "湿度", "降水", "云量", "气压", "海拔", "风速",
    "阵风", "风向", "今日最高", "今日最低", "日出", "日落", "紫外线",
    "地面气压", "昼夜",
};
static lv_obj_t* g_cellVal[CELL_N] = {nullptr};

/* ── 下半屏：近 7 日 ── */
static lv_obj_t* g_dayDate[DAY_N] = {nullptr};
static lv_obj_t* g_dayDesc[DAY_N] = {nullptr};
static lv_obj_t* g_dayTmp[DAY_N] = {nullptr};
static lv_obj_t* g_dayExtra[DAY_N] = {nullptr};

/* ── 下半屏：今日逐时 ── */
static lv_obj_t* g_hourLab[HOUR_N] = {nullptr};
static lv_obj_t* g_hourCard[HOUR_N] = {nullptr};   /* 卡片本体：按温度上底色 */

/* ── 下半屏：近 7 日的温度条 ── */
static lv_obj_t* g_dayBarBg[DAY_N] = {nullptr};    /* 底槽（本周范围） */
static lv_obj_t* g_dayBar[DAY_N] = {nullptr};      /* 当天 min~max 区间 */

/* 上半屏的天气图标 */
static lv_obj_t* g_wxIcon = nullptr;

static uint32_t g_lastFetch = 0;

/* 前向声明（refresh_cb 在文件前段就要用到） */
static void weatherStart(bool forceLocate);
static void applyResult(const WeatherResult& r);

/* ── WMO weather_code -> 中文 ── */
static const char* wmoDesc(int code) {
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
    default: return "未知";
  }
}

/* 风向度数 -> 八方位中文（气象学上"风从哪来"就叫哪个风） */
static const char* windDirName(float deg) {
  if (deg < 0) return "--";
  static const char* kNames[8] = {"北风", "东北风", "东风", "东南风",
                                  "南风", "西南风", "西风", "西北风"};
  int i = (int)((deg + 22.5f) / 45.0f) & 7;
  return kNames[i];
}

/* Zeller 公式算星期（0=周日）。用来把 "2026-09-25" 标成"周五"。 */
static const char* kWeekName[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
static int weekdayOf(int y, int m, int d) {
  if (m < 3) { m += 12; y--; }
  int K = y % 100, J = y / 100;
  int h = (d + 13 * (m + 1) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
  return (h + 6) % 7;   /* h: 0=周六 1=周日 … -> 0=周日 */
}

static void swipe_cb(lv_event_t* e) {
  /* ⚠️ 这个屏**能下滑**，所以竖滑退出必须关掉（allowVertical=false），
     否则滚到底再往上滑就被当成"退出"了。左右滑退出仍然保留。 */
  swipe_back_to_any(e, g_swipe, nav_launcher, false);
}

static void refresh_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  /* 刷新 = 顺便重新定位（IP 变了也能跟上）。后台任务跑，UI 立刻能继续操作。 */
  weatherStart(true);
}

/* ══ JSON 解析 ══
 * 用 String::indexOf 而不是 ArduinoJson：这项目为了省内存没引 JSON 库，
 * 而 open-meteo 的结构是固定的，取字段用不着通用解析器。 */
static String extractStr(const String& json, const char* key) {
  String pat = String("\"") + key + "\":\"";
  int idx = json.indexOf(pat);
  if (idx < 0) return "";
  idx += pat.length();
  int end = json.indexOf('"', idx);
  if (end < 0) return "";
  return json.substring(idx, end);
}

/* 从 from 位置之后找 key（JSON 里 current_units 和 current 有同名字段，
   不跳过单位块会把 "°C" 当温度解析成 0）。 */
static float extractFloatFrom(const String& json, const char* key, int from) {
  String pat = String("\"") + key + "\":";
  int idx = json.indexOf(pat, from);
  if (idx < 0) return -999;
  idx += pat.length();
  return json.substring(idx).toFloat();
}

/* ── daily 是**数组**（"temperature_2m_max":[30.4,24.3,…]），
   所以要能按下标取元素。下面三个函数就是干这个的。 ── */
static bool arrElem(const String& j, int from, const char* key, int n, int& b, int& e) {
  String pat = String("\"") + key + "\":[";
  int i = j.indexOf(pat, from);
  if (i < 0) return false;
  i += pat.length();
  for (int k = 0; k < n; k++) {          /* 跳过前 n 个逗号 */
    int c = j.indexOf(',', i);
    if (c < 0) return false;
    i = c + 1;
  }
  int comma = j.indexOf(',', i);
  int brack = j.indexOf(']', i);
  int end = -1;
  if (comma >= 0 && brack >= 0) end = (comma < brack) ? comma : brack;
  else if (comma >= 0) end = comma;
  else end = brack;
  if (end < 0) return false;
  b = i; e = end;
  return true;
}
static bool arrNum(const String& j, int from, const char* key, int n, float& out) {
  int b, e;
  if (!arrElem(j, from, key, n, b, e)) return false;
  String t = j.substring(b, e);
  t.trim();
  if (!t.length()) return false;
  out = t.toFloat();
  return true;
}
static bool arrStr(const String& j, int from, const char* key, int n, char* out, int cap) {
  int b, e;
  if (!arrElem(j, from, key, n, b, e)) return false;
  int i = b;
  while (i < e && j[i] != '"') i++;
  if (i >= e) return false;
  i++;
  int s = i;
  while (i < e && j[i] != '"') i++;
  int len = i - s;
  if (len > cap - 1) len = cap - 1;
  memcpy(out, j.c_str() + s, (size_t)len);
  out[len] = '\0';
  return true;
}

/* ══ 数据源 ══
   2026-09-24：原来用的 uapis.cn 在**设备侧 DNS 解析失败**
   （串口实锤：hostByName(): DNS Failed for uapis.cn；同一 URL 在 PC 上 200）。
   换 open-meteo：HTTPS、免 key。⚠️ 走 WiFiClientSecure + setInsecure()：
   HTTPClient 默认会验证书，不 setInsecure 会握手失败。 */
static String makeWeatherUrl() {
  double la = GeoIP::lat(), lo = GeoIP::lon();
  /* 一次把能拿的都拿了：current 全字段 + daily 7 日 + hourly 24 条。
     响应 ~3.0KB，比只取 4 个字段的 0.5KB 大，但一次请求能撑起整页内容。
     ⚠️ URL 实测 594 字节：buf 必须 >= 640，写 512 会被 snprintf 悄悄截断，
        服务端拿到半个参数直接回 400（踩过：设备 400 / PC 同样 URL 200）。 */
  char buf[768];
  snprintf(buf, sizeof(buf),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
           "is_day,precipitation,weather_code,cloud_cover,pressure_msl,"
           "surface_pressure,wind_speed_10m,wind_direction_10m,wind_gusts_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
           "apparent_temperature_max,apparent_temperature_min,precipitation_sum,"
           "precipitation_probability_max,wind_speed_10m_max,wind_gusts_10m_max,"
           "uv_index_max,sunrise,sunset"
           "&hourly=temperature_2m,weather_code,precipitation_probability"
           "&timezone=auto&forecast_days=7&forecast_hours=24", la, lo);
  return String(buf);
}

/* ══ 后台任务取代"UI 里阻塞等 HTTP" ═══════════════════════════════════════
 * 2026-09-25 master：「点进天气就卡住，什么都点不了」。
 * 根因：定位 + HTTPS + 失败重试全在 WeatherScreen_tick()（UI 线程）里同步跑，
 *   一次最坏 2×(12s 超时) ≈ 24 秒 —— 期间 LVGL 不转，点了也没用。
 * ⚠️ 任务里**绝对不碰 LVGL**：只往 g_res 里填数据，贴标签由 tick 在 UI 线程做。
 * ═══════════════════════════════════════════════════════════════════════════ */
static TaskHandle_t g_task = nullptr;
static volatile bool g_done = false;
static volatile bool g_busy = false;    /* 这一轮还在跑 */
static volatile bool g_again = false;   /* 跑的过程中又来了新请求 -> 补一轮 */
static volatile bool g_wantLocate = false;
static WeatherResult g_res;
static bool g_haveRes = false;   /* g_res 里有没有一份能直接贴的结果 */

static bool wxCacheSave(const String& body);
static bool wxCacheLoad(String& out);
/* 解析 open-meteo 的响应体。抽出来是为了 SD 缓存也能复用同一套解析。 */
static bool parseWeatherJson(const String& body, WeatherResult& r);

static bool parseWeatherJson(const String& body, WeatherResult& r) {
  /* ⚠️ 必须从 "current":{ 之后开始找：前面 current_units 里也有
     temperature_2m（值是 "°C"），不跳过会解析成 0°C。 */
  int cur = body.indexOf("\"current\":{");
  if (cur < 0) { Serial.println("[Weather] no current block"); return false; }

  float temp = extractFloatFrom(body, "temperature_2m", cur);
  if (temp <= -900) { Serial.println("[Weather] no temperature"); return false; }
  r.temp = temp;
  r.humidity  = extractFloatFrom(body, "relative_humidity_2m", cur);
  r.feels     = extractFloatFrom(body, "apparent_temperature", cur);
  r.wcode     = extractFloatFrom(body, "weather_code", cur);
  r.precip    = extractFloatFrom(body, "precipitation", cur);
  r.cloud     = extractFloatFrom(body, "cloud_cover", cur);
  r.pressMsl  = extractFloatFrom(body, "pressure_msl", cur);
  r.pressSurf = extractFloatFrom(body, "surface_pressure", cur);
  r.wind      = extractFloatFrom(body, "wind_speed_10m", cur);
  r.windDir   = extractFloatFrom(body, "wind_direction_10m", cur);
  r.gust      = extractFloatFrom(body, "wind_gusts_10m", cur);
  r.elevation = extractFloatFrom(body, "elevation", 0);
  r.isDay     = extractFloatFrom(body, "is_day", cur);

  /* 更新时间：current.time = "2026-09-25T02:45" -> 取 T 后面 5 位 */
  {
    String t = extractStr(body.substring(cur), "time");
    if (t.length() >= 16) {
      snprintf(r.curTime, sizeof(r.curTime), "%.5s", t.c_str() + 11);
    } else {
      r.curTime[0] = '\0';
    }
  }

  /* ── daily 数组 ── */
  int day = body.indexOf("\"daily\":{");
  r.dayCount = 0;
  if (day >= 0) {
    char buf[16];
    for (int i = 0; i < DAY_N; i++) {
      DayFc& d = r.days[i];
      d.week = -1;
      if (!arrStr(body, day, "time", i, buf, sizeof(buf))) break;
      /* "2026-09-25" -> md="09-25"，星期从年月日算 */
      if (strlen(buf) >= 10) {
        snprintf(d.md, sizeof(d.md), "%.5s", buf + 5);
        int y = atoi(buf), m = atoi(buf + 5), dd = atoi(buf + 8);
        d.week = weekdayOf(y, m, dd);
      } else {
        snprintf(d.md, sizeof(d.md), "%s", buf);
      }
      float v;
      d.code    = arrNum(body, day, "weather_code", i, v) ? (int)v : 0;
      d.tmax    = arrNum(body, day, "temperature_2m_max", i, v) ? v : 0;
      d.tmin    = arrNum(body, day, "temperature_2m_min", i, v) ? v : 0;
      d.tappMax = arrNum(body, day, "apparent_temperature_max", i, v) ? v : 0;
      d.tappMin = arrNum(body, day, "apparent_temperature_min", i, v) ? v : 0;
      d.precip  = arrNum(body, day, "precipitation_sum", i, v) ? v : 0;
      d.pop     = arrNum(body, day, "precipitation_probability_max", i, v) ? v : 0;
      d.wind    = arrNum(body, day, "wind_speed_10m_max", i, v) ? v : 0;
      d.gust    = arrNum(body, day, "wind_gusts_10m_max", i, v) ? v : 0;
      d.uv      = arrNum(body, day, "uv_index_max", i, v) ? v : 0;
      /* sunrise/sunset = "2026-09-25T05:54" -> 取 T 后 5 位 */
      if (arrStr(body, day, "sunrise", i, buf, sizeof(buf)) && strlen(buf) >= 16)
        snprintf(d.sunrise, sizeof(d.sunrise), "%.5s", buf + 11);
      else snprintf(d.sunrise, sizeof(d.sunrise), "--:--");
      if (arrStr(body, day, "sunset", i, buf, sizeof(buf)) && strlen(buf) >= 16)
        snprintf(d.sunset, sizeof(d.sunset), "%.5s", buf + 11);
      else snprintf(d.sunset, sizeof(d.sunset), "--:--");
      r.dayCount++;
    }
  }
  Serial.printf("[Weather] parsed %d daily rows\n", r.dayCount);

  /* ── hourly 数组（今日 0~23 点）── */
  int hr = body.indexOf("\"hourly\":{");
  r.hourCount = 0;
  if (hr >= 0) {
    char hb[24];
    for (int i = 0; i < HOUR_N; i++) {
      HourFc& h = r.hours[i];
      if (!arrStr(body, hr, "time", i, hb, sizeof(hb))) break;
      /* "2026-09-25T14:00" -> "14" */
      if (strlen(hb) >= 13) snprintf(h.hh, sizeof(h.hh), "%.2s", hb + 11);
      else snprintf(h.hh, sizeof(h.hh), "--");
      float v;
      h.temp = arrNum(body, hr, "temperature_2m", i, v) ? v : 0;
      h.code = arrNum(body, hr, "weather_code", i, v) ? (int)v : 0;
      h.pop  = arrNum(body, hr, "precipitation_probability", i, v) ? v : 0;
      r.hourCount++;
    }
  }
  Serial.printf("[Weather] parsed %d hourly rows\n", r.hourCount);

  /* 整块 JSON 落到 SD：下次开机还没联网就能先显示一份。
     ⚠️ SD 是懒挂载的（没敲 `sd` 就没 mount），没挂就静默跳过 ——
        缓存只是加速，不是功能，别因为它失败就把整个抓取判失败。 */
  wxCacheSave(body);

  r.ok = true;
  return true;
}

/* 只抓数据，不碰任何 LVGL 对象 */
static bool fetchOnce(WeatherResult& r) {
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
  Serial.printf("[Weather] %u B\n", (unsigned)body.length());

  /* 解析抽成 parseWeatherJson：SD 缓存读回来的 JSON 走同一套代码 */
  if (!parseWeatherJson(body, r)) return false;

  wxCacheSave(body);

  return true;
}

/* SD 读写。返回 false 一律静默：没插卡 / 没挂载都属正常。 */
static bool wxCacheSave(const String& body) {
  if (!SDCard::mounted()) return false;
  if (!SDCard::writeFile(WX_CACHE_PATH, body)) {
    Serial.println("[Weather] sd cache write failed (ignored)");
    return false;
  }
  Serial.printf("[Weather] sd cache saved %u B\n", (unsigned)body.length());
  return true;
}

/* 开机 / 进屏时先读一次缓存，让屏幕立刻有内容 */
static bool wxCacheLoad(String& out) {
  if (!SDCard::mounted()) return false;
  if (!SDCard::readFile(WX_CACHE_PATH, out) || out.length() < 200) return false;
  Serial.printf("[Weather] sd cache loaded %u B\n", (unsigned)out.length());
  return true;
}

/* 常驻任务：等通知 -> 定位 + 抓数据 -> 置 g_done。全程不碰 LVGL。 */
static void weatherTask(void* param) {
  (void)param;
  for (;;) {
    /* 等通知；开着自动更新就最多等 1 小时（超时 = 到点自动拉一次）。
       ⚠️ 关掉开关必须退化成 portMAX_DELAY：靠"醒了再判断开关"会每小时
          唤醒一次，那不叫停止。 */
    uint32_t got = ulTaskNotifyTake(pdTRUE,
                                    g_auto ? pdMS_TO_TICKS(AUTO_MS)
                                           : portMAX_DELAY);
    bool autoTick = (got == 0);
    if (autoTick && !g_auto) continue;      /* 开关刚被关掉，接着睡 */
    if (autoTick) Serial.println("[Weather] auto tick (1h)");
    g_busy = true;

    /* do-while 而不是再发一个 notify：notify 是计数的，连发两次会让任务
       连着跑两轮 HTTPS（进屏自动拉 + 串口 weather 撞一起就是这么来的）。
       现在改成"这轮跑完发现有新请求就补一轮"。 */
    do {
    g_again = false;
    WeatherResult r;
    memset(&r, 0, sizeof(r));
    r.windDir = -1;
    r.dayCount = 0;

    /* 先定位（有缓存就直接用，不会联网） */
    if (g_wantLocate || !GeoIP::valid()) {
      GeoIP::locate(g_wantLocate);
      g_wantLocate = false;
    }
    snprintf(r.city, sizeof(r.city), "%s", GeoIP::city());

    /* DNS 在这个网络里会偶发失败，失败重试一轮。
       ⚠️ 任务里用 vTaskDelay，不是 delay() —— 让出 CPU 给别的任务。 */
    if (WiFi.status() == WL_CONNECTED) {
      for (int attempt = 0; attempt < 2; attempt++) {
        if (fetchOnce(r)) break;
        vTaskDelay(pdMS_TO_TICKS(300));
      }
    }
    g_res = r;
    g_haveRes = r.ok;   /* 失败不留缓存：下次进屏要真的重试，不是把"失败"贴上去 */
    g_done = true;
    } while (g_again);
    g_busy = false;
  }
}

/* 触发一次抓取（不阻塞：通知任务后就返回） */
static void weatherStart(bool forceLocate) {
  if (forceLocate) g_wantLocate = true;
  if (!g_task) {
    /* 栈 8KB：HTTPClient + mbedTLS 握手实测远小于此（浏览器那边峰值 4.6KB）。
       ⚠️ 任务栈只能从 DRAM 分配，所以**只建一次、永不删除**（常驻复用）。 */
    BaseType_t ret = xTaskCreatePinnedToCore(weatherTask, "weather", 8192,
                                             NULL, 4, &g_task, 0);
    if (ret != pdPASS) {
      g_task = nullptr;
      Serial.printf("[Weather] task create failed, DRAM free=%u\n",
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
      if (g_statusLab) lv_label_set_text(g_statusLab, "内存不足");
      return;
    }
  }
  /* 正在拉 -> 只记一笔"再拉一次"，别再发 notify（会连跑两轮 HTTPS） */
  if (g_busy) {
    g_again = true;
    return;
  }
  /* 刚拉过（10s 内）就不再联网：来回切屏不该每次都打一次 HTTPS。
     但有缓存结果就直接贴上去，屏幕不会是空的。 */
  if (!forceLocate && g_haveRes && g_lastFetch &&
      millis() - g_lastFetch < 10000) {
    if (g_statusLab) applyResult(g_res);
    return;
  }
  g_done = false;
  g_lastFetch = millis();
  if (g_statusLab) lv_label_set_text(g_statusLab, "获取中...");
  xTaskNotifyGive(g_task);
}

/* 温度 -> 冷暖色（低=冷蓝 0x24405E，高=暖红 0x6E2F2A）。
   用在逐时格子的底色上：一眼看出哪几个小时最热。 */
static uint32_t tempColor(float t, float lo, float hi) {
  float k = (hi > lo + 0.01f) ? (t - lo) / (hi - lo) : 0.5f;
  if (k < 0) k = 0;
  if (k > 1) k = 1;
  uint8_t r = 0x24 + (uint8_t)((0x6E - 0x24) * k);
  uint8_t g = 0x40 + (uint8_t)((0x2F - 0x40) * k);
  uint8_t b = 0x5E + (uint8_t)((0x2A - 0x5E) * k);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* ── UI 小工具 ── */
static lv_obj_t* mkLabel(lv_obj_t* parent, const lv_font_t* font,
                  uint32_t color, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text ? text : "");
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(l, font, 0);
  return l;
}

static lv_obj_t* mkSectionTitle(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = mkLabel(parent, &font_zh_16, 0x888888, text);
  lv_obj_set_width(l, lv_pct(100));
  return l;
}

/* 「当前实况」网格的两列布局：一行放两格，每格 = 名字(灰) + 值(白) */
static lv_obj_t* mkCell(lv_obj_t* parent, const char* name) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_set_size(box, 214, 38);
  /* 卡片化：微微的底色 + 细边框 + 圆角。以前是纯透明，一屏灰字确实敷衍。 */
  lv_obj_set_style_bg_color(box, lv_color_hex(0x151515), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0x2A2A2A), 0);
  lv_obj_set_style_radius(box, 3, 0);
  lv_obj_set_style_pad_left(box, 10, 0);
  lv_obj_set_style_pad_right(box, 8, 0);
  lv_obj_set_style_pad_top(box, 0, 0);
  lv_obj_set_style_pad_bottom(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(box, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t* n = mkLabel(box, &font_zh_16, 0x777777, name);
  lv_obj_set_width(n, 78);
  lv_obj_t* v = mkLabel(box, &lv_font_montserrat_16, 0xDDDDDD, "--");
  lv_obj_set_flex_grow(v, 1);
  return v;
}

/* tick 里把结果贴到界面上（UI 线程，可以碰 LVGL） */
static void applyResult(const WeatherResult& r) {
  char buf[64];

  /* 城市**总是**跟着更新：定位可能成功而天气失败，
     城市不该跟着天气一起卡住。 */
  if (g_locLab) {
    lv_label_set_text(g_locLab, r.city);
    Serial.printf("[Weather] location label now = '%s'\n",
                  lv_label_get_text(g_locLab));
  }

  if (!r.ok) {
    if (g_statusLab)
      lv_label_set_text(g_statusLab, WiFi.status() == WL_CONNECTED
                                         ? "天气获取失败" : "未连接WiFi");
    if (g_updLab) lv_label_set_text(g_updLab, "");
    Serial.println("[Weather] FAILED (see lines above for HTTP code)");
    return;
  }

  snprintf(buf, sizeof(buf), "%.0f°C", r.temp);
  if (g_tempLab) lv_label_set_text(g_tempLab, buf);
  if (g_descLab) lv_label_set_text(g_descLab, wmoDesc((int)r.wcode));
  snprintf(buf, sizeof(buf), "体感 %.0f° · 湿度 %.0f%%", r.feels, r.humidity);
  if (g_subLab) lv_label_set_text(g_subLab, buf);
  if (g_updLab) {
    if (r.curTime[0]) { snprintf(buf, sizeof(buf), "更新 %s", r.curTime); lv_label_set_text(g_updLab, buf); }
    else lv_label_set_text(g_updLab, "");
  }
  if (g_statusLab) lv_label_set_text(g_statusLab, "下拉看 7 日预报");

  /* ── 当前实况网格 ── */
  const DayFc& d0 = r.days[0];
  snprintf(buf, sizeof(buf), "%.0f°", r.feels);        if (g_cellVal[0]) lv_label_set_text(g_cellVal[0], buf);
  snprintf(buf, sizeof(buf), "%.0f%%", r.humidity);    if (g_cellVal[1]) lv_label_set_text(g_cellVal[1], buf);
  snprintf(buf, sizeof(buf), "%.1f mm", r.precip);     if (g_cellVal[2]) lv_label_set_text(g_cellVal[2], buf);
  snprintf(buf, sizeof(buf), "%.0f%%", r.cloud);       if (g_cellVal[3]) lv_label_set_text(g_cellVal[3], buf);
  snprintf(buf, sizeof(buf), "%.0f hPa", r.pressMsl);  if (g_cellVal[4]) lv_label_set_text(g_cellVal[4], buf);
  snprintf(buf, sizeof(buf), "%.0f m", r.elevation);   if (g_cellVal[5]) lv_label_set_text(g_cellVal[5], buf);
  snprintf(buf, sizeof(buf), "%.1f km/h", r.wind);     if (g_cellVal[6]) lv_label_set_text(g_cellVal[6], buf);
  snprintf(buf, sizeof(buf), "%.1f km/h", r.gust);     if (g_cellVal[7]) lv_label_set_text(g_cellVal[7], buf);
  if (g_cellVal[8]) lv_label_set_text(g_cellVal[8], windDirName(r.windDir));
  if (r.dayCount > 0) {
    snprintf(buf, sizeof(buf), "%.0f°", d0.tmax);      if (g_cellVal[9]) lv_label_set_text(g_cellVal[9], buf);
    snprintf(buf, sizeof(buf), "%.0f°", d0.tmin);      if (g_cellVal[10]) lv_label_set_text(g_cellVal[10], buf);
    if (g_cellVal[11]) lv_label_set_text(g_cellVal[11], d0.sunrise);
    if (g_cellVal[12]) lv_label_set_text(g_cellVal[12], d0.sunset);
    snprintf(buf, sizeof(buf), "%.1f", d0.uv);         if (g_cellVal[13]) lv_label_set_text(g_cellVal[13], buf);
  }
  snprintf(buf, sizeof(buf), "%.0f hPa", r.pressSurf); if (g_cellVal[14]) lv_label_set_text(g_cellVal[14], buf);
  if (g_cellVal[15]) lv_label_set_text(g_cellVal[15], r.isDay > 0.5f ? "白天" : "夜间");

  /* ── 今日逐时：先算这 24 小时的温度范围，再按冷暖给每格上底色 ── */
  float hLo = 999, hHi = -999;
  for (int i = 0; i < r.hourCount; i++) {
    if (r.hours[i].temp < hLo) hLo = r.hours[i].temp;
    if (r.hours[i].temp > hHi) hHi = r.hours[i].temp;
  }
  for (int i = 0; i < HOUR_N; i++) {
    if (!g_hourLab[i]) continue;
    if (i >= r.hourCount) {
      lv_label_set_text(g_hourLab[i], "");
      if (g_hourCard[i]) lv_obj_set_style_bg_opa(g_hourCard[i], LV_OPA_TRANSP, 0);
      continue;
    }
    const HourFc& h = r.hours[i];
    snprintf(buf, sizeof(buf), "%s\n%.0f°\n%.0f%%", h.hh, h.temp, h.pop);
    lv_label_set_text(g_hourLab[i], buf);
    if (g_hourCard[i]) {
      lv_obj_set_style_bg_color(g_hourCard[i],
                                lv_color_hex(tempColor(h.temp, hLo, hHi)), 0);
      lv_obj_set_style_bg_opa(g_hourCard[i], LV_OPA_COVER, 0);
    }
  }

  /* ── 近 7 日 ── */
  float wLo = 999, wHi = -999;
  for (int i = 0; i < r.dayCount; i++) {
    if (r.days[i].tmin < wLo) wLo = r.days[i].tmin;
    if (r.days[i].tmax > wHi) wHi = r.days[i].tmax;
  }
  for (int i = 0; i < DAY_N; i++) {
    if (i >= r.dayCount) {
      if (g_dayDate[i]) lv_label_set_text(g_dayDate[i], "");
      if (g_dayDesc[i]) lv_label_set_text(g_dayDesc[i], "");
      if (g_dayTmp[i]) lv_label_set_text(g_dayTmp[i], "");
      if (g_dayExtra[i]) lv_label_set_text(g_dayExtra[i], "");
      continue;
    }
    const DayFc& d = r.days[i];
    /* 前两天说"今天/明天"，之后说星期 */
    if (g_dayDate[i]) {
      if (i == 0) lv_label_set_text(g_dayDate[i], "今天");
      else if (i == 1) lv_label_set_text(g_dayDate[i], "明天");
      else {
        char t[24];
        snprintf(t, sizeof(t), "%s %s", d.md,
                 (d.week >= 0 && d.week < 7) ? kWeekName[d.week] : "");
        lv_label_set_text(g_dayDate[i], t);
      }
    }
    if (g_dayDesc[i]) lv_label_set_text(g_dayDesc[i], wmoDesc(d.code));
    if (g_dayTmp[i]) {
      snprintf(buf, sizeof(buf), "%.0f° / %.0f°", d.tmax, d.tmin);
      lv_label_set_text(g_dayTmp[i], buf);
    }
    /* 温度条：按**本周**的 min~max 归一化，条里的亮块是当天的 min~max */
    if (g_dayBar[i] && g_dayBarBg[i] && wHi > wLo) {
      const int W = 110;
      float span = wHi - wLo;
      int x = (int)((d.tmin - wLo) / span * (float)W);
      int w = (int)((d.tmax - d.tmin) / span * (float)W);
      if (x < 0) x = 0;
      if (x > W - 4) x = W - 4;
      if (w < 6) w = 6;
      if (x + w > W) w = W - x;
      lv_obj_set_pos(g_dayBar[i], x, 0);
      lv_obj_set_size(g_dayBar[i], w, 8);
      /* 当天越热，条越偏暖色 */
      lv_obj_set_style_bg_color(g_dayBar[i],
                                lv_color_hex(tempColor(d.tmax, wLo, wHi)), 0);
    }
    if (g_dayExtra[i]) {
      snprintf(buf, sizeof(buf),
               "降水 %.0f%% · %.1fmm · 风 %.0fkm/h · 紫外 %.1f · 日出 %s 日落 %s",
               d.pop, d.precip, d.wind, d.uv, d.sunrise, d.sunset);
      lv_label_set_text(g_dayExtra[i], buf);
    }
  }
}


/* 🔒 屏被销毁时把全局指针还清 —— 屏销毁后它们全是悬空指针，
   只要 tick 里还往里写一次就崩（跟 clock 屏那只忘记注销的 lv_timer 同类）。 */
void scr_delete_cb(lv_event_t* e) {
  (void)e;
  g_locLab = nullptr;
  g_updLab = nullptr;
  g_tempLab = nullptr;
  g_descLab = nullptr;
  g_subLab = nullptr;
  g_statusLab = nullptr;
  g_refreshBtn = nullptr;
  g_scroll = nullptr;
  for (int i = 0; i < CELL_N; i++) g_cellVal[i] = nullptr;
  for (int i = 0; i < DAY_N; i++) {
    g_dayDate[i] = nullptr;
    g_dayDesc[i] = nullptr;
    g_dayTmp[i] = nullptr;
    g_dayExtra[i] = nullptr;
  }
  for (int i = 0; i < HOUR_N; i++) {
    g_hourLab[i] = nullptr;
    g_hourCard[i] = nullptr;
  }
  for (int i = 0; i < DAY_N; i++) {
    g_dayBar[i] = nullptr;
    g_dayBarBg[i] = nullptr;
  }
  g_wxIcon = nullptr;
  /* ⚠️ 后台任务**不删**：常驻复用。它只写 g_res/g_done，不会碰悬空指针。 */
  g_done = false;
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

  /* ── 上半屏（固定）──
     重排：左边一个天气图标，右边城市 / 大温度 / 描述竖着排成一列，
     不再全都居中叠在一起（2026-09-25 改，原来确实有点敷衍）。 */
  g_wxIcon = icon_create(scr, Icon::Weather, 60);
  if (g_wxIcon) lv_obj_align(g_wxIcon, LV_ALIGN_TOP_LEFT, 22, 44);

  g_locLab = mkLabel(scr, &font_zh_16, 0x888888, GeoIP::city());
  lv_obj_align(g_locLab, LV_ALIGN_TOP_LEFT, 96, 40);

  g_updLab = mkLabel(scr, &lv_font_montserrat_14, 0x666666, "");
  lv_obj_align(g_updLab, LV_ALIGN_TOP_RIGHT, -12, 42);

  g_tempLab = mkLabel(scr, &lv_font_montserrat_48, 0xFFFFFF, "--°C");
  lv_obj_align(g_tempLab, LV_ALIGN_TOP_LEFT, 94, 60);

  g_descLab = mkLabel(scr, &font_zh_24, 0xCCCCCC, "--");
  lv_obj_align(g_descLab, LV_ALIGN_TOP_LEFT, 96, 118);

  g_subLab = mkLabel(scr, &font_zh_16, 0x888888, "");
  lv_obj_align(g_subLab, LV_ALIGN_TOP_LEFT, 96, 148);

  /* ── 下半屏（可下滑）──
     ⚠️ 滚动条必须 OFF：默认 AUTO 会在暗色 UI 上画一条浅色滚动条
     （就是浏览器那条"白线"，本项目踩过）。 */
  g_scroll = lv_obj_create(scr);
  lv_obj_set_size(g_scroll, 456, 254);
  lv_obj_set_pos(g_scroll, 12, 172);
  lv_obj_set_style_bg_opa(g_scroll, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_scroll, 0, 0);
  lv_obj_set_style_pad_all(g_scroll, 0, 0);
  lv_obj_set_style_pad_gap(g_scroll, 0, 0);
  lv_obj_set_flex_flow(g_scroll, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(g_scroll, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(g_scroll, LV_OBJ_FLAG_EVENT_BUBBLE);

  /* 段 1：当前实况（两列网格，7 行） */
  mkSectionTitle(g_scroll, "当前实况");
  {
    lv_obj_t* grid = lv_obj_create(g_scroll);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 2, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_EVENT_BUBBLE);
    for (int i = 0; i < CELL_N; i++) g_cellVal[i] = mkCell(grid, kCellNames[i]);
  }

  /* 段 2：今日逐时
     ⚠️ 用 ROW_WRAP 排成 8 列 × 3 行，而不是横向滚动：LVGL 8.3 没有
        lv_obj_set_scroll_dir()，嵌套一个横向滚容器会把父容器的竖滑吃掉
        （7 日列表就滑不动了）。换行后整体跟着页面竖滑，反而更好操作。 */
  mkSectionTitle(g_scroll, "未来 24 小时");
  {
    lv_obj_t* hr = lv_obj_create(g_scroll);
    lv_obj_set_width(hr, lv_pct(100));
    lv_obj_set_height(hr, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hr, 0, 0);
    lv_obj_set_style_pad_all(hr, 0, 0);
    lv_obj_set_style_pad_gap(hr, 0, 0);
    lv_obj_clear_flag(hr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hr, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(hr, 2, 0);
    lv_obj_set_style_pad_column(hr, 4, 0);
    lv_obj_add_flag(hr, LV_OBJ_FLAG_EVENT_BUBBLE);
    for (int i = 0; i < HOUR_N; i++) {
      lv_obj_t* card = lv_obj_create(hr);
      lv_obj_set_size(card, 52, 68);
      g_hourCard[i] = card;
      lv_obj_set_style_bg_color(card, lv_color_hex(0x24405E), 0);
      lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
      /* ⚠️ 圆角要跑抗锯齿：24 个格子每帧算一遍，实测把天气页拖到 88ms/帧。
         改成直角填充，观感几乎不变，渲染直接回到 ~30ms。 */
      lv_obj_set_style_radius(card, 0, 0);
      lv_obj_set_style_border_width(card, 0, 0);
      lv_obj_set_style_pad_all(card, 0, 0);
      lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE);
      g_hourLab[i] = mkLabel(card, &font_zh_16, 0xCCCCCC, "--");
      lv_obj_set_width(g_hourLab[i], 52);
      lv_obj_align(g_hourLab[i], LV_ALIGN_TOP_MID, 0, 0);
      lv_obj_set_style_text_align(g_hourLab[i], LV_TEXT_ALIGN_CENTER, 0);
    }
  }

  /* 段 3：近 7 日 */
  mkSectionTitle(g_scroll, "近 7 日预报");
  for (int i = 0; i < DAY_N; i++) {
    lv_obj_t* row = lv_obj_create(g_scroll);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_top(row, 6, 0);
    lv_obj_set_style_pad_bottom(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* line1 = lv_obj_create(row);
    lv_obj_set_width(line1, lv_pct(100));
    lv_obj_set_height(line1, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(line1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(line1, 0, 0);
    lv_obj_set_style_pad_all(line1, 0, 0);
    lv_obj_clear_flag(line1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(line1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(line1, LV_OBJ_FLAG_EVENT_BUBBLE);

    g_dayDate[i] = mkLabel(line1, &font_zh_16, 0xDDDDDD, "");
    lv_obj_set_width(g_dayDate[i], 96);
    g_dayDesc[i] = mkLabel(line1, &font_zh_16, 0xAAAAAA, "");
    lv_obj_set_flex_grow(g_dayDesc[i], 1);

    /* 温度条：底槽 = 本周最低~最高的整段，里面的亮块 = 当天 min~max。
       一眼能看出哪天最热 —— 比一行干巴巴的数字强多了。
       ⚠️ 底槽**不能**是 flex 容器：区间块要用绝对坐标定位。 */
    g_dayBarBg[i] = lv_obj_create(line1);
    lv_obj_set_size(g_dayBarBg[i], 110, 8);
    lv_obj_set_style_bg_color(g_dayBarBg[i], lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(g_dayBarBg[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dayBarBg[i], 0, 0);
    lv_obj_set_style_radius(g_dayBarBg[i], 2, 0);
    lv_obj_clear_flag(g_dayBarBg[i], LV_OBJ_FLAG_SCROLLABLE);
    g_dayBar[i] = lv_obj_create(g_dayBarBg[i]);
    lv_obj_set_size(g_dayBar[i], 20, 8);
    lv_obj_set_pos(g_dayBar[i], 0, 0);
    lv_obj_set_style_bg_color(g_dayBar[i], lv_color_hex(0x5B9BD5), 0);
    lv_obj_set_style_bg_opa(g_dayBar[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dayBar[i], 0, 0);
    lv_obj_set_style_radius(g_dayBar[i], 2, 0);
    lv_obj_clear_flag(g_dayBar[i], LV_OBJ_FLAG_SCROLLABLE);

    g_dayTmp[i] = mkLabel(line1, &lv_font_montserrat_16, 0xFFFFFF, "");
    lv_obj_set_width(g_dayTmp[i], 74);
    lv_obj_set_style_text_align(g_dayTmp[i], LV_TEXT_ALIGN_RIGHT, 0);

    g_dayExtra[i] = mkLabel(row, &lv_font_montserrat_14, 0x666666, "");
    lv_obj_set_width(g_dayExtra[i], lv_pct(100));
  }

  /* ── 底部状态 + 刷新 ── */
  g_statusLab = mkLabel(scr, &font_zh_16, 0x666666,
                        WiFi.status() == WL_CONNECTED ? "就绪" : "未连接WiFi");
  lv_obj_align(g_statusLab, LV_ALIGN_BOTTOM_LEFT, 12, -14);

  g_refreshBtn = lv_btn_create(scr);
  lv_obj_set_size(g_refreshBtn, 80, 36);
  lv_obj_align(g_refreshBtn, LV_ALIGN_BOTTOM_RIGHT, -12, -14);
  lv_obj_set_style_bg_opa(g_refreshBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(g_refreshBtn, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(g_refreshBtn, lv_color_hex(0x161616), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(g_refreshBtn, lv_color_white(), 0);
  lv_obj_set_style_border_width(g_refreshBtn, 1, 0);
  lv_obj_set_style_radius(g_refreshBtn, 8, 0);
  lv_obj_add_event_cb(g_refreshBtn, refresh_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(g_refreshBtn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t* rl = mkLabel(g_refreshBtn, &font_zh_16, 0xFFFFFF, "刷新");
  lv_obj_center(rl);

  /* 进屏就自动拉一次（后台任务，不卡 UI）。有上次的结果先贴上，屏幕不是空的。 */
  if (g_haveRes) applyResult(g_res);
  else {
    /* 内存里没有（刚开机）-> 试试 SD 上的 JSON 缓存。
       有就当场解析贴上，屏幕上立刻有内容，不用干等 1~2 秒的网络。 */
    String cached;
    if (wxCacheLoad(cached)) {
      WeatherResult r;
      memset(&r, 0, sizeof(r));
      r.windDir = -1;
      if (parseWeatherJson(cached, r)) {
        g_res = r;
        g_haveRes = true;
        applyResult(r);
        if (g_statusLab) lv_label_set_text(g_statusLab, "来自SD缓存，正在更新");
      }
    }
  }
  weatherStart(false);

  return scr;
}

void WeatherScreen_tick() {
  /* 后台任务跑完了 -> 在 UI 线程贴结果。
     ⚠️ 任务里只写 g_res/g_done，绝不碰 LVGL。 */
  if (g_done) {
    g_done = false;
    if (g_statusLab) applyResult(g_res);
  }
}

/* 串口入口：weather —— 直接拉一次并打印诊断，不用点屏幕。
   ⚠️ 必须在匿名 namespace 之外：namespace 里的函数是内部链接，
      serial_console.cpp 那边链接不到（踩过：undefined reference）。 */
/* 后台自动更新的开关（设置页 / 串口用）。关掉 = 完全停止。 */
void WeatherScreen_setAuto(bool on) {
  g_auto = on;
  Serial.printf("[Weather] auto refresh %s\n", on ? "on" : "off");
}
bool WeatherScreen_auto() { return g_auto; }

bool WeatherScreen_fetchNow(const char* adcode) {
  if (!g_statusLab) {
    Serial.println("[Weather] screen not created yet");
    return false;
  }
  (void)adcode;   /* 坐标来自 IP 定位（GeoIP），不再用 adcode */
  weatherStart(true);
  return true;
}
