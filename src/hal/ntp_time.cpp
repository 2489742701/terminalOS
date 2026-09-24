#include "ntp_time.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>
#include <time.h>

namespace NtpTime {

namespace {

/* 国内优先：aliyun 的 NTP 在大陆延迟最低，pool.ntp.org 兜底。 */
const char* kServers[] = {"ntp.aliyun.com", "pool.ntp.org", "time.google.com"};
const int kServerCount = (int)(sizeof(kServers) / sizeof(kServers[0]));

const int kNtpPacketSize = 48;
const uint16_t kNtpPort = 123;
const uint32_t kSecs1900To1970 = 2208988800UL;
const uint32_t kSyncIntervalMs = 6UL * 3600UL * 1000UL;   /* 6 小时重对一次 */

TaskHandle_t s_task = nullptr;
volatile bool s_auto = true;
volatile bool s_request = false;
volatile bool s_synced = false;
volatile uint32_t s_lastSyncMs = 0;      /* millis() 时基 */
volatile uint32_t s_lastSyncUnix = 0;

WiFiUDP s_udp;

/* 单发一次 NTP 请求。成功返回 true 并把 UNIX 秒写进 outUnix。
   阻塞，最长 timeoutMs —— 只在后台任务里调用。 */
bool ntpQuery(const char* host, uint32_t timeoutMs, uint32_t& outUnix) {
  IPAddress ip;
  if (!WiFi.hostByName(host, ip)) {
    Serial.printf("[NTP] hostByName failed: %s\n", host);
    return false;
  }

  uint8_t buf[kNtpPacketSize];
  memset(buf, 0, sizeof(buf));
  /* LI=3(未同步) VN=4 Mode=3(客户端) → 0b11100011 */
  buf[0] = 0xE3;
  buf[1] = 0;      /* stratum */
  buf[2] = 6;      /* poll */
  buf[3] = 0xEC;   /* precision */
  buf[12] = 49;
  buf[13] = 0x4E;
  buf[14] = 49;
  buf[15] = 52;

  s_udp.stop();
  if (!s_udp.begin(2390)) {
    Serial.println("[NTP] udp begin failed");
    return false;
  }
  s_udp.beginPacket(ip, kNtpPort);
  s_udp.write(buf, kNtpPacketSize);
  if (s_udp.endPacket() != 1) {
    s_udp.stop();
    return false;
  }

  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    int cb = s_udp.parsePacket();
    if (cb >= kNtpPacketSize) {
      s_udp.read(buf, kNtpPacketSize);
      s_udp.stop();
      uint32_t secs1900 = ((uint32_t)buf[40] << 24) | ((uint32_t)buf[41] << 16) |
                          ((uint32_t)buf[42] << 8) | (uint32_t)buf[43];
      if (secs1900 == 0) return false;
      outUnix = secs1900 - kSecs1900To1970;
      return true;
    }
    delay(20);
  }
  s_udp.stop();
  Serial.printf("[NTP] timeout: %s\n", host);
  return false;
}

bool doSync() {
  uint32_t unixSec = 0;
  bool ok = false;
  for (int i = 0; i < kServerCount && !ok; i++) {
    ok = ntpQuery(kServers[i], 3000, unixSec);
    if (ok) {
      Serial.printf("[NTP] ok via %s -> %u\n", kServers[i], (unsigned)unixSec);
    }
  }
  if (!ok) return false;

  /* 东八区：POSIX TZ 串，CST-8 = UTC+8 */
  setenv("TZ", "CST-8", 1);
  tzset();

  struct timeval tv;
  tv.tv_sec = (time_t)unixSec;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);

  s_lastSyncUnix = unixSec;
  s_lastSyncMs = millis();
  s_synced = true;

  time_t now = (time_t)unixSec;
  struct tm tmv;
  localtime_r(&now, &tmv);
  Serial.printf("[NTP] clock set: %04d-%02d-%02d %02d:%02d:%02d\n",
                tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return true;
}

void ntpTask(void*) {
  for (;;) {
    /* 5s 一轮；被 notify 时立刻醒。 */
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));

    if (WiFi.status() != WL_CONNECTED) continue;

    bool need = s_request;
    if (!need && s_auto) {
      if (!s_synced) need = true;
      else if (millis() - s_lastSyncMs > kSyncIntervalMs) need = true;
    }
    if (!need) continue;

    s_request = false;
    doSync();
  }
}

}  // namespace

void begin() {
  if (s_task) return;
  /* 栈 4096：只跑 UDP + printf，不需要很大。任务栈来自动态内存，别给太大。 */
  xTaskCreate(ntpTask, "ntp", 4096, nullptr, 1, &s_task);
  Serial.println("[NTP] task created");
}

void requestSync() {
  s_request = true;
  if (s_task) xTaskNotifyGive(s_task);
}

void setAutoSync(bool on) { s_auto = on; }
bool autoSyncEnabled() { return s_auto; }

bool isSynced() { return s_synced; }
uint32_t lastSyncUnix() { return s_lastSyncUnix; }
uint32_t sinceSyncMs() { return s_synced ? (millis() - s_lastSyncMs) : 0xFFFFFFFFu; }

}  // namespace NtpTime
