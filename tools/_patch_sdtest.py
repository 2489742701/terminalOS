# -*- coding: utf-8 -*-
"""加 SDCard::selfTest：隔离验证 SD 读写是否会改字节（2026-09-25 诊断）"""
import io

ROOT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
H = ROOT + r"\src\hal\sd_card.h"
C = ROOT + r"\src\hal\sd_card.cpp"

h = io.open(H, encoding="utf-8", newline="").read()
h = h.replace("// 列目录到串口，depth 是递归层数（默认 1）",
"""/* 读写自检：写一段可预测的字节模式再原样读回比对。
   用来隔离"SD 读写本身会不会改字节" —— 页面缓存读回来内容对不上时先跑它。
   kb = 测试数据量（默认 128）。串口 `sdtest [kb]`。 */
bool selfTest(uint32_t kb);

// 列目录到串口，depth 是递归层数（默认 1）""", 1)
io.open(H, "w", encoding="utf-8", newline="").write(h)
print("OK h")

c = io.open(C, encoding="utf-8", newline="").read()
impl = r'''
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

  SD.remove(path);
  heap_caps_free(pat);
  Serial.printf("[SDTest] %s\n", okAll ? "ALL OK" : "FAILED");
  return okAll;
}

}  // namespace SDCard'''
c = c.replace("\n}  // namespace SDCard", impl, 1)
io.open(C, "w", encoding="utf-8", newline="").write(c)
print("OK cpp")
