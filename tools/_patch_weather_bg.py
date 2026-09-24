# -*- coding: utf-8 -*-
"""天气：后台每小时自动更新（可关，关了就完全停）+ 结果缓存到 SD 卡。

master：
  · "天气预报：一直在后台运行，每过一个小时会获取一次天气。
     用户把它关掉，它就完全停止在那。"
  · "缓存也可以缓存到SD卡"

做法：
  1) 常驻任务改成「带超时地等通知」：
     ulTaskNotifyTake(pdTRUE, auto ? 1h : portMAX_DELAY)
     超时返回 0 = 到点了自动拉一次；被通知 = 手动拉。
     关掉开关就退化成 portMAX_DELAY —— 不占 CPU、不联网，真的"完全停止"。
  2) 抓到的原始 JSON 整块写 SD（/gt/weather.json）；开机/进屏时先读它，
     屏幕立刻有内容（显示"上次更新 xx:xx"），不用干等网络。
     SD 没插就优雅跳过，功能不受影响。
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\weather_screen.cpp"
src = io.open(P, encoding="utf-8").read()

# ── 1) include SD ─────────────────────────────────────────────────────────
old_inc = '#include "../hal/geoip.h"'
new_inc = '#include "../hal/geoip.h"\n#include "../hal/sd_card.h"'
assert src.count(old_inc) == 1
src = src.replace(old_inc, new_inc, 1)

# ── 2) 自动刷新开关 + SD 缓存路径 ─────────────────────────────────────────
old = """static SwipeState g_swipe;"""
new = """static SwipeState g_swipe;

/* ── 后台自动更新（master 2026-09-25）──────────────────────────────────────
 * 开关为 false 时任务用 portMAX_DELAY 等通知 —— 不轮询、不联网、不占 CPU，
 * 是真正的"完全停止"，不是"每小时醒来发现开关关了又睡"。 */
static volatile bool g_auto = true;
static const uint32_t AUTO_MS = 60u * 60u * 1000u;   /* 1 小时 */

/* SD 缓存：整块存原始 JSON，读回来直接复用现成的解析器，
   不用再写一套序列化。3KB 一次，对 SD 毫无压力。 */
#define WX_CACHE_PATH "/gt/weather.json\""""
assert src.count(old) == 1
src = src.replace(old, new, 1)

# ── 3) 任务改成带超时等待 ─────────────────────────────────────────────────
old2 = """  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    g_busy = true;"""
new2 = """  for (;;) {
    /* 等通知；开着自动更新就最多等 1 小时（超时 = 到点自动拉一次）。
       ⚠️ 关掉开关必须退化成 portMAX_DELAY：靠"醒了再判断开关"会每小时
          唤醒一次，那不叫停止。 */
    uint32_t got = ulTaskNotifyTake(pdTRUE,
                                    g_auto ? pdMS_TO_TICKS(AUTO_MS)
                                           : portMAX_DELAY);
    bool autoTick = (got == 0);
    if (autoTick && !g_auto) continue;      /* 开关刚被关掉，接着睡 */
    if (autoTick) Serial.println("[Weather] auto tick (1h)");
    g_busy = true;"""
assert src.count(old2) == 1
src = src.replace(old2, new2, 1)

# ── 4) 抓完写 SD；失败/未插卡静默跳过 ─────────────────────────────────────
old3 = """  Serial.printf("[Weather] parsed %d hourly rows\\n", r.hourCount);

  r.ok = true;
  return true;
}"""
new3 = """  Serial.printf("[Weather] parsed %d hourly rows\\n", r.hourCount);

  /* 整块 JSON 落到 SD：下次开机还没联网就能先显示一份。
     ⚠️ SD 是懒挂载的（没敲 `sd` 就没 mount），没挂就静默跳过 ——
        缓存只是加速，不是功能，别因为它失败就把整个抓取判失败。 */
  wxCacheSave(body);

  r.ok = true;
  return true;
}

/* SD 读写。返回 false 一律静默：没插卡 / 没挂载都属正常。 */
static bool wxCacheSave(const String& body) {
  if (!SDCard::isMounted()) return false;
  if (!SDCard::writeFile(WX_CACHE_PATH, body)) {
    Serial.println("[Weather] sd cache write failed (ignored)");
    return false;
  }
  Serial.printf("[Weather] sd cache saved %u B\\n", (unsigned)body.length());
  return true;
}

/* 开机 / 进屏时先读一次缓存，让屏幕立刻有内容 */
static bool wxCacheLoad(String& out) {
  if (!SDCard::isMounted()) return false;
  if (!SDCard::readFile(WX_CACHE_PATH, out) || out.length() < 200) return false;
  Serial.printf("[Weather] sd cache loaded %u B\\n", (unsigned)out.length());
  return true;
}"""
assert src.count(old3) == 1
src = src.replace(old3, new3, 1)

# wxCacheSave/Load 要在 fetchOnce 之前声明（fetchOnce 里调 wxCacheSave）
old4 = """/* 只抓数据，不碰任何 LVGL 对象 */"""
new4 = """static bool wxCacheSave(const String& body);
static bool wxCacheLoad(String& out);

/* 只抓数据，不碰任何 LVGL 对象 */"""
assert src.count(old4) == 1
src = src.replace(old4, new4, 1)

# ── 5) 外部开关接口 ───────────────────────────────────────────────────────
old5 = """bool WeatherScreen_fetchNow(const char* adcode) {"""
new5 = """/* 后台自动更新的开关（设置页 / 串口用）。关掉 = 完全停止。 */
void WeatherScreen_setAuto(bool on) {
  g_auto = on;
  Serial.printf("[Weather] auto refresh %s\\n", on ? "on" : "off");
}
bool WeatherScreen_auto() { return g_auto; }

bool WeatherScreen_fetchNow(const char* adcode) {"""
assert src.count(old5) == 1
src = src.replace(old5, new5, 1)

io.open(P, "w", encoding="utf-8", newline="").write(src)
print("weather bg ok")
