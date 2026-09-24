"""写入第二轮优化（bpp1 + 透明短路 + 图标DRAM回退）的实测结论。"""
import io
import os

P = os.path.join(r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040",
                 ".workbuddy", "memory", "2026-09-24.md")

NOTE = """

## 第二轮：bpp 1 + 混合透明短路 + 图标 DRAM（含一次回退）

master 拍板"抗锯齿无所谓"后做的三项，收益依次递减：

### ① 字体 --bpp 1（已保留）
整字成本 **134 -> 108 us/字**（-19%），桌面 draw 30507 -> 30014。
收益远小于预期的"3~4 倍"，说明**剩余成本不在解包和 mix 计算**。
⚠️ 附带大礼：字体体积 3196KB -> 1125KB，**Flash 77.0% -> 60.9%**（省 537KB）。

### ② FILL_NORMAL_MASK_PX 加透明短路（已保留，改的是 vendored LVGL）
`Libraries/Lvgl/src/draw/sw/lv_draw_sw_blend.c`。
原宏只有 `mask==255` 快路径，**mask==0（透明像素）仍调用 lv_color_mix(color,dest,0)**
—— 结果恒等于 dest，纯浪费；汉字里大片是透明像素。
对比 `MAP_NORMAL_MASK_PX`（图片路径）早就写了 `if(*mask)` 短路，文字路径漏了。
语义等价（lv_color_mix(fg,bg,0)==bg），零观感影响。工具：`tools/blend_skip_transp.py`。

### ③ 图标 canvas 缓冲搬内部 DRAM —— ❌ 已回退
图标 buf 原本 `heap_caps_malloc(MALLOC_CAP_SPIRAM)`，绘制时每像素一次片外读
（实测 11 个图标 7962us = 1488 ns/px）。改 `MALLOC_CAP_INTERNAL` 后：
- 收益：canvas 7962 -> 5716us，**draw 只省 2ms**
- 代价：**DRAM 109488 -> 63472（-46KB）**
⛔ 根因：`MAX_BARS=12` 个顶栏槽位 × 每槽 4 个图标 × 22×22×2B=968B ≈ 46KB，
  全部预分配。2ms 不值得让 DRAM 见底（WiFi 要 38KB、浏览器 fetch 要 16KB）。
**判据：改内存归属前先看槽位数，别只看单个对象大小。**

### 关键结构性发现：帧时间与脏区面积线性
| 脏区 | 整帧 | fps |
|---|---|---|
| 480 行 | 48.8ms | 20.5 |
| 240 行 | 28.5ms | 35.1 |
| 120 行 | 13.2ms | 75.5 |
| 60 行 | 7.5ms | 132.5 |
| 30 行 | 5.6ms | 177.6（其中 draw 4.2ms 是固定开销） |

=> **没有固定瓶颈，纯按像素计价**。所以"减少重绘面积"和"减少每像素成本"是唯二杠杆。
`perf`（全屏）是最坏值，不是日常体验 —— 静止时 fps=0（无脏区根本不绘制）。

### PSRAM 带宽已无优化空间
板级 `f_flash=80MHz`，PSRAM OPI 80MHz 已是默认最高。实测 18~30MB/s 远低于
理论 160MB/s，差额被 **LCD GDMA 持续扫屏**（480×480×2×60Hz ≈ 27MB/s）吃掉。
这是架构必然，别再往"提频""换 DMA"方向使劲。

### 下一步唯一看得到量级提升的路：LV_COLOR_DEPTH 16 -> 8
framebuffer 460KB -> 230KB：① LCD 带宽占用减半 ② flush 数据量减半（~10ms）
③ 230KB 有机会放进内部 DRAM → draw 也变快。
代价：8bpp 要调色板、颜色数 256，画质有损。**待 master 拍板。**
（direct mode 不算：draw 在 PSRAM 上会涨到 ~43ms，抵消掉省的 20ms flush，净收益很小。）

### 顺手核对的两条过时记忆
- MEMORY 里"字体不含 ASCII、靠 fallback 兜底" —— **已过时**。
  `font_symbols_16.txt` 3927 字里**含全部 95 个 ASCII**，所以 `.fallback=NULL` 是正常的。
- MEMORY.md 已膨胀到 235 行、被截断注入 → **本轮已精简重写**（只留结论+指针，细节推 docs/日志）。

### 待查（非性能）
- **NTP 三个服务器全超时**（aliyun / pool.ntp.org / time.google.com）。
  配置和兜底逻辑都对，怀疑是网络侧（WiFi 未连或 DNS 被墙）。顶栏时间可能不准。
"""

s = io.open(P, "r", encoding="utf-8", newline="").read()
io.open(P, "w", encoding="utf-8", newline="").write(s + NOTE)
print("APPENDED", len(s), "->", len(s) + len(NOTE))
