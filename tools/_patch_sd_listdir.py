# -*- coding: utf-8 -*-
"""给 SDCard 加目录枚举（缓存相册要列 /gt 下的缩略图）。

⛔ 不在业务层直接 #include <SD.h> 去 open 目录 —— 一旦 SD 没挂载就会去动
   与 LCD 共用的那条 SPI。枚举也走 sd_card 这层门。
"""
import io
BASE = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal'


def w(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


def rep(s, old, new, tag, cnt=1):
    n = s.count(old)
    assert n == cnt, ('anchor %s: found %d' % (tag, n))
    return s.replace(old, new, 1)


H = BASE + r'\src\hal\sd_card.h'
s = io.open(H, encoding='utf-8').read()
old = """/* 文件是否存在 + 大小。exists 只要"在不在"就传 NULL 给 size。 */
bool statFile(const char* path, size_t* size);"""
new = """/* 文件是否存在 + 大小。exists 只要"在不在"就传 NULL 给 size。 */
bool statFile(const char* path, size_t* size);

/* 列目录下的**文件名**（不含目录前缀），最多 max 个，返回实际个数。
   子目录和隐藏文件都会列出来，匹配前缀/后缀由调用方自己过滤。
   ⛔ 未挂载返回 0（绝不碰 SPI）。 */
int listDirNames(const char* dir, String* out, int max);"""
s = rep(s, old, new, 'hdr')
w(H, s)

C = BASE + r'\src\hal\sd_card.cpp'
s = io.open(C, encoding='utf-8').read()
old = """bool readFile(const char* path, String& out) {"""
new = """int listDirNames(const char* dir, String* out, int max) {
  if (!g_mounted || !dir || !out || max <= 0) return 0;
  File root = SD.open(dir);
  if (!root) return 0;
  int n = 0;
  while (n < max) {
    File f = root.openNextFile();
    if (!f) break;
    String nm = String(f.name());
    f.close();
    /* SD 的 name() 给的是带目录的完整路径（"/gt/t1234abcd.thm"），
       这里统一剥成纯文件名，调用方才好按前缀/后缀过滤。 */
    const char* slash = strrchr(nm.c_str(), '/');
    out[n++] = slash ? String(slash + 1) : nm;
  }
  root.close();
  return n;
}

bool readFile(const char* path, String& out) {"""
s = rep(s, old, new, 'cpp')
w(C, s)
print('ok')
