# -*- coding: utf-8 -*-
import io
P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\MEMORY.md"
s = io.open(P, encoding="utf-8").read()
old = """- 搜索引擎目前**只有必应**（master 2026-09-25 拍板退回单一引擎）。
  想加：`kEngines` 加一行 + 首页切换 chip 加回来，代码结构留着。"""
new = """- 搜索引擎目前**只有必应**（master 2026-09-25 拍板退回单一引擎）。
  想加：`kEngines` 加一行 + 首页切换 chip 加回来，代码结构留着。
- **360 在设备浏览器上实测能用**（2026-09-25）：
  `www.so.com/s?q=esp32` -> 110 widget / 44 可点链接 / 排版 390ms；
  中文查询 -> 200 widget（**撞到 200 上限**，页面后半截会被砍）/ 31 链接 / 504ms。
  DRAM 余 ~90KB 没问题。所以"能不能用"是能的，取舍在内容完整度。"""
assert s.count(old) == 1
s = s.replace(old, new, 1)
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("ok")
