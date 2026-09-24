# -*- coding: utf-8 -*-
"""热点新闻平台表：重排 + 加 verified（金色小星星）标记。

master 的标准：**能正常打开、能看到里面内容的源排前面，并给一颗金星**。
2026-09-25 实测（PC，Chrome120 UA，每条源取前 4 条真实链接看 HTTP 状态）：
    douban 4/4 OK   36kr 4/4 OK   juejin 4/4 OK   github 4/4 OK
    hackernews 4/4 OK   baidu 4/4 OK(937KB，偏大)   bilibili 4/4 OK
    zhihu 0/4（全部 403，反爬）
    weibo 未测出（脚本 URL 编码问题，待测）
⚠️ **360 不是新闻源**：news.orz.ai 的 360 / so360 / 360search 全部返回 0 条。
   360 只能做搜索引擎（www.so.com 实测 200 / 449KB）。
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\news.h"
src = io.open(P, encoding="utf-8").read()

old = """/* 平台代码 + 中文名。UI 按这个顺序画 chip。 */
struct NewsPlatform {
  const char* code;
  const char* name;
};"""
new = """/* 平台代码 + 中文名 + 是否已验证。
   UI 按这个顺序画 chip —— **能正常打开看内容的排前面**（master 2026-09-25）。
   verified=true 的源，名字旁边画一颗金色小星星：
     选中态（白底黑字）时星星**仍然是金色**，只有文字变黑。
   ⚠️ verified 不是"猜"的，是逐条点开前 4 条链接实测出来的，改之前先测。 */
struct NewsPlatform {
  const char* code;
  const char* name;
  bool verified;
};"""
assert src.count(old) == 1
src = src.replace(old, new, 1)
io.open(P, "w", encoding="utf-8", newline="").write(src)

C = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\news.cpp"
src = io.open(C, encoding="utf-8").read()

old2 = """const NewsPlatform kNewsPlatforms[] = {
    {"baidu", "百度"},   {"weibo", "微博"},   {"zhihu", "知乎"},
    {"36kr", "36氪"},    {"bilibili", "B站"}, {"juejin", "掘金"},
    {"github", "GitHub"}, {"hackernews", "HN"}, {"douban", "豆瓣"},
};"""
new2 = """/* 顺序 = 已验证可打开的在前，打不开的垫底。
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
assert src.count(old2) == 1
src = src.replace(old2, new2, 1)
io.open(C, "w", encoding="utf-8", newline="").write(src)
print("platform table ok")
