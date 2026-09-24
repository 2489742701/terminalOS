# -*- coding: utf-8 -*-
"""新闻源星标改成**三档**（master 2026-09-25 纠正）：

  ⚠️ 资格不是"能加载出列表"，而是**能打开详情页看到内容**。
     star: 2 = 金星（完全能看）  1 = 白星（部分内容 / 网络问题看不全）  0 = 不给星

  master 手动定的：
     金星：豆瓣、36氪、稀土掘金
     白星：B站、GitHub（能显示部分内容，或因网络问题无法全部显示）
     其它：不标星

  另外「36氪」的**氪**原本是方框 —— 字库里没这个字，已补进 font_symbols_16.txt。
"""
import io

H = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\news.h"
s = io.open(H, encoding="utf-8").read()
old = """/* 平台代码 + 中文名 + 是否已验证。
   UI 按这个顺序画 chip —— **能正常打开看内容的排前面**（master 2026-09-25）。
   verified=true 的源，名字旁边画一颗金色小星星：
     选中态（白底黑字）时星星**仍然是金色**，只有文字变黑。
   ⚠️ verified 不是"猜"的，是逐条点开前 4 条链接实测出来的，改之前先测。 */
struct NewsPlatform {
  const char* code;
  const char* name;
  bool verified;
};"""
new = """/* 平台代码 + 中文名 + 星标档位。UI 按这个顺序画 chip。
   ⚠️ 星标的资格是「**能打开详情页看到内容**」，不是"能加载出列表"
      （master 2026-09-25 纠正）—— 很多源列表拉得到，点进去却是白屏。
   star: 2 = 金星（完全能看）  1 = 白星（部分内容 / 网络问题看不全）  0 = 不标
   ⚠️ 档位是 master 手动定的，不是脚本测出来的 —— 改之前先问他。 */
struct NewsPlatform {
  const char* code;
  const char* name;
  int star;
};"""
assert s.count(old) == 1
s = s.replace(old, new, 1)
io.open(H, "w", encoding="utf-8", newline="").write(s)

C = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\news.cpp"
s = io.open(C, encoding="utf-8").read()
old2 = """/* 顺序 = 已验证可打开的在前，打不开的垫底。
   2026-09-25 实测：豆瓣 / 36氪 / 掘金 / GitHub / HN / 百度 / B站 的前 4 条
   链接全部 200 能打开；知乎全部 403（反爬），排最后且不给星。 */
const NewsPlatform kNewsPlatforms[] = {
    {"douban",     "豆瓣",    true},
    {"36kr",       "36氪",    true},
    {"juejin",     "掘金",    true},
    {"github",     "GitHub",  true},
    {"hackernews", "HN",      true},
    {"baidu",      "百度",    true},
    {"bilibili",   "B站",     true},
    {"weibo",      "微博",    false},
    {"zhihu",      "知乎",    false},
};"""
new2 = """/* 顺序：有星的在前（金星 -> 白星），没星的垫底。
   星标档位由 master 手动定，标准是「点进详情页能不能看到内容」：
     金星 豆瓣 / 36氪 / 稀土掘金 —— 完全能看
     白星 B站 / GitHub          —— 能显示部分内容，或因网络问题看不全
     无星 其余                  —— 打不开 / 没验证
   ⚠️ 注意「氪」字：字库里原本没有，显示为方框，已补进 font_symbols_16.txt。 */
const NewsPlatform kNewsPlatforms[] = {
    {"douban",     "豆瓣",    2},
    {"36kr",       "36氪",    2},
    {"juejin",     "稀土掘金", 2},
    {"bilibili",   "B站",     1},
    {"github",     "GitHub",  1},
    {"baidu",      "百度",    0},
    {"hackernews", "HN",      0},
    {"weibo",      "微博",    0},
    {"zhihu",      "知乎",    0},
};"""
assert s.count(old2) == 1
s = s.replace(old2, new2, 1)
io.open(C, "w", encoding="utf-8", newline="").write(s)
print("star ok")
