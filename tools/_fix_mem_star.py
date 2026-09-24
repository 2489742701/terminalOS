# -*- coding: utf-8 -*-
"""纠正记忆：新闻星标资格 + 缺字(方框)排查方法。"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\MEMORY.md"
s = io.open(P, encoding="utf-8").read()

old = """- **知乎全部 403**（反爬，App 侧无解）；其余源的前 4 条链接都 200 能打开。
  排序按"能打开"排，verified 的画金星（星星要单独 label：选中态文字变黑它仍金色）。
- 每源缓存放 PSRAM（9 源 ~35KB，⛔ 别放静态数组，DRAM 只剩 ~85KB），TTL 30 分钟。"""

new = """- **知乎全部 403**（反爬，App 侧无解）。
- ⭐ **星标资格 = 能打开详情页看到内容**，不是"列表能加载出来"（master 2026-09-25
  纠正过我一次：很多源列表拉得到，点进去白屏）。
  三档 `NewsPlatform::star`：2=金星（完全能看）/ 1=白星（部分内容或网络问题）/ 0=不标。
  当前：金星 豆瓣·36氪·稀土掘金；白星 B站·GitHub；其余不标。
  ⚠️ **档位是 master 手动定的**，不是脚本测的，改之前先问他。
  星星要单独一个 label —— 选中态（白底黑字）时星星保持原色，不跟着变黑。
- 每源缓存放 PSRAM（9 源 ~35KB，⛔ 别放静态数组，DRAM 只剩 ~85KB），TTL 30 分钟。"""
assert s.count(old) == 1
s = s.replace(old, new, 1)

old2 = """## 字体"""
new2 = """## 字体 / 方框（缺字）
- ⚠️ **界面上出现方框 = 字库里没这个字**，不是编码问题。排查：
  `tools/check_font_chars.py` 或直接查 `tools/font_symbols_16.txt` 里有没有那个字。
  补字：加进 `font_symbols_16.txt` 再跑 `tools/gen_fonts.py`（会重新生成 .c）。
  已补过：「氪」（36氪那个字，master 看到的就是它显示成框）+ 氙/氡/氩。
- 16px 与 24px 是**两份独立字表**，24px 那份缺字多得多（只用于大标题/描述）。

## 字体"""
assert s.count(old2) == 1
s = s.replace(old2, new2, 1)

io.open(P, "w", encoding="utf-8", newline="").write(s)
print("ok")

# 日志
D = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory"
note = """

## 星标资格被 master 纠正 + 「氪」是方框（2026-09-25）

我第一版按"列表能加载出来"给金星 —— **错的**。master 的标准是
「**能打开详情页、能看到里面的内容**」：很多源列表拉得到，点进去白屏。
改成三档 `star`：2=金星（豆瓣/36氪/稀土掘金）、1=白星（B站/GitHub，
能显示部分内容或因网络问题看不全）、0=不标。
⚠️ 档位是他**手动定**的，不是脚本测的 —— 注释里写明改之前先问他。

**「36氪」后面的框子 = 「氪」字库里没有**，一直画成方框。
补进 font_symbols_16.txt（顺带氙/氡/氩），重新生成 16px 字体 → 3931 字。
排查方法：界面出现方框 -> 直接查 tools/font_symbols_16.txt 有没有那个字，
别去怀疑 UTF-8 编码。chip 宽度也改成按名字长度自适应（"稀土掘金"4 字会挤）。
"""
io.open(D + r"\2026-09-25.md", "a", encoding="utf-8").write(note)
print("log ok")
