# -*- coding: utf-8 -*-
import io
P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\MEMORY.md"
s = io.open(P, encoding="utf-8").read()

old = """- ⛔ **360 不是新闻源**（平台名 360 / so360 / 360search 全返回 0 条）—— 只能当
  搜索引擎（www.so.com 200 / 449KB）。master 说的「36 开头」= **36氪（36kr）**。"""
new = """- ⛔ **360 不是新闻源**（平台名 360 / so360 / 360search 全返回 0 条）—— 只能当
  搜索引擎（www.so.com 200 / 449KB）。master 说的「36 开头」= **36氪（36kr）**。
- ⚠️ **别再把 36氪 和 360 搞混**（我搞混过）：36氪(36kr) 是科技媒体、**没有搜索**；
  360(www.so.com) 才是搜索引擎。两家不是一回事。
- 搜索引擎目前**只有必应**（master 2026-09-25 拍板退回单一引擎）。
  想加：`kEngines` 加一行 + 首页切换 chip 加回来，代码结构留着。"""
assert s.count(old) == 1
s = s.replace(old, new, 1)
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("ok")

D = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory"
note = """
## 搜索引擎退回只有必应（2026-09-25）

master 问「36氪是不是就是那个有搜索引擎的」—— **不是**。
36氪(36kr) = 科技媒体，是新闻源，**没有搜索功能**；
360(www.so.com) = 搜索引擎，跟 36氪不是一家，而且也不是新闻源。
我之前把这两件事混着说，害得他也混了 —— 以后讲这两个名字必须带上性质
（"36氪=新闻源/媒体"、"360=搜索引擎"），别只说"36 开头"。

他给的判断是"36 开头又能搜的没有就退回只有 Bing" -> 条件成立。
kEngines 只留必应，删掉切换 chip 和回调。想加回来：加一行 + 加回 chip。
"""
io.open(D + r"\2026-09-25.md", "a", encoding="utf-8").write(note)
print("log ok")
