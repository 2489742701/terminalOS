# -*- coding: utf-8 -*-
import io
P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\MEMORY.md"
s = io.open(P, encoding="utf-8").read()

old = "## 外设"
new = """## 热点新闻源（news.orz.ai，2026-09-25 实测）
- 可用：baidu51 weibo51 zhihu30 36kr50 bilibili20 juejin50 github30 hackernews30 douban30。
- ⛔ **360 不是新闻源**（平台名 360 / so360 / 360search 全返回 0 条）—— 只能当
  搜索引擎（www.so.com 200 / 449KB）。master 说的「36 开头」= **36氪（36kr）**。
- **知乎全部 403**（反爬，App 侧无解）；其余源的前 4 条链接都 200 能打开。
  排序按"能打开"排，verified 的画金星（星星要单独 label：选中态文字变黑它仍金色）。
- 每源缓存放 PSRAM（9 源 ~35KB，⛔ 别放静态数组，DRAM 只剩 ~85KB），TTL 30 分钟。

## 天气 / 日历
- 后台自动更新 = 常驻任务带超时等通知（1 小时）。
  ⛔ 关开关必须退回 portMAX_DELAY 睡眠 —— "醒了再看开关"等于每小时唤醒一次，不算停止。
- JSON 整块缓存到 SD 卡，读回来复用同一个解析函数。SDCard 的读写在未挂载时静默失败。
- ⛔ **LVGL 圆角很贵**：24 个格子加圆角 6 → 88ms/帧（11fps）；改直角回到 ~30ms。
- 日历（calendar_screen）：月视图，Flash +8.8KB / 常驻 DRAM +0.7KB / 运行时 ~10KB。
  农历没做 —— 光闰月表就要 ~10KB Flash。

## 外设"""
assert s.count(old) == 1
s = s.replace(old, new, 1)
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("MEMORY.md ok")
