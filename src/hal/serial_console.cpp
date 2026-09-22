#include "serial_console.h"

#if SERIAL_CONSOLE_ENABLED

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include "../app/nav.h"
#include "../app/browser_screen.h"

namespace SerialConsole {

static const int CMD_BUF_SIZE = 256;
static char s_buf[CMD_BUF_SIZE];
static int s_bufLen = 0;
static bool s_inited = false;

/* 屏幕名 → 屏幕指针 查找表 */
struct ScreenEntry {
  const char* name;
  lv_obj_t* scr;
};
static ScreenEntry s_screens[] = {
  {"launcher",  nullptr},  // 延迟填充，begin() 里赋值
  {"clock",     nullptr},
  {"settings",  nullptr},
  {"wifi",      nullptr},
  {"game",      nullptr},
  {"browser",   nullptr},
  {"draw",      nullptr},
  {"memory",    nullptr},
  {"sysinfo",   nullptr},
  {"weather",   nullptr},
};
static const int s_screenCount = sizeof(s_screens) / sizeof(s_screens[0]);

static void printHelp() {
  Serial.println("=== Serial Console Commands ===");
  Serial.println("help              - show this help");
  Serial.println("nav <screen>      - navigate to screen");
  Serial.println("  screens: launcher clock settings wifi game browser draw memory sysinfo weather");
  Serial.println("browser <url>     - open browser and load URL");
  Serial.println("wifi              - show WiFi status");
  Serial.println("mem               - show memory info (DRAM + PSRAM)");
  Serial.println("reboot            - restart device");
  Serial.println("version           - show version info");
  Serial.println("time              - show uptime");
  Serial.println("===============================");
}

static void cmdNav(const char* arg) {
  if (!arg || strlen(arg) == 0) {
    Serial.println("Usage: nav <screen>");
    Serial.println("  screens: launcher clock settings wifi game browser draw memory sysinfo weather");
    return;
  }
  for (int i = 0; i < s_screenCount; i++) {
    if (strcmp(arg, s_screens[i].name) == 0) {
      if (s_screens[i].scr) {
        nav_go_anim(s_screens[i].scr, LV_SCR_LOAD_ANIM_FADE_ON, 300);
        Serial.printf("[Console] nav -> %s\n", arg);
      } else {
        Serial.printf("[Console] screen '%s' is null\n", arg);
      }
      return;
    }
  }
  Serial.printf("[Console] unknown screen: %s\n", arg);
}

static void cmdBrowser(const char* arg) {
  if (!arg || strlen(arg) == 0) {
    Serial.println("Usage: browser <url>");
    Serial.println("  example: browser http://info.cern.ch/");
    return;
  }
  /* 触发浏览器加载（已在浏览器屏则直接加载，不切屏避免 LVGL 状态崩溃） */
  if (nav_browser) {
    BrowserScreen_navigate(arg);
    Serial.printf("[Console] browser -> %s\n", arg);
  } else {
    Serial.println("[Console] browser screen is null");
  }
}

static void cmdWifi() {
  Serial.printf("[Console] WiFi status: %s\n",
    WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("  SSID: %s\n", WiFi.SSID().c_str());
  }
}

static void cmdMem() {
  /* 用 xPortGetFreeHeapSize() 而非 ESP.getFreeHeap()：
     ESP.getFreeHeap() → heap_caps_get_free_size() 会遍历所有堆并加锁，
     在 ESP32-S3 多堆（DRAM+PSRAM+DMA）环境下锁住 Core1 WiFi 任务导致看门狗超时。
     xPortGetFreeHeapSize() 只读 FreeRTOS 的一个字段，不锁堆。 */
  uint32_t freeHeap = xPortGetFreeHeapSize();
  yield();
  uint32_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  yield();
  Serial.printf("[Console] DRAM free: %u, PSRAM free: %u\n",
    (unsigned)freeHeap, (unsigned)freePsram);
}

static void cmdVersion() {
  Serial.println("=== Version Info ===");
  Serial.printf("  Chip: ESP32-S3 rev %d\n", ESP.getChipRevision());
  Serial.printf("  CPU freq: %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("  Flash: %u KB\n", ESP.getFlashChipSize() / 1024);
  Serial.printf("  SDK: %s\n", ESP.getSdkVersion());
  Serial.println("  App: geek-terminal v1.0");
  Serial.println("====================");
}

static void cmdTime() {
  Serial.printf("[Console] Uptime: %lu ms (%.1f s)\n",
    (unsigned long)millis(), (float)millis() / 1000.0f);
}

static void cmdReboot() {
  Serial.println("[Console] rebooting...");
  delay(100);
  ESP.restart();
}

/* 解析并执行一行命令 */
static void executeLine(char* line) {
  /* 去掉末尾的 \r */
  int len = strlen(line);
  while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) {
    line[--len] = 0;
  }
  if (len == 0) return;

  /* 拆分命令和参数（第一个空格） */
  char* cmd = line;
  char* arg = nullptr;
  char* sp = strchr(line, ' ');
  if (sp) {
    *sp = 0;
    arg = sp + 1;
    /* 跳过连续空格 */
    while (*arg == ' ') arg++;
  }

  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    printHelp();
  } else if (strcmp(cmd, "nav") == 0) {
    cmdNav(arg);
  } else if (strcmp(cmd, "browser") == 0) {
    cmdBrowser(arg);
  } else if (strcmp(cmd, "wifi") == 0) {
    cmdWifi();
  } else if (strcmp(cmd, "mem") == 0) {
    cmdMem();
  } else if (strcmp(cmd, "reboot") == 0) {
    cmdReboot();
  } else if (strcmp(cmd, "version") == 0) {
    cmdVersion();
  } else if (strcmp(cmd, "time") == 0) {
    cmdTime();
  } else {
    Serial.printf("[Console] unknown command: %s (type 'help')\n", cmd);
  }
}

void begin() {
  s_bufLen = 0;
  s_buf[0] = 0;
  s_inited = true;

  /* 填充屏幕指针（nav.h 里的全局变量在 app.cpp begin() 后才有效，
     但 SerialConsole::begin() 在 app.cpp begin() 末尾调用，所以此时已就绪） */
  s_screens[0].scr = nav_launcher;
  s_screens[1].scr = nav_clock;
  s_screens[2].scr = nav_settings;
  s_screens[3].scr = nav_wifi;
  s_screens[4].scr = nav_game;
  s_screens[5].scr = nav_browser;
  s_screens[6].scr = nav_draw;
  s_screens[7].scr = nav_memory;
  s_screens[8].scr = nav_sysinfo;
  s_screens[9].scr = nav_weather;

  Serial.println("\n[Console] Serial Console ready. Type 'help' for commands.");
}

void tick() {
  if (!s_inited) return;

  /* 逐字符读取串口数据，遇到换行符执行命令 */
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (s_bufLen > 0) {
        s_buf[s_bufLen] = 0;
        executeLine(s_buf);
        s_bufLen = 0;
        s_buf[0] = 0;
      }
    } else if (s_bufLen < CMD_BUF_SIZE - 1) {
      s_buf[s_bufLen++] = c;
    }
    /* 缓冲区满时自动执行，防止溢出 */
    if (s_bufLen >= CMD_BUF_SIZE - 1) {
      s_buf[s_bufLen] = 0;
      executeLine(s_buf);
      s_bufLen = 0;
      s_buf[0] = 0;
    }
  }
}

}  // namespace SerialConsole

#else  /* SERIAL_CONSOLE_ENABLED == 0 */

namespace SerialConsole {
void begin() {}
void tick() {}
}  // namespace SerialConsole

#endif