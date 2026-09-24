#include "sd_card.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
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

static uint32_t g_spiHz = 1000000;

static bool mountAt(uint32_t hz) {
  if (SD.begin(PIN_SD_CS, *g_spi, hz)) return true;
  SD.end();
  return false;
}

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

  /* ⚠️ 握手必须在低速（SPI 模式初始化规范要求），但**握完必须提速**：
     一直停在 1MHz 实测只有 0.11 MB/s，读 300KB 要 2.6 秒 —— 慢到没法用。
     这里 1MHz 握完就 end() 掉，按 16M → 8M → 4M 依次重试，全失败退回 1M。 */
  SD.end();
  const uint32_t speeds[3] = {16000000, 8000000, 4000000};
  uint32_t used = 0;
  for (int i = 0; i < 3; i++) {
    if (mountAt(speeds[i])) { used = speeds[i]; break; }
  }
  if (!used) {
    if (!mountAt(1000000)) {
      Serial.println("[SD] re-mount failed even at 1MHz");
      return false;
    }
    used = 1000000;
  }
  g_spiHz = used;

  g_mounted = true;
  Serial.printf("[SD] ok type=%s spi=%u Hz size=%.2f GB fs: %.2f/%.2f GB used\n",
                typeName(), (unsigned)g_spiHz,
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

/* 读写速度实测。用途：评估「应用放 SD 卡、按需加载」时，读一个 app 要多久。
   这条总线是 SPI 模式（不是 SDIO），速度天花板本来就低，先量再决策。 */
uint32_t spiHz() { return g_spiHz; }

void bench(uint32_t kb) {
  if (!g_mounted && !begin()) {
    Serial.println("[SD] bench: mount failed");
    return;
  }
  if (kb == 0) kb = 256;
  const size_t CH = 16 * 1024;
  uint8_t* buf = (uint8_t*)malloc(CH);
  if (!buf) { Serial.println("[SD] bench: no buffer"); return; }
  memset(buf, 0xA5, CH);

  const char* path = "/.bench.tmp";
  const uint32_t total = kb * 1024;

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.println("[SD] bench: open for write failed");
    free(buf);
    return;
  }
  uint32_t t0 = millis();
  uint32_t w = 0;
  while (w < total) {
    size_t n = f.write(buf, CH);
    if (!n) break;
    w += n;
  }
  f.close();
  uint32_t dtw = millis() - t0;

  t0 = millis();
  uint32_t r = 0;
  f = SD.open(path, FILE_READ);
  if (f) {
    while (r < total) {
      int n = f.read(buf, CH);
      if (n <= 0) break;
      r += (uint32_t)n;
    }
    f.close();
  }
  uint32_t dtr = millis() - t0;

  SD.remove(path);
  free(buf);

  if (!dtw) dtw = 1;
  if (!dtr) dtr = 1;
  double wmb = (w / (double)dtw) * 1000.0 / 1048576.0;
  double rmb = (r / (double)dtr) * 1000.0 / 1048576.0;
  Serial.printf("[SD] bench %u KB @ %u Hz: write %u B/%u ms = %.2f MB/s | "
                "read %u B/%u ms = %.2f MB/s\n",
                (unsigned)kb, (unsigned)g_spiHz, (unsigned)w, (unsigned)dtw, wmb,
                (unsigned)r, (unsigned)dtr, rmb);
  /* 换算成真实体感：一个 300KB 的 app 从卡里读出来要多久 */
  double perKBms = (double)dtr / (r / 1024.0);
  Serial.printf("[SD] 换算: 读 300KB 的 app 约 %.0f ms, 1MB 约 %.0f ms\n",
                perKBms * 300.0, perKBms * 1024.0);
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

bool writeFile(const char* path, const String& data) {
  if (!g_mounted || !path) return false;
  /* 建父目录：/gt/weather.json -> 先 mkdir /gt */
  const char* slash = strrchr(path, '/');
  if (slash && slash > path) {
    char dir[64];
    int n = (int)(slash - path);
    if (n >= (int)sizeof(dir)) n = (int)sizeof(dir) - 1;
    memcpy(dir, path, (size_t)n);
    dir[n] = '\0';
    if (!SD.exists(dir)) SD.mkdir(dir);
  }
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  size_t w = f.write((const uint8_t*)data.c_str(), data.length());
  f.close();
  return w == data.length();
}

bool readFile(const char* path, String& out) {
  if (!g_mounted || !path) return false;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  /* ⚠️ 2026-09-25：以前这里写的是 `out += buf`。
     `String::operator+=(const char*)` 走 strlen —— **遇到 0x00 就停**，
     于是文件里每出现一个 NUL，它所在的那一块剩下的字节全被丢掉。
     实测：131072 B 的测试文件（含 NUL）只读回 71892 B，且内容从头就错位。
     网页 HTML（必应/360）的内联 JS 里就带 0x00，缓存读回来是错位的，
     lexbor 于是把 JS 当成标签解析 —— 表现就是页面上冒出一坨脚本源码。
     → 改用 concat(ptr, len) 按长度拷，不再依赖 NUL 结尾。 */
  out = "";
  size_t total = (size_t)f.size();
  if (total) out.reserve(total + 1);   /* 免得每块都重新分配（O(n^2)） */

  uint8_t buf[512];
  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    out.concat((const char*)buf, (unsigned int)n);
  }
  f.close();
  return out.length() > 0;
}

/* 读写自检（2026-09-25）
   页面缓存写到 SD 再读回来，96KB 的文件**长度一模一样但字节从 182 开始就不对**，
   而且换一页还是同一个偏移 —— 说明不是 HTML 的问题，是这一层。
   这里写一段"第 i 字节 = i 的函数"再原样读回，逐字节比对，
   就能分清是写坏了、还是读坏了、还是只在超过某个大小后才坏。 */
