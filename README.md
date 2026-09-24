# Geek Terminal

ESP32-S3 上的黑底白线图标小系统：Launcher + 时钟 + 设置三屏，带息屏 / 锁屏（上滑解锁）。

**硬件**：ESP32-S3（16MB Flash / 8MB OPI PSRAM） + 4.0" 480×480 ST7701S RGB 并行屏 + GT911 触摸
**平台**：PlatformIO，`espressif32@^6.0.0` + `framework = arduino`（Arduino core 2.x / IDF 4.4）
**串口**：COM7 @ 115200

---

## 文档

| 文档 | 内容 |
|---|---|
| [`00` 项目上手指南](docs/00-项目上手指南.md) | **第一次接手先读这篇**：目录结构、模块职责、构建烧录、串口命令全集、常用脚本、文档地图 |
| [`01` 板级硬事实与构建烧录](docs/01-板级硬事实与构建烧录.md) | **改构建配置前必读**：Flash 必须 DIO、PSRAM 必须 OPI、⛔ 禁 16MB 分区表、引脚定义、构建/烧录/抓日志命令、已知无害告警 |
| [`02` 显示：花屏与 ST7701S](docs/02-显示-花屏与ST7701S.md) | 刷新瞬间花屏的根因（PSRAM 带宽争抢 / LCD DMA 饿死）、有效修复、已排除嫌疑、社区方案可行性 |
| [`03` 浏览器架构与生命周期](docs/03-浏览器架构与生命周期.md) | 两阶段异步渲染、Lexbor CSS 崩溃绕法、内存账本、Activity 懒创建与回收 |
| [`04` 浏览器排版与渲染](docs/04-浏览器排版与渲染.md) | **改排版前必读**：为什么放弃还原 CSS 改走平铺、元素莫名消失的取证、UI 坐标与整页缩放 |
| [`05` 搜索与 SERP](docs/05-搜索与SERP.md) | 自建搜索首页、UA 与 SERP 实测（必应必须桌面 UA）、壳子过滤、引擎横评 |
| [`06` 中文字体](docs/06-中文字体.md) | **改字体前必读**：字体分工、可复现的生成流程、sparse cmap 相对偏移陷阱 |
| [`07` 排障方法论](docs/07-排障方法论.md) | bootloop 怎么查、反汇编 bootloader、极简工程二分、`auto_del` + 常驻 `lv_timer` 实战取证 |
| [`08` UI 约定与产品语义](docs/08-UI约定与产品语义.md) | **改 UI 前必读**：三点键/首页键的行为约定、屏幕分区坐标、底栏两组互斥、状态栏（真实 FPS / WiFi 图标） |
| [`09` 坑点速查表](docs/09-坑点速查表.md) | **按症状反查的最快入口**：开不了机/显示/崩溃/内存/构建/排版/字体/网络/生命周期/工具 十类 |
| [`10` 项目长期记忆](docs/10-项目长期记忆.md) | 结论速记（跨会话接手用）+ 待办清单 |
| [参考项目分析](docs/archive/参考项目分析.md) | 历史调研：MicroPythonOS Activity/Intent、First-Start 同硬件 BSP |
| [早期项目知识库](docs/archive/早期项目知识库.md) | ⚠️ 历史留档，**含过时内容**（其分区表章节有害，勿照做），精华已淘到其他篇 |

**⚠️ 动任何显示 / 构建配置之前，先读 `docs/02` 的「板级硬事实」** —— 那几条改错就直接开不了机。

---

## 三条最容易踩的硬事实

1. **Flash 模式 = DIO，PSRAM 线模式 = OPI，两条必须同时成立**（缺一开不了机）。
2. **PCLK = 10MHz，不要改回 16MHz**（改回去会刷新花屏，见 `docs/01`）。
3. **不要删 `.pio`**（会触发依赖库重下 + safe-delete 拦截，环境崩）。重编只删 `.wb_build`。
4. **编译后务必确认真的编进去了**（全量 533 行 `Compiling`；若为 0 说明 PIO 静默跳过，
   烧的还是旧固件）。⚠️ 查之前先确认看的是**正确的日志**（`build_run.py` 写的是
   `build_sdkdef.log`，不是 `pio_run2.log`）；最可靠的判据是对比 `.o` 与 `.cpp` 的 mtime。
   详见 `docs/02` §6 / §6.1。
5. **运行时不要调 `heap_caps_get_largest_free_block()` / `heap_caps_get_info()`**：
   O(n) 遍历整堆持锁，撞上 WiFi 收包直接 wdt 重启（addr2line 实证，见 `docs/06` §6.2）。
