# -*- coding: utf-8 -*-
"""防御：没插卡时别把片内 LittleFS 写满。

"每打开一页就落盘"在 SD 卡上无所谓（GB 级），退回 LittleFS 时片内只有几百 KB，
几页就写满 —— 写满不是"存不下"，是整个分区挂掉。留 4KB 余量，不够就不缓存：
少一份缓存只是多等一次网络。
"""
import io
B = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp'
s = io.open(B, encoding='utf-8').read()

old = """  if (!LittleFS.begin(false)) return false;
  storePath(p, sizeof(p), name, false);
  File f = LittleFS.open(p, FILE_WRITE);
  if (!f) { LittleFS.end(); return false; }
  size_t got = f.write(data, len);"""
new = """  if (!LittleFS.begin(false)) return false;
  /* ⛔ 片内 Flash 只有几百 KB，"每页都落盘"几页就写满 —— 写满不是存不下，
     是整个分区废掉。留 4KB 余量，不够就不缓存（代价只是多等一次网络）。
     SD 卡路径不需要这道闸：GB 级容量，塞不满。 */
  size_t avail = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (avail < len + 4096) { LittleFS.end(); return false; }
  storePath(p, sizeof(p), name, false);
  File f = LittleFS.open(p, FILE_WRITE);
  if (!f) { LittleFS.end(); return false; }
  size_t got = f.write(data, len);"""
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)
io.open(B, 'w', encoding='utf-8', newline='').write(s)
print('ok')
