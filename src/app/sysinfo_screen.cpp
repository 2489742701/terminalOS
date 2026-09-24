#include "sysinfo_screen.h"
#include "icons.h"
#include "nav.h"
#include "status_bar.h"
#include "font_zh.h"
#include <lvgl.h>
#include <esp_chip_info.h>
#include <esp_spi_flash.h>
#include <esp_system.h>

#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>

namespace {

SwipeState g_swipe;
lv_obj_t* g_infoLab = nullptr;
lv_obj_t* g_memLab = nullptr;
lv_obj_t* g_wifiLab = nullptr;
uint32_t g_lastUpdate = 0;

void swipe_cb(lv_event_t* e) {
  swipe_back_to_any(e, g_swipe, nav_launcher);
}

void addRow(lv_obj_t* parent, const char* key, const char* val, int y) {
  lv_obj_t* k = lv_label_create(parent);
  lv_label_set_text(k, key);
  lv_obj_set_style_text_color(k, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(k, &font_zh_16, 0);
  lv_obj_align(k, LV_ALIGN_TOP_LEFT, 30, y);

  lv_obj_t* v = lv_label_create(parent);
  lv_label_set_text(v, val);
  lv_obj_set_style_text_color(v, lv_color_white(), 0);
  lv_obj_set_style_text_font(v, &font_zh_16, 0);
  lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -30, y);
}

void updateInfo() {
  if (!g_memLab) return;

  uint32_t freeHeap = ESP.getFreeHeap() / 1024;
  uint32_t freePsram = ESP.getFreePsram() / 1024;
  uint32_t totalPsram = ESP.getPsramSize() / 1024;
  uint32_t uptime = millis() / 1000;
  uint32_t h = uptime / 3600;
  uint32_t m = (uptime % 3600) / 60;
  uint32_t s = uptime % 60;

  char buf[128];
  snprintf(buf, sizeof(buf),
    "空闲内存 %lu KB\nPSRAM %lu/%lu KB\n运行 %lu:%02lu:%02lu",
    (unsigned long)freeHeap, (unsigned long)freePsram,
    (unsigned long)totalPsram, (unsigned long)h, (unsigned long)m, (unsigned long)s);
  lv_label_set_text(g_memLab, buf);

  if (g_wifiLab) {
    if (WiFi.status() == WL_CONNECTED) {
      snprintf(buf, sizeof(buf), "已连接 %s\nIP %s",
               WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    } else {
      strcpy(buf, "未连接");
    }
    lv_label_set_text(g_wifiLab, buf);
  }
}

}  // namespace

lv_obj_t* SysInfoScreen_create() {
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

  StatusBar_create(scr, "系统信息");

  esp_chip_info_t chip;
  esp_chip_info(&chip);
  char chipBuf[32];
  snprintf(chipBuf, sizeof(chipBuf), "ESP32-S3 rev%d", chip.revision);
  addRow(scr, "芯片", chipBuf, 70);
  addRow(scr, "CPU", "240 MHz", 100);
  addRow(scr, "Flash", "16 MB", 130);

  g_memLab = lv_label_create(scr);
  lv_label_set_text(g_memLab, "");
  lv_obj_set_style_text_color(g_memLab, lv_color_white(), 0);
  lv_obj_set_style_text_font(g_memLab, &font_zh_16, 0);
  lv_obj_align(g_memLab, LV_ALIGN_TOP_LEFT, 30, 165);

  lv_obj_t* wifiTitle = lv_label_create(scr);
  lv_label_set_text(wifiTitle, "WiFi");
  lv_obj_set_style_text_color(wifiTitle, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(wifiTitle, &font_zh_16, 0);
  lv_obj_align(wifiTitle, LV_ALIGN_TOP_LEFT, 30, 260);

  g_wifiLab = lv_label_create(scr);
  lv_label_set_text(g_wifiLab, "未连接");
  lv_obj_set_style_text_color(g_wifiLab, lv_color_white(), 0);
  lv_obj_set_style_text_font(g_wifiLab, &font_zh_16, 0);
  lv_obj_align(g_wifiLab, LV_ALIGN_TOP_LEFT, 30, 285);

  updateInfo();
  return scr;
}

void SysInfoScreen_tick() {
  if (millis() - g_lastUpdate > 2000) {
    g_lastUpdate = millis();
    updateInfo();
  }
}