6. **不要写 `board_build.flash_size = 16MB` / `partitions_16MB.csv`**（板子物理 16MB，但本工程
   编译出的 bootloader 只认 8MB）→ 分区越界会让 bootloader 进 `while(1)` 死循环，
   表现为 **`rst:0x3` 无限重启且无任何 panic 输出**，极具迷惑性。详见 `docs/02` §1.4。

---

## 快速开始

```bash
# 1) 清构建目录（必须！否则 PIO 可能静默跳过编译，见 docs/02 §6）
"C:/Users/longyaosi/.workbuddy/binaries/python/versions/3.13.12/python.exe" \
  -c "import shutil; shutil.rmtree('.wb_build', ignore_errors=True)"

# 2) 编译（约 100 秒，日志里应有 533 行 Compiling）
"C:/Users/longyaosi/python-sdk/python3.13.2/python.exe" -m platformio run -d . -e esp32s3

# 3) 烧录（flash_mode 必须 dio）
#    注：命令行这个 --flash_size 16MB 指"物理 flash 容量"，无害；
#    真正有害的是 platformio.ini 里的 board_build.flash_size / partitions_16MB.csv（见硬事实 6）
"C:/Users/longyaosi/python-sdk/python3.13.2/python.exe" \
  "C:/Users/longyaosi/.platformio/packages/tool-esptoolpy/esptool.py" \
  --chip esp32s3 --port COM7 --baud 921600 --before default_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 .wb_build/esp32s3/bootloader.bin \
  0x8000 .wb_build/esp32s3/partitions.bin \
  0x10000 .wb_build/esp32s3/firmware.bin
```

串口工具：

```powershell
# 抓启动日志（会复位设备）
python tools\cap_log.py
# 发单条命令并读响应（不复位）
python tools\serial_cmd.py COM7 "browser https://m.baidu.com" 60
# 生命周期压测：进入→加载→退出→再进入，全程记录内存
python tools\verify_flow.py COM7 "https://m.baidu.com"
```

成功的串口标志：

```
[Display] init ok
[Touch] init ok
[App] buf pixels=57600 bytes=115200 ptr=0x3d8712dc   ← 0x3d... 是 PSRAM 段，说明缓冲分配正确
[App] init ok
[BOOT] Geek Terminal ready
```

---

## 当前功能

- Launcher / 时钟 / 设置 / 天气 / WiFi / 内存 / 系统信息 / 游戏 / 画板 多屏导航，黑底 + 白色线条图标
- 息屏状态机（`src/app/screensaver.cpp`）：ACTIVE（亮）→ DIM（PWM 15% 暗显时间）→ OFF（关背光）
  - DIM 态上滑 ≥120px 解锁；OFF 态触摸回 DIM
- **浏览器**（`src/app/browser_screen.cpp` + `src/browser_engine/`）：HTML → Lexbor 解析 → 平铺排版 → LVGL 渲染
  - 平铺排版（不建容器，避免 CSS 坐标压盖）、链接可点、搜索首页（必应/百度切换 + 常用词胶囊）
  - 页面缓存：URI→HTML 存 PSRAM，TTL 5 分钟，3 槽 LRU，命中则跳过下载
  - 下载当前页到 LittleFS（`dl` 或底栏下载键）+ 内置 HTTP 服务（`serve`），PC 浏览器访问设备 IP 看原始 HTML
- NTP 校时（`src/hal/ntp_time.cpp`，手写避开 `Time` 库）、背光 PWM（LEDC，PIN_BL=38）

## 待办

- [ ] **中文输入法**（拼音 IME）—— `lv_keyboard` 只有 ASCII，现在中文只能点预设胶囊
- [ ] SERP 广告过滤（广告/推广/sponsored）
- [ ] 页脚备案/隐私/条款等垃圾一并丢掉
- [ ] emoji 仍是豆腐块
- [ ] 触摸坐标偏移；上滑时左下角一条白线
- [ ] 蓝牙接入（现为占位）
- [ ] 新闻源：等 master 给源再填（首页已留占位框）

> 完整清单与背景见 [`docs/00` §7](docs/00-项目上手指南.md)。

---

## 协作方式

**给 AI / 协作者的工作契约见 [`AGENT.md`](AGENT.md)** —— 排查方法论、本项目红线、
知识落盘约定。开工前读完 `AGENT.md` + `docs/00`。
