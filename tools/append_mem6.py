#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把今天必应 SERP 的结论追加进项目日志与长期记忆。"""
import io
import os
import sys

BASE = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040"
LOG = os.path.join(BASE, ".workbuddy", "memory", "2026-09-23.md")
MEM = os.path.join(BASE, ".workbuddy", "memory", "MEMORY.md")

LOG_ADD = """

## 必应 SERP：UA 决定一切（根因 + 修复，已上板验证）

master 报「搜索里为什么是 全部/24小时/一周内/一个月内/去年，点进去跳到很奇怪的地方，
正常只有『下一页』」。排查结论：不是"锁错"，是**必应对我们这个客户端按 UA 降级**。

- `www.bing.com` 会被地域 302 到 `cn.bing.com`（别拿 www 的测试推断设备）。
- UA 矩阵（cn.bing.com/search?q=esp32）：
  移动 UA（KitKat / Android13 / iPhone）→ 60KB、**5 条** li.b_algo、**0 个分页标记**；
  **桌面 Chrome120 → 100KB、10 条、有「下一页」**。→ 必应改走桌面 UA（百度仍 KitKat）。
- 「下一页」是**假分页**：链接带 `FPIG=<32位hex>` token；裸 first=11 / 带 FPIG /
  再加 cookie+Referer，三种方式与第 1 页相比**新增结果都是 0**。必应对无 JS 客户端
  只下发固定 ~10 条语料。仍保留该入口（官方入口），数字页码「2」「3」丢掉。
- 其它引擎全不可达：DuckDuckGo / Mojeek / searx / Brave / Marginalia 超时或无结果；
  360 351KB 且 pn= 无效；搜狗 296KB 的「下一页」是 JS 按钮；百度 2.7MB 超上限。
  → 只能在中文引擎里选，必应+桌面UA 是最优解。

改动（已编译烧录 + 设备实测）：
- `lvgl_renderer.cpp` 按域名选 UA；注释重写（旧的还写着 Android13，是错的）。
- `layout_engine.cpp` `flat_is_time_filter_link()` → `flat_is_serp_chrome_link()`：
  丢 `filters=ex1`（时间筛选）/ `qpvt=`（全部·时间不限）/ `FORM=HDRSC`（顶部导航）
  / 垂搜路径 / `first=` 且文本是 1~3 位纯数字（页码）；`FORM=PORE`（下一页）反向保留。
- 垃圾短语加：`切换到国际版` `时间不限` `搜索工具` `分页`。
- `dom_renderer.cpp` `li_has_block_children()`：nav/ul/ol/table 出现一个即判块级
  （分页 `<li class=b_pag>` 只有一个 nav，够不到 ≥2 门槛 → 整块被压成一行）。
- 诊断 dump 的 60 行上限改成运行时可调：串口 `flatdump <n>`（分页排在第 60 行之后，
  日志里看不到会误判成"没渲染"）。
- `browser_screen.cpp` `listSavedPages()`：服务器在跑时不再 `LittleFS.end()`
  （实测 serve 后敲 ls，再访问就 404）。

设备实测：junkDropped 5→18，结果 5→10 条，渲染出 `[1]` 和 `[下一页]`，
clickable links=17，DRAM free 246KB。详细数据见 docs/13 §10。
"""

MEM_OLD = """## 浏览器：平铺（tiling）+ 搜索优先（路线 D）"""
MEM_NEW = """## 浏览器：平铺（tiling）+ 搜索优先（路线 D）"""


def append_log():
    with io.open(LOG, "r", encoding="utf-8", newline="") as f:
        raw = f.read()
    if "UA 决定一切" in raw:
        print("[SKIP] 日志已写过")
        return
    with io.open(LOG, "a", encoding="utf-8", newline="") as f:
        f.write(LOG_ADD)
    print("[OK] 日志已追加")


def patch_mem():
    """在长期记忆里替换「默认引擎 = 必应」那一段，并新增诊断命令小节。"""
    with io.open(MEM, "r", encoding="utf-8", newline="") as f:
        raw = f.read()
    old = ("- **默认引擎 = 必应**：必应 SERP **59KB**、结果是静态 HTML（`li.b_algo` ×10），"
           "实测渲出 5 条真结果")
    new = ("- **默认引擎 = 必应，且必应必须走桌面 Chrome120 UA**：移动 UA 只给 **5 条**"
           "`li.b_algo` + 0 个分页标记（`first=` 被服务端忽略）；桌面 UA 给 **10 条** + "
           "真的「下一页」（~100KB）。百度/其它站点仍用 KitKat。\n"
           "  ⚠️ 必应的「下一页」是**假分页**：链接带 `FPIG=<hex>` token，但裸 first=11 / "
           "带 FPIG / 再加 cookie+Referer 三种方式结果都与第 1 页相同（必应对无 JS 客户端"
           "只给固定 ~10 条语料）。留着当入口，别指望翻出新东西。\n"
           "  ⚠️ `www.bing.com` 会被地域 302 到 `cn.bing.com`。\n"
           "  ⚠️ 其它引擎这个网络里全不可达（DDG/Mojeek/searx/Brave 超时）；"
           "360 351KB 且 pn= 无效、搜狗「下一页」是 JS 按钮、百度 2.7MB 超上限。")
    if old in raw:
        raw = raw.replace(old, new, 1)
        print("[OK] MEMORY.md: 必应段已更新")
    else:
        print("[WARN] MEMORY.md: 必应段锚点未命中，改为追加")

    anchor = "- **平铺跳过外部 CSS**"
    add = ("- **SERP 壳子过滤** `flat_is_serp_chrome_link()`：丢 `filters=ex1`（时间筛选）/ "
           "`qpvt=`/`FORM=HDRSC`（顶部导航）/ 垂搜路径 / `first=` 且文本是 1~3 位纯数字（页码）；"
           "`FORM=PORE`（下一页）反向保留。⚠️ `first=` **不能无条件删**：`<li>` 会继承子树 "
           "href，分页容器会被连坐掉、下一页跟着没。\n"
           "- **诊断**：串口 `flatdump <n>` 调平铺 dump 的行上限（默认 60；分页在第 60 行之后，"
           "看不到会误判成没渲染）。\n")
    if anchor in raw:
        raw = raw.replace(anchor, add + anchor, 1)
        print("[OK] MEMORY.md: 新增诊断/过滤小节")
    else:
        print("[WARN] MEMORY.md: 平铺锚点未命中")

    with io.open(MEM, "w", encoding="utf-8", newline="") as f:
        f.write(raw)
    print("[OK] MEMORY.md 已写回")


if __name__ == "__main__":
    append_log()
    patch_mem()
