# -*- coding: utf-8 -*-
"""docs/09：删掉被重复写入的 I11~I17 前一份，并追加 4 条新坑点（I18~I21）。"""
import io, re

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\docs\09-坑点速查表.md"
src = io.open(P, encoding="utf-8").read()

# ── 1) 去重：I11~I17 写了两遍，删掉**第一份**（到第二个 "### I11" 之前为止）
first = src.index("### I11 · 后台任务的停止标志")
second = src.index("### I11 · 后台任务的停止标志", first + 10)
# 退到 second 前面的那个空行，保证段落之间只有一个空行
cut = second
while cut > 0 and src[cut - 1] in "\r\n":
    cut -= 1
src = src[:first] + src[cut:]
print("dedup: removed %d chars" % (second - cut))

# ── 2) 追加新坑点：插到 "## J. 工具 / 环境" 之前
new = """### I18 · URL 拼进固定 buf 会被 snprintf 悄悄截断 → 服务端 400
- **症状**：天气加了 `hourly` 之后设备端一直 `HTTP 400`，
  而**同一个 URL 在 PC 上 200**（很容易误判成"对方接口不接受我的参数"）。
- **根因**：`makeWeatherUrl()` 的 `char buf[512]`，完整 URL 实测 **594 字节** →
  `snprintf` 静默截断，服务端收到半个参数（`forecast_hours=2`）直接 400。
- **动作**：buf 放大到 768，并在注释里写清楚实测长度。
- **教训**：设备端 400 / PC 端 200，**先怀疑 URL 被截断**，别急着改参数。
  任何"拼长 URL"的地方都要留 >= 实际长度 1.3 倍的余量。

### I19 · 屏的 create / tick / delete_cb 不能待在匿名 namespace 里
- **症状**：`error: 'g_dayDate' was not declared in this scope`（以及一串 g_*）。
- **根因**：`WeatherScreen_create()` / `tick()` / `scr_delete_cb()` 必须
  **外部链接**（nav.cpp、serial_console.cpp 要链接），所以放在 namespace 外；
  可它们要读写放在 `namespace { }` 里的 `g_*` 指针 → 外层看不见。
  反向做也不行：把 create 挪进 namespace 就变成内部链接 → 链接期 undefined reference。
- **动作**：`struct` / `constexpr` / 全部 `g_*` 提到 namespace **外面**并加 `static`，
  namespace 里只留纯内部辅助函数（也加 static），这样两边都不撞。
- ⚠️ 同一条规矩的另一种翻车方式见 I15（`g_uiAnim`）。
  一句话：**要被别的 .cpp 看见的一律放外面，只在本文件用的加 static。**

### I20 · LVGL 8.3 没有 `lv_obj_set_scroll_dir()`（8.4 才有）
- 想让"逐时天气"横向滚，调它直接 `error: 'LV_SCROLL_DIR_HOR' was not declared`。
- 而且就算有，在**竖向滚动的父容器里**嵌一个横向滚动容器，手指竖滑会先被
  子容器吃掉，下面的 7 日列表就滑不动了。
- 本项目选的做法：逐时那 24 格用 `LV_FLEX_FLOW_ROW_WRAP` 排成 8 列 × 3 行，
  整体跟着页面竖滑 —— 一次能看全，也不用跟手势打架。

### I21 · "编译成功"可能是陈旧 .o 的假阳性
- **症状**：先看到 `[SUCCESS]`，下一轮改别的才暴露出上一次根本没编的错。
- **动作**：改完必须核对 `.o` 与源文件 mtime（`FRESH` 才算过），
  并且 `grep error: build_sdkdef.log` 看计数。
- ⚠️ 改 `lv_conf.h` 后还要删 `.wb_build/.../lib51c` 逼 LVGL 重编（老规矩，见 C 章）。

"""
marker = "## J. 工具 / 环境"
i = src.index(marker)
src = src[:i] + new + src[i:]
io.open(P, "w", encoding="utf-8", newline="").write(src)
print("ok, lines =", src.count("\n"))
