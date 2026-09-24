# -*- coding: utf-8 -*-
"""给 SDCard 加 readFile / writeFile（天气 JSON 缓存要用）。

⚠️ 用全局 SD 对象之前必须确认 mounted()：SD 是懒挂载的，没挂就 open 会
   初始化 SPI 跟 LCD 抢总线 -> 花屏。
"""
import io

H = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\hal\sd_card.h"
src = io.open(H, encoding="utf-8").read()
old = """// 列目录到串口，depth 是递归层数（默认 1）
void listDir(const char* path, int depth);"""
new = """// 列目录到串口，depth 是递归层数（默认 1）
void listDir(const char* path, int depth);

/* 整文件读写（天气 JSON 缓存等）。未挂载 / 失败一律返回 false，调用方静默跳过。
   ⚠️ 写之前会建父目录（/gt 这种）。别在没挂载时调 —— 会去动 SPI。 */
bool writeFile(const char* path, const String& data);
bool readFile(const char* path, String& out);"""
assert src.count(old) == 1
src = src.replace(old, new, 1)
src = src.replace("#include <stdint.h>", "#include <stdint.h>\n#include <Arduino.h>", 1)
io.open(H, "w", encoding="utf-8", newline="").write(src)

C = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\hal\sd_card.cpp"
src = io.open(C, encoding="utf-8").read()
old2 = """}  // namespace SDCard"""
new2 = """bool writeFile(const char* path, const String& data) {
  if (!g_mounted || !path) return false;
  /* 建父目录：/gt/weather.json -> 先 mkdir /gt */
  const char* slash = strrchr(path, '/');
  if (slash && slash > path) {
    char dir[64];
    int n = (int)(slash - path);
    if (n >= (int)sizeof(dir)) n = (int)sizeof(dir) - 1;
    memcpy(dir, path, (size_t)n);
    dir[n] = '\\0';
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
  out = "";
  while (f.available()) {
    char buf[256];
    int n = f.read((uint8_t*)buf, sizeof(buf) - 1);
    if (n <= 0) break;
    buf[n] = '\\0';
    out += buf;
  }
  f.close();
  return out.length() > 0;
}

}  // namespace SDCard"""
assert src.count(old2) == 1
src = src.replace(old2, new2, 1)
io.open(C, "w", encoding="utf-8", newline="").write(src)
print("sd file io ok")
