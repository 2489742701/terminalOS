#include "sd_card.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <stdio.h>

namespace {

SPIClass* g_spi = nullptr;
bool g_mounted = false;
uint8_t g_cardType = 0;

void printEntry(const char* name, bool isDir, uint64_t size) {
  if (isDir) {
    Serial.printf("  [DIR ] %s\n", name);
  } else {
    if (size >= 1024 * 1024)
      Serial.printf("  %6.1f MB  %s\n", size / 1048576.0, name);
    else if (size >= 1024)
      Serial.printf("  %6.1f KB  %s\n", size / 1024.0, name);
    else
      Serial.printf("  %6u B   %s\n", (unsigned)size, name);
  }
}

}  // namespace

namespace SDCard {

bool begin() {
  if (g_mounted) return true;

  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);   /* 先拉高：别让卡在 SPI 起来之前乱说话 */

  if (!g_spi) g_spi = new SPIClass(HSPI);
  /* ⚠️ 必须用独立 SPI 实例（HSPI）。用全局 SPI 会把 Arduino_GFX 已经在用的
     那条 FSPI 总线重新初始化 —— 屏幕当场花掉。 */
  g_spi->begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI);

  /* 先 1MHz 慢慢握（厂家 Demo 就是这个速率），成功后再提速到 4MHz。
     SPI 模式的卡在初始化阶段必须慢速，直接 4M 有时认不出老卡。 */
  if (!SD.begin(PIN_SD_CS, *g_spi, 1000000)) {
    Serial.println("[SD] mount failed at 1MHz (no card / wiring?)");
    return false;
  }
  g_cardType = SD.cardType();
  if (g_cardType == CARD_NONE) {
    Serial.println("[SD] mounted but CARD_NONE -> 卡槽里没卡，或接触不良");
    SD.end();
    return false;
  }

  g_mounted = true;
  Serial.printf("[SD] ok type=%s size=%.2f GB fs: %.2f/%.2f GB used\n",
                typeName(), cardBytes() / 1073741824.0,
                usedBytes() / 1073741824.0, totalBytes() / 1073741824.0);
  return true;
}

void end() {
  if (!g_mounted) return;
  SD.end();
  if (g_spi) g_spi->end();
  g_mounted = false;
  Serial.println("[SD] unmounted");
}

bool mounted() { return g_mounted; }

uint64_t cardBytes() { return g_mounted ? (uint64_t)SD.cardSize() : 0ULL; }
uint64_t totalBytes() { return g_mounted ? (uint64_t)SD.totalBytes() : 0ULL; }
uint64_t usedBytes() { return g_mounted ? (uint64_t)SD.usedBytes() : 0ULL; }

const char* typeName() {
  switch (g_cardType) {
    case CARD_NONE: return "NONE";
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SD";
    case CARD_SDHC: return "SDHC";
    default: return "UNKNOWN";
  }
}

void listDir(const char* path, int depth) {
  if (!g_mounted) {
    Serial.println("[SD] not mounted");
    return;
  }
  File root = SD.open(path ? path : "/");
  if (!root) {
    Serial.printf("[SD] open failed: %s\n", path ? path : "/");
    return;
  }
  if (!root.isDirectory()) {
    printEntry(path, false, root.size());
    root.close();
    return;
  }

  Serial.printf("[SD] %s\n", path ? path : "/");
  int n = 0;
  File f = root.openNextFile();
  while (f && n < 40) {
    char full[256];
    snprintf(full, sizeof(full), "%s/%s", path ? path : "", f.name());
    printEntry(f.name(), f.isDirectory(), f.isDirectory() ? 0 : f.size());
    if (f.isDirectory() && depth > 1) listDir(full, depth - 1);
    f = root.openNextFile();
    n++;
  }
  root.close();
}

}  // namespace SDCard