bool selfTest(uint32_t kb) {
  if (!g_mounted) { Serial.println("[SDTest] not mounted"); return false; }
  if (kb == 0) kb = 128;
  if (kb > 1024) kb = 1024;
  size_t n = (size_t)kb * 1024;

  uint8_t* pat = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!pat) { Serial.println("[SDTest] alloc failed"); return false; }
  for (size_t i = 0; i < n; i++) pat[i] = (uint8_t)(i * 31u + (i >> 8));

  const char* path = "/gt/_selftest.bin";
  bool okAll = true;

  /* ── 1) 一次大 write（writeFile 就是这么写的）── */
  {
    File f = SD.open(path, FILE_WRITE);
    if (!f) { Serial.println("[SDTest] open for write failed"); heap_caps_free(pat); return false; }
    size_t w = f.write(pat, n);
    f.close();
    Serial.printf("[SDTest] big-write: want=%u wrote=%u\n", (unsigned)n, (unsigned)w);

    File g = SD.open(path, FILE_READ);
    if (!g) { Serial.println("[SDTest] open for read failed"); heap_caps_free(pat); return false; }
    size_t bad = 0, firstBad = 0;
    uint8_t rb[512];
    size_t pos = 0;
    while (pos < n) {
      int r = g.read(rb, sizeof(rb));
      if (r <= 0) break;
      for (int k = 0; k < r && pos + (size_t)k < n; k++) {
        if (rb[k] != pat[pos + (size_t)k]) {
          if (bad == 0) firstBad = pos + (size_t)k;
          bad++;
        }
      }
      pos += (size_t)r;
    }
    g.close();
    Serial.printf("[SDTest] big-write readback: got=%u bad=%u firstBad=%u %s\n",
                  (unsigned)pos, (unsigned)bad, (unsigned)firstBad,
                  bad ? "MISMATCH" : "MATCH");
    if (bad) okAll = false;
  }

  /* ── 2) 分块写（4KB 一块）── */
  {
    File f = SD.open(path, FILE_WRITE);
    if (!f) { heap_caps_free(pat); return false; }
    size_t off = 0;
    while (off < n) {
      size_t chunk = n - off > 4096 ? 4096 : n - off;
      if (f.write(pat + off, chunk) != chunk) break;
      off += chunk;
    }
    f.flush();
    f.close();
    Serial.printf("[SDTest] chunk-write: wrote=%u\n", (unsigned)off);

    File g = SD.open(path, FILE_READ);
    if (!g) { heap_caps_free(pat); return false; }
    size_t bad = 0, firstBad = 0, pos = 0;
    uint8_t rb[512];
    while (pos < n) {
      int r = g.read(rb, sizeof(rb));
      if (r <= 0) break;
      for (int k = 0; k < r && pos + (size_t)k < n; k++) {
        if (rb[k] != pat[pos + (size_t)k]) {
          if (bad == 0) firstBad = pos + (size_t)k;
          bad++;
        }
      }
      pos += (size_t)r;
    }
    g.close();
    Serial.printf("[SDTest] chunk-write readback: got=%u bad=%u firstBad=%u %s\n",
                  (unsigned)pos, (unsigned)bad, (unsigned)firstBad,
                  bad ? "MISMATCH" : "MATCH");
    if (bad) okAll = false;
  }

  /* ── 3) 走 writeFile/readFile（页面缓存真正用的那条路）──
        模式里**不掺 0x00**，先排除 NUL 截断这个已知嫌疑。 */
  {
    String sdata;
    sdata.reserve(n + 16);
    for (size_t i = 0; i < n; i++) sdata += (char)(uint8_t)(i * 31u + (i >> 8));
    bool wok = SDCard::writeFile(path, sdata);
    String back;
    bool rok = SDCard::readFile(path, back);
    size_t bad = 0, firstBad = 0;
    size_t m = back.length() < n ? back.length() : n;
    for (size_t i = 0; i < m; i++)
      if ((uint8_t)back[i] != (uint8_t)sdata[i]) { if (!bad) firstBad = i; bad++; }
    Serial.printf("[SDTest] writeFile/readFile: w=%d r=%d len=%u/%u bad=%u firstBad=%u %s\n",
                  (int)wok, (int)rok, (unsigned)back.length(), (unsigned)n,
                  (unsigned)bad, (unsigned)firstBad, (bad || !wok || !rok) ? "MISMATCH" : "MATCH");
    if (bad || !wok || !rok) okAll = false;
  }

  /* ── 4) 同上，但模式里掺入 0x00（验证 String 累加遇到 NUL 会怎样）── */
  {
    String sdata;
    sdata.reserve(n + 16);
    for (size_t i = 0; i < n; i++) {
      uint8_t v = (uint8_t)(i * 31u + (i >> 8));
      if ((i % 997) == 0) v = 0;      /* 每 997 字节塞一个 NUL */
      sdata += (char)v;
    }
    bool wok = SDCard::writeFile(path, sdata);
    String back;
    bool rok = SDCard::readFile(path, back);
    Serial.printf("[SDTest] with-NUL: len=%u want=%u %s\n",
                  (unsigned)back.length(), (unsigned)n,
                  (back.length() == n) ? "LEN-OK" : "LEN-SHORT");
    if (back.length() != n) okAll = false;
  }

  SD.remove(path);
  heap_caps_free(pat);
  Serial.printf("[SDTest] %s\n", okAll ? "ALL OK" : "FAILED");
  return okAll;
}

}  // namespace SDCard
