"""追加性能专题到项目日志。

注意：不要试图在 bash -c 的 python 里写含反引号的中文文本 —— 反引号会被
shell 当命令替换吃掉（本项目已踩过）。老老实实写成 .py 文件再跑。
"""
import io, os

p = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\2026-09-24.md'
s = io.open(p, 'r', encoding='utf-8', newline='').read()

add = """

## 性能专题（master 指定为第一优先）：13.6 fps -> 17 fps，根因已钉死

### 先量后改：两个新诊断命令（已烧进固件）
- `perfbw` —— 带宽体检。perf 只告诉你 draw/flush 各占多少，但 flush 那一半
  **到底是什么在耗时**（软件拷贝？DMA？等 vsync？）必须单独量。
- `perfx hide <label|img|canvas>` / `perfx show` —— 按控件类型隐藏后跑 perf，
  用**差值**反推 draw 花在哪类控件上。比猜快得多。
- 批量采集 `tools/perf_sweep.py`（逐屏 nav + perf，写 perf_sweep.out.txt）。
- 串口命令靠 `tools/serial_cmd.py COM7 "<cmd>"` 发（pyserial 只在系统
  python-sdk 3.13.2 里有，managed python 没有）。

### 最重要的发现：flush 那 26ms 根本不是在"推屏"
perfbw 实测（460800 B = 一整屏）：
| 项目 | 耗时 | 带宽 |
|---|---|---|
| **5 gfx draw16bitRGBBitmap（真实 flush 路径）** | **25481 us** | 18.1 MB/s |
| 1 memcpy PSRAM->PSRAM | 25065 us | 18.4 MB/s |
| 2 memcpy DRAM->PSRAM | 15626 us | 29.5 MB/s |
| 3 memset PSRAM（纯写） | 16394 us | 28.1 MB/s |
| 6 fill32 写 PSRAM | 15333 us | 30.1 MB/s |
| **7 fill32 写内部 DRAM** | **2442 us** | **188.6 MB/s** |

第 5 项和第 1 项**完全同速** => dispFlush 里 gfx->draw16bitRGBBitmap()
本质是 **PSRAM -> PSRAM 的 memcpy**：LVGL 的 dispBuf 在 PSRAM，panel 的
framebuffer 也在 PSRAM（flags.fb_in_psram=1 是 Arduino_GFX 硬编码的），
460800 B 一读一写全走 OPI，还被 LCD 的 GDMA 抢走一半带宽。
**跟 PCLK、DMA、vsync 都没关系** —— 别再往那个方向使劲。
（480 行 x 548 clk/行 @10MHz = 26.3ms，跟实测 26.6ms 太像，极易误判成
"PCLK 带宽下限"；其实是拷贝被 GDMA 拖慢到刚好这个数，纯属巧合。）

### 三个方案实测对比
| 方案 | launcher 整帧 | draw | flush | 画面 |
|---|---|---|---|---|
| 原始（PSRAM 双缓冲 120 行） | 73582 us (13.6 fps) | 46972 | 26610 | 正常 |
| direct mode（draw_buf = framebuffer） | 43167 us (23.2 fps) | 43218 | ~0 | **撕裂闪烁** |
| **内部 DRAM 双缓冲 60 行（现用）** | 58720 us (17.0 fps) | 38590 | 20130 | 正常 |

- 内部 DRAM 只够摆 2 块 60 行（57600 px = 115200 B），不够整屏也够用，
  LVGL 自动拆 8 批 draw+flush。DRAM free 从 224KB 掉到 ~109KB。
- direct mode 收益最大但**画面撕裂**：LVGL 边画边被 GDMA 扫出去。
  代码里留着（g_directMode）做对照，**默认不开**。
  只有 draw 压到 <26ms（屏扫一帧的时间）才可能不撕裂 —— 后续目标。

### draw 的构成（perfx 差分，桌面 draw=42409 us）
| 藏掉什么 | draw | 该部分成本 |
|---|---|---|
| 11 个 canvas 图标 | 29348 | 13061 us（31%） |
| 再藏 11 处 label 文字 | 4292 | 25056 us（**59%**） |
| 剩下（背景/容器/圆角） | 4292 | 10% |

=> **文字是大头，图标次之**，背景容器几乎不要钱。
- 图标已改：canvas 从 LV_IMG_CF_TRUE_COLOR_ALPHA(32bpp 逐像素 alpha 混合)
  改成 LV_IMG_CF_TRUE_COLOR(16bpp 不透明)，缓冲也减半。
  draw 42409 -> 38590（省 3.8ms，比差分的 13ms 少：不透明后 blit 本身还在）。
  副作用：图标外框不再透明而是**不透明黑色**，非黑底处会露黑方块，待确认。
- 文字 25ms 还没动，是下一个目标。

### 顺手挖出并修掉的实机崩溃（浏览器后台任务竞态）
栈：back_async_cb -> nav_back_home -> nav_release_all_except -> lv_obj_del(browser)，
崩在 lv_obj_get_screen（爬 parent 链）=> 对象树已被写坏。
真因：fetch 任务跑在**另一个任务**里却直接操作 LVGL 对象（显示/隐藏
g_loadingOverlay、往内容容器塞 widget）。BrowserScreen_close() 只丢了个
g_stopRequested=true 就立刻 contentReset() + 让 nav 去 lv_obj_del ——
树拆了，任务手里全是悬空指针，它再写一次就把树写坏，**崩在下一次遍历**，
所以栈看着像"删 clock 崩"，clock 其实无辜。
修：close() 里轮询等 g_state 离开 LOADING（上限 300x10ms）；
另加 BrowserScreen_isBusy() 给 nav 当 canRelease 守卫（跟 wifi 同款）。

### 待办
- 攻文字渲染（59%）：可能要动字体 bpp（抗锯齿 4bpp -> 1bpp），影响字形观感，需拍板。
- 编译优化：Arduino 默认 -Os，改 -O2/-O3 零风险可能 10~30%。
- draw 压到 <26ms 后重开 direct mode，目标 ~20ms/帧（50 fps）。
- 待 master 确认：不透明图标在非黑底上有没有露黑方块。
"""

io.open(p, 'w', encoding='utf-8', newline='').write(s + add)
print('APPENDED %d -> %d' % (len(s), len(s) + len(add)))
