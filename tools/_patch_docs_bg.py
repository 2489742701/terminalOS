# -*- coding: utf-8 -*-
"""docs/09 追加 I22（回桌面清后台），并更新 README 的后台管理说明。"""
import io

D = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"

p = D + r"\docs\09-坑点速查表.md"
src = io.open(p, encoding="utf-8").read()

new = """### I22 · 回桌面 ≠ 退出：`nav_back_home()` 曾经把所有后台都清了
- **症状**（master 原话）：「从应用切入后台之后，我无论点不点结束，它都会帮我结束掉」。
- **根因**：`nav_back_home()` / `nav_back_home_anim()` 里调
  `nav_release_all_except(nav_launcher)` —— **回桌面那一刻就把 launcher 以外的
  所有 Activity 全 release 了**，不只是刚离开的那个。所以后台管理里点不点"结束"
  都一样：应用早没了。串口实锤（`navlist` / `back` 命令）：
  `running=2 (clock, weather)` → `back` → `release clock` + `release weather` → `running=0`。
- **改法**：回桌面 = 切后台，只 `lv_scr_load(nav_launcher)`，**绝不销毁**；
  内存压力交给 `nav_open()` 之前的 `trimBackground()` 按 LRU 兜底
  （后台 > 4 个、或 DRAM < 48KB 时，回收最久没进前台的那个；
   前台 / 正要开的 / canRelease 守不过的一律不动）。
- 仍然**独占**的场景：Launcher 进浏览器照旧
  `nav_release_all_except(nav_launcher, nav_browser)` —— 它太重，别跟别人挤。
- 诊断命令：`navlist`（列还在内存里的 Activity）、`back`（走真实返回桌面路径）。

"""
marker = "## J. 工具 / 环境"
i = src.index(marker)
src = src[:i] + new + src[i:]
io.open(p, "w", encoding="utf-8", newline="").write(src)
print("docs ok")

r = D + r"\README.md"
t = io.open(r, encoding="utf-8").read()
old = "- **动画**"
if "后台管理" in t:
    print("README has 后台管理 already")
else:
    print("README: check 后台管理 section manually")
