# -*- coding: utf-8 -*-
"""检查热点新闻源名字（以及界面上会出现的字）里有哪些字库没有 —— 缺字就是方框。"""
import io, os

BASE = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"

s16 = io.open(os.path.join(BASE, "tools", "font_symbols_16.txt"), encoding="utf-8").read()
s24 = io.open(os.path.join(BASE, "tools", "font_symbols_24.txt"), encoding="utf-8").read()

names = "豆瓣 36氪 稀土掘金 B站 GitHub 微博 知乎 百度 HN 更新 分钟前 小时前 已是最新 每 一次 加载 失败 换个源试试 点上面的源 下拉看 日预报 未来 小时 当前实况 近 体感 湿度 降水 云量 气压 海拔 风速 阵风 风向 今日最高 今日最低 日出 日落 紫外线 地面 昼夜 天气 日历 年 月 今天 周 搜索引擎 必应 百度 刷新"

miss16 = sorted(set(c for c in names if '\u4e00' <= c <= '\u9fff' and c not in s16))
miss24 = sorted(set(c for c in names if '\u4e00' <= c <= '\u9fff' and c not in s24))
print("16px 缺:", "".join(miss16) or "(无)")
print("24px 缺:", "".join(miss24) or "(无)")

# 顺带看看「氪」同类的化学/生僻字有没有其它要用的
extra = "氪氙氡氦氖氩氪"
for c in extra:
    print(c, "16:", c in s16, "24:", c in s24)
