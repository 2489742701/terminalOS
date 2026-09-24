#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 2026-09-23 必应 SERP 的实测结论追加到 docs/13 §10。"""
import io
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
P = os.path.join(ROOT, "docs", "13-搜索优先的小引擎方案.md")

SEC = """
---

## 10. 必应 SERP 实测：UA 决定一切（2026-09-23）

### 10.1 现象

master 报："搜索里为什么是「全部 / 24小时 / 一周内 / 一个月内 / 去年」？点进去跳到很奇怪
的地方。正常只有「下一页」是必应的搜索页 —— 是不是『下一页』有特殊的『第五』没被锁到？"

不是"锁错"，是**必应对我们这个客户端做了降级**，而且降级的开关就是 UA。

### 10.2 域名矩阵（先排掉一个假线索）

`www.bing.com` 会被**地域 302** 到 `cn.bing.com`。所以匹配 UA 的域名写 `bing.com`，
两个都覆盖；别拿 `www` 的测试结果去推断设备的行为。

### 10.3 UA × 结果数 × 分页（cn.bing.com/search?q=esp32，PC 复现）

| UA | 体积 | `li.b_algo` | 分页标记 | `first=` 链接 |
|---|---|---|---|---|
| KitKat / Chrome30（移动） | 60 KB | **5** | 0 | 0 |
| Android 13 / Chrome120（移动） | 60 KB | **5** | 0 | 0 |
| iPhone / Safari17（移动） | 180 KB | **5** | 0 | 0 |
| **桌面 Chrome 120** | **100 KB** | **10** | 有 | **3** |

**结论：必应必须换桌面 UA。** 移动 UA 拿到的是阉割版 —— 5 条结果、HTML 里零个分页
标记、`first=` 参数被服务端直接忽略。桌面 UA 给 10 条 + 真的「下一页」。

体积从 60KB 涨到 ~100KB 完全可接受（平铺模式本来就跳过外部 CSS，不会变成几十次 TLS）。
百度仍走 KitKat（桌面 UA 会撞 wappass 验证码），所以 UA 是**按域名选**的。

### 10.4 「下一页」其实是假的（重要）

带桌面 UA 后，分页链接长这样：

```
<a href="/search?q=esp32&FPIG=<32位hex>&first=11&FORM=PERE">2</a>
<a href="/search?q=esp32&FPIG=<32位hex>&first=21&FORM=PERE1">3</a>
<a title="下一页" href="/search?q=esp32&FPIG=<32位hex>&first=11&FORM=PORE">下一页</a>
```

`FPIG` 是必应从上一页下发的 token。三种翻页方式全试过：

| 方式 | 与第 1 页相比的新增结果 |
|---|---|
| 裸 `&first=11`（不带 FPIG） | 0 |
| 带 FPIG + `first=11` | 0 |
| 带 FPIG + cookie 会话 + `Referer` + `first=11` | 0 |

**必应对无 JS、无历史 cookie 的客户端只下发一份固定的 ~10 条语料**，翻页链接会返回，
但内容几乎不变。所以「下一页」是官方入口、留着给用户翻，但别指望它翻出新东西。
数字页码「2」「3」纯装饰，直接丢。

### 10.5 其它引擎：都不可用

| 引擎 | 结果 |
|---|---|
| DuckDuckGo `html/` `lite/` | 本机网络**超时**（不可达） |
| Mojeek / searx.be / searx.tiekoetter / priv.au / Brave | 全部**超时**（不可达） |
| Marginalia | 可达，但搜不到东西 |
| 360 `m.so.com/s?q=` | 351 KB，14 条；`pn=` 被忽略，无「下一页」 |
| 搜狗 `m.sogou.com` | 296 KB；「下一页」是 JS「加载更多」按钮，不是链接 |
| 百度 `m.baidu.com/s` | 2.7 MB，超 786KB 上限 |

**结论：只能在中文引擎里选，且没有一个能提供真分页。** 必应 + 桌面 UA 是最优解
（100KB / 10 条 / 有官方入口）。

### 10.6 已落地的过滤（`flat_is_serp_chrome_link()`）

SERP 壳子不是搜索结果，却在平铺里全变成胶囊，点了还跳到重复页。丢这些：

| 判据 | 对应壳子 |
|---|---|
| `filters=ex1` | 24小时 / 一周内 / 一个月内 / 去年 |
| `qpvt=` | 「全部」「时间不限」「网页」标签 |
| `FORM=HDRSC` | 顶部导航：图片 / 视频 / 学术 / 词典 / 航班 |
| `/images/search` `/videos/search` `/academic/search` `/dict/search` `/travel/search` `/maps/` | 垂搜入口 |
| `first=` **且文本是 1~3 位纯数字** | 数字页码「2」「3」 |
| `FORM=PORE` | ⚠️ **反向白名单：永远保留**（这是「下一页」） |

垃圾文本短语另加：`切换到国际版` `时间不限` `搜索工具` `分页`。

### 10.7 三个连带踩到的坑

**① 容器连坐。** `<li>` 会从子树继承 `href_resolved`（`dom_renderer.cpp` 的
`subtree_first_href()` 只对 li 做，为了让搜索结果整条可点）。分页容器
`<li class="b_pag">` 因此也带着 `first=11`，**无条件按 `first=` 删会把它整块连坐掉**，
「下一页」跟着消失。修法：`first=` 只在**文本是 1~3 位纯数字**时才删。

**② `li_has_block_children()` 的门槛。** 分页 `<li>` 只有一个 `<nav>` 子元素，够不到
「≥2 个块级子元素」，于是整块文本被抽成一行 `- 分页123下一页`。修法：`nav/ul/ol/table`
这种结构容器出现一个就判定为有块级子元素。

**③ 诊断 dump 有 60 行上限。** `[Flat]` 日志写死 `s_widgetCount <= 60`，排在后面的行
（比如分页）永远打不出来，会误判成"没渲染"。已改成运行时可调：串口 `flatdump <n>`。

### 10.8 设备实测（改完之后）

```
[Browser] read 102562 bytes
[Browser] layout: FLAT tiles, nodes=251 junkDropped=17
... 10 条结果 ...
[Flat]  56 [1]          ← 当前页
[Flat]  57 [下一页]      ← 官方翻页入口
[Browser] widgets created: 63 (limit 200), clickable links=28
```

时间筛选那排、顶部导航那排全部消失；结果从 5 条变 10 条。
"""


def main():
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        raw = f.read()
    if "## 10. 必应 SERP 实测" in raw:
        print("[SKIP] §10 已存在")
        return 0
    out = raw.rstrip("\r\n") + "\n" + SEC
    with io.open(P, "w", encoding="utf-8", newline="") as f:
        f.write(out)
    with io.open(P, "r", encoding="utf-8", newline="") as f:
        back = f.read()
    ok = "## 10. 必应 SERP 实测" in back
    print("[%s] 追加 docs/13 §10" % ("OK" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
