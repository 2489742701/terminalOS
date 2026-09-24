#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""把 2026-09-23 晚间的崩溃修复批次写进今日日志 + 长期记忆。

⚠️ 本项目坑 J3：反引号会被 shell 吃掉 —— 所以内容放 .py 文件里写，
   不要走 bash -c 的 heredoc / 双引号字符串。
"""
import io
import os

DAY = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\2026-09-23.md'
MEM = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\MEMORY.md'

LOG = """

## 浏览器崩溃修复批次（23:00~23:55）

master 报：正常冲浪时崩了 —— 必应搜 ESP32 -> 点「下一页」没翻 -> 点进结果页 ->
再点里面一个东西，直接重启，画面还在但触摸全失效，断电才恢复。

### 根因 1：loopTask 栈溢出（不是内存不够）
复现拿到铁证（tools/verify_fix.txt / repro_crash.txt）：
    [Browser] fetch_task done: result=0 layout=0x3d90a1bc DRAM free=203660
    ***ERROR*** A stack overflow in task loopTask has been detected.
    Backtrace: ... |<-CORRUPTED
    rst:0xc (RTC_SW_CPU_RST)
DRAM 还剩 203KB —— 是栈爆了不是堆爆了。
loopTask 栈默认只有 8192 B，而 layout_engine 有 4 个全树遍历函数
（layout_tree_stats / layout_flatten_tree / layout_scale_tree /
layout_clamp_horizontal）把 **next_sibling 也写成了递归**，
于是递归深度 = 树的**节点总数**（几百上千）而非嵌套层数。
必应 356 节点侥幸过，乐鑫 977 节点直接炸。

修：① 那 4 个函数 sibling 改 while 迭代；② platformio.ini 加
-DARDUINO_LOOP_STACK_SIZE=32768；③ 剩余父子递归加 MAX_LAYOUT_DEPTH 64
（⚠️ layout_node_destroy 绝不能加深：截断 = 泄漏）；
④ 渲染后打印 loopTask stack 水位做永续预警。
效果：977 节点页面从必崩 -> 正常渲染，栈峰值 11108 B / 剩余 21660 B。

### 根因 2：伪链接被当真网页下载
乐鑫官网菜单里大量 href="javascript: void(0);"，被拼成
https://host/path/javascript: void(0); 后照抓不误 ——
服务器回一个 507KB 的 404 页，解析出 4780 节点，DRAM 从 207KB 掉到 72KB。
master 说的「加载到 64% 就关了」正落在下载阶段。
修：isPseudoUrl() 三处拦 —— link_click_cb、startFetch 入口、以及
layout_drop_junk 里直接不渲染（省 widget 配额）。
⚠️ 关键：这些 href 常被拼上域名前缀，**只看前缀拦不住，必须查子串**。
伪链接点击现在给 toast「此按钮需要 JavaScript」，不再静默。

### 顺带完成的功能（master 要求）
- 去掉百度，只留必应（百度 2.93MB + 反爬，无 JS 客户端不可用）
- 搜索首页「新闻源」下加「下载的网站」入口 -> 列表页：
  每行 = [名字+大小][删]，点名字离线重看（文件内容塞回页面缓存，
  startFetch 命中缓存不联网）；清空全部要二次确认（3 秒）
- 下载时把原 URL 写进文件第一行 <!--URL:xxx--> —— 文件名只有 hash，
  列表页靠它显示可读名字
- 页面缓存 3 槽 -> **2 槽**（当前页 + 上一页，硬上限）
- 设置屏加「浏览器缓存 / 已下载页面」两行 + 清理缓存 / 清理下载按钮
  （清下载要二次确认）

### 新发现的瓶颈（未修，待 master 拍板）
乐鑫这种站：**导航菜单本身是真链接**，162 个吃光 MAX_WIDGETS=200 的配额，
正文（id="main" 落在 54.9% 处）永远排不到号 —— 表现为「加载了但看不到内容」。
伪链接过滤只把 clickable 197->162、junkDropped 1->64，仍然打满 200。
且乐鑫**没有** <header>/<nav>/<main> 语义标签（全 div），靠标签跳过走不通。
真正的瓶颈：LV_MEM_SIZE 只有 128KB（实测用 71%），而 PSRAM 空着 7.3MB。
候选方案：LV_MEM_SIZE 128K->512K + MAX_WIDGETS 200->500；
或按 class 启发式（navbar/menu/sidebar）跳过导航容器。

### 两个安全规矩（代码里已注释，别再踩）
1. 自建 UI 的回调里不能重建列表/开页面 -> 一律走 tick 的延迟通道 g_uiPendingKind
2. LittleFS 挂载后**不许 end()** -> 会打挂正在跑的页面服务器；begin() 是幂等的

### 文档
docs/09-坑点速查表.md 新增 C8（loopTask 栈溢出）、F10（伪链接），
红线补 4 条。
"""

MEM_ADD = """
## 浏览器：崩溃与容量（2026-09-23 晚）

- **崩了先看是栈还是堆**：串口写 `A stack overflow in task loopTask` = **栈**，
  且 DRAM 还剩一大把。loopTask 默认栈只有 8192 B，现配
  `-DARDUINO_LOOP_STACK_SIZE=32768`。别去加 PSRAM。
- **遍历树的铁律：兄弟用迭代，父子才递归。**
  写成兄弟也递归 = 递归深度等于节点总数 = 必爆。
  `layout_node_destroy` 不许加深（截断=泄漏）。
- **链接不一定是 URL**：`javascript: void(0);` 这类 JS 菜单占位按钮在乐鑫官网
  有上百个。会**被拼上域名前缀**，所以判定必须查**子串**。
  不拦 = 一次下载 507KB、DRAM 掉 130KB；拦了还能省 widget 配额。
- **内存策略（master 定）**：页面缓存**只留 2 槽**（当前页+上一页），
  再多走下载（LittleFS）或设置里手删。下载列表入口在搜索首页「新闻源」下面。
- **当前最大瓶颈**：MAX_WIDGETS=200 被导航菜单吃光，正文排不到号；
  且 LV_MEM_SIZE 只有 128KB（PSRAM 空着 7.3MB）。待 master 拍板是否放宽。
"""

for path, text, mode in ((DAY, LOG, 'a'), (MEM, MEM_ADD, 'a')):
    raw = io.open(path, encoding='utf-8', errors='replace').read()
    nl = '\r\n' if '\r\n' in raw else '\n'
    if not raw.endswith('\n'):
        raw += nl
    io.open(path, 'w', encoding='utf-8', newline='').write(raw + text.replace('\n', nl))
    print('appended ->', os.path.basename(path), os.path.getsize(path))
