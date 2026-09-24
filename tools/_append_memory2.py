# -*- coding: utf-8 -*-
import io, os
D = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory"

note = """

## 「切到后台就被自动结束」—— 定位并修掉（2026-09-25）

master：「从应用切入后台之后，我无论点不点结束，它都会帮我结束掉」。

根因（串口新增 `navlist` / `back` 两个诊断命令实锤）：
```
navlist -> running=2 (clock 前台, weather 8628B)
back    -> release clock / release weather     ← 一次返回把**所有**都清了
navlist -> running=0
```
`nav_back_home()` / `nav_back_home_anim()` 里调 `nav_release_all_except(nav_launcher)`，
回桌面那一刻就把 launcher 以外的 Activity 全 release 了 —— 所以后台管理里
点不点"结束"都一样。息屏唤醒那条路只 lv_scr_load 不释放，不是它。

改法：回桌面 = 切后台，只切屏**绝不销毁**；内存由 `nav_open()` 之前的
`trimBackground()` 按 LRU 兜底（后台 > 4 个 或 DRAM < 48KB 时回收最久未用的；
前台/正要开的/canRelease 守不过的一律不动）。Launcher 进浏览器仍独占（太重）。
验证：back 后仍 running=3，再开 memory 变 4，DRAM 87KB 稳。

## ⚠️ 我自己写错过一次根因（记下来别再犯）

天气页编译报"一串 g_xxx was not declared"时，我判断成"匿名 namespace 里的符号
外面看不见"，还据此把一批变量搬出 namespace —— **错的**。硬反证：nav.cpp 的
`nav_open()` 就在 `}  // namespace` 之后，照样调 namespace 内的 `find()`，
一直编译通过。匿名 namespace ≈ `namespace unique {} + using namespace unique;`，
**关闭后仍能用非限定名访问**。
真因是 `constexpr DAY_N` 被我挪到了 `g_dayDate[DAY_N]` 使用之后 → 数组声明整条
失效 → 连锁爆一片。教训：看到"一串变量未声明"，先查它们依赖的常量/类型的
**声明顺序**，别急着怪 namespace / 链接性。docs/09 的 I19 已改写。
"""

io.open(os.path.join(D, "2026-09-25.md"), "a", encoding="utf-8").write(note)
print("ok")
