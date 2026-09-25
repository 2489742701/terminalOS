# -*- coding: utf-8 -*-
"""给 SDCard 补一个二进制整文件读（图片缓存回读用）。

⚠️ 记忆：Edit 工具在 CRLF 文件上会假成功 —— 一律用脚本改，改完 grep 核对。
"""
import io, sys

H = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\hal\sd_card.h'
C = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\hal\sd_card.cpp'

# ---------- 1. 头文件声明 ----------
s = io.open(H, encoding='utf-8').read()
old = """bool writeFileBin(const char* path, const uint8_t* data, size_t len);

}  // namespace SDCard"""
new = """bool writeFileBin(const char* path, const uint8_t* data, size_t len);

/* 二进制整文件读（图片缓存回读）。
   *out 用 heap_caps_malloc(MALLOC_CAP_SPIRAM) 分配 —— 图片字节必须落 PSRAM，
   内部 DRAM 只有几百 KB，一张 64KB 的图就能把它吃掉一大截。
   调用方负责 heap_caps_free(*out)。未挂载 / 打不开 / 长度对不上一律 false。
   ⚠️ 按**字节数**读并比对实际读到的长度，不依赖任何 NUL 结尾（见 readFile）。 */
bool readFileBin(const char* path, uint8_t** out, size_t* outLen);

/* 文件是否存在 + 大小。exists 只要"在不在"就传 NULL 给 size。 */
bool statFile(const char* path, size_t* size);

}  // namespace SDCard"""
assert s.count(old) == 1, ('hdr anchor', s.count(old))
s = s.replace(old, new, 1)
io.open(H, 'w', encoding='utf-8', newline='').write(s)
print('sd_card.h ok')

# ---------- 2. 实现 ----------
s = io.open(C, encoding='utf-8').read()
old = """bool readFile(const char* path, String& out) {"""
new = """bool readFileBin(const char* path, uint8_t** out, size_t* outLen) {
  if (out) *out = nullptr;
  if (outLen) *outLen = 0;
  if (!g_mounted || !path || !out || !outLen) return false;

  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  size_t n = (size_t)f.size();
  if (n == 0) { f.close(); return false; }

  uint8_t* buf = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) { f.close(); return false; }
  size_t got = f.read(buf, n);
  f.close();
  if (got != n) { heap_caps_free(buf); return false; }

  *out = buf;
  *outLen = n;
  return true;
}

bool statFile(const char* path, size_t* size) {
  if (!g_mounted || !path) return false;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  size_t n = (size_t)f.size();
  f.close();
  if (size) *size = n;
  return true;
}

bool readFile(const char* path, String& out) {"""
assert s.count(old) == 1, ('cpp anchor', s.count(old))
s = s.replace(old, new, 1)
io.open(C, 'w', encoding='utf-8', newline='').write(s)
print('sd_card.cpp ok')
