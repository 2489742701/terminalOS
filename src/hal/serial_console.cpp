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

/* 屏幕名 → 屏幕指针变量 查找表
   注意存的是"指针的指针"：Activity 改成懒创建后，屏幕对象在首次进入时才建立，
   nav_open() 会写回这个变量。若像以前那样在这里缓存 lv_obj_t* 快照，
   懒创建的屏永远是 nullptr，串口命令会误报 "screen is null"。 */
struct ScreenEntry {
  const char* name;
  lv_obj_t** scr;
};
static ScreenEntry s_screens[] = {
  {"launcher",  &nav_launcher},
  {"clock",     &nav_clock},
  {"settings",  &nav_settings},
  {"wifi",      &nav_wifi},
  {"game",      &nav_game},
  {"browser",   &nav_browser},
  {"draw",      &nav_draw},
  {"memory",    &nav_memory},
  {"sysinfo",   &nav_sysinfo},
  {"weather",   &nav_weather},
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
      /* 回主页 = 退出当前应用：销毁除 Launcher 外的所有 Activity */
      if (s_screens[i].scr == &nav_launcher) {
        nav_back_home();
        Serial.println("[Console] nav -> launcher (released others)");
        return;
      }
      /* 懒创建：未加载的屏在这里按需建立 */
      lv_obj_t* scr = nav_open(s_screens[i].scr);
      if (scr) {
        nav_go_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 300);
        Serial.printf("[Console] nav -> %s\n", arg);
      } else {
        Serial.printf("[Console] open '%s' failed\n", arg);
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
  /* 按需创建浏览器 Activity（懒创建，首次调用时才建屏） */
  lv_obj_t* scr = nav_open(&nav_browser);
  if (!scr) {
    Serial.println("[Console] open browser failed");
    return;
  }
  /* 不在浏览器屏就先切过去 */
  if (lv_scr_act() != scr) {
    nav_go_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 300);
    Serial.println("[Console] switching to browser screen");
  }
  BrowserScreen_navigate(arg);
  Serial.printf("[Console] browser -> %s\n", arg);
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
  /* 注意：xPortGetFreeHeapSize() 返回的是含 PSRAM 的"总堆"，
     在 ESP32-S3 上会显示 6MB+ —— 那不是内部 DRAM！
     真实内部 DRAM 要看 heap_caps_get_free_size(MALLOC_CAP_INTERNAL)，
     两者能差 70 倍。之前把它标成 "DRAM" 严重误导排障。 */
  uint32_t freeDram  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  yield();
  uint32_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  yield();
  uint32_t totalHeap = xPortGetFreeHeapSize();
  Serial.printf("[Console] DRAM(internal) free: %u, PSRAM free: %u, total heap: %u\n",
    (unsigned)freeDram, (unsigned)freePsram, (unsigned)totalHeap);
  /* LVGL 对象池（widget 内存）是独立静态池，不走 system DRAM —— 这正是
     "退出浏览器后 DRAM 数字没变"的原因：widget 归还的是这里，不是 DRAM。
     要看 widget 有没有真的释放，必须看这一行。
     lv_mem_monitor 只遍历 LVGL 自己的 tlsf 池，不碰 ESP-IDF 堆锁，安全。 */
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  Serial.printf("[Console] LVGL pool: used %u%% (%u B), free %u B, max block %u B\n",
    (unsigned)mon.used_pct, (unsigned)mon.total_size - (unsigned)mon.free_size,
    (unsigned)mon.free_size, (unsigned)mon.free_biggest_size);
  /* ⚠️ 绝对不要在这里加 heap_caps_get_largest_free_block() / heap_caps_get_info()。
     它们是 O(n) 遍历（multi_heap_get_info → tlsf_walk_pool），持堆锁时间以毫秒计；
     WiFi 收包路径（ppTask → sta_rx_cb → wlanif_input → pbuf_alloc → mem_malloc）
     在另一核等同一把锁 → Interrupt wdt timeout on CPU1 → 整机重启。
     已用 addr2line 实证：PC 停在 multi_heap_get_info_tlsf，见 docs/06。
     heap_caps_get_free_size() 只 O(1) 读字段，实测安全。 */
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

  /* 屏指针变量已在 s_screens 里静态绑定（存的是 &nav_xxx），无需在此填充。
     应用屏改为懒创建后，这里若缓存快照会在 nav_open 建好屏后仍是 nullptr。 */

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