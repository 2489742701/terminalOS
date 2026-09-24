# -*- coding: utf-8 -*-
import io, os
D = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory"

note = """

## 2026-09-25 深夜批次：新闻源 / 360搜索 / 天气后台 / 日历

**⚠️ 360 不是新闻源**：news.orz.ai 的 `360`/`so360`/`360search` 全部返回 0 条
（273 B 空响应）。master 说的"36 开头能打开的新闻源"= **36氪（36kr）**，
实测 50 条、前 4 条链接全部 200。360 只能当搜索引擎（www.so.com 200 / 449KB）。
news.orz.ai 现有可用源：baidu51 weibo51 zhihu30 36kr50 bilibili20 juejin50
github30 hackernews30 douban30。

**各源链接实测能不能打开**（PC，取前 4 条）：
豆瓣/36氪/掘金/GitHub/HN/百度/B站 全部 200 能打开；**知乎全部 403**（反爬）。
→ 新闻源排序按"能打开"排，verified=true 的画金色小星星（选中态白底黑字时
星星仍金色，所以星星要单独一个 label，不能跟名字共用一个）。

**新闻缓存**：每源一份放 PSRAM（`heap_caps_calloc(..., MALLOC_CAP_SPIRAM)`，
9 源 ~35KB）。⚠️ 不能放静态数组 —— 内部 DRAM 只剩 ~85KB。30 分钟 TTL，
底部「更新」按钮 force=true 跳过节流。

**天气后台**：常驻任务改成 `ulTaskNotifyTake(pdTRUE, g_auto ? 1h : portMAX_DELAY)`。
⚠️ 关开关必须退化成 portMAX_DELAY —— 靠"醒了再看开关"等于每小时唤醒一次，
那不叫停止。设置项「系统设置 → 天气后台更新」**立即生效**（跟动画的重启生效不同）。

**天气 SD 缓存**：整块 JSON 存 /gt/weather.json，读回来复用同一套解析
（把 fetchOnce 的解析抽成 `parseWeatherJson`）。SDCard 新增 writeFile/readFile，
⚠️ 未挂载一律静默失败（SD 是懒挂载的，别因为它失败就把抓取判失败）。

**⚠️ LVGL 圆角很贵**：天气页给 24 个逐时格子加圆角 6 → 渲染从 ~30ms/帧
涨到 **88ms/帧**（11fps）。圆角要走抗锯齿。改成直角后恢复。
以后给"列表里几十个"的容器加装饰，先想清楚圆角代价。

**日历**（新增 calendar_screen）：月视图，翻月按钮 + 今天金色高亮 + 点日期看星期。
占用实测：Flash +8.8KB、常驻 DRAM +0.7KB、运行时 ~10KB（屏在才占）。
农历/节假日没做 —— 光闰月表就要 ~10KB Flash。

## 一个观察（未处理）
串口操作期间出现 `[Nav] open settings` 等**没有发命令就自己开屏**的现象，
screen changed 连跳三次。像是 **GT911 触摸噪声/误触发**（设备平放在桌上）。
跟 back 逻辑无关（`back` + `navlist` 验证返回和后台保留都正常）。
master 之前也提过触摸偏移，这两件事可能是同一个根因，值得单独查一次。
"""

io.open(os.path.join(D, "2026-09-25.md"), "a", encoding="utf-8").write(note)
print("ok")
