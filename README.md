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

- **中文拼音输入法**（`src/app/ime_pinyin.c`）：384 音节 / 2000 常用字，Flash 常量表二分查；
  键盘上方候选条，监听 textarea 尾部 ASCII 拼音，上屏延迟一拍
- **热点新闻**（`src/app/news.cpp`）：news.orz.ai 拉取，复用浏览器常驻 fetch 任务
- **应用后台管理**（`src/app/taskmgr_screen.cpp`）：应用不常驻、按需启动，可逐个 / 全部关闭；
  顶栏电池区可点进入。
  ⚠️ **回桌面 = 切后台，不是退出**（2026-09-25 修）：`nav_back_home()` 早期会
  `nav_release_all_except(launcher)` 把**所有**后台一起清掉，于是"点不点结束都会被结束"。
  现在回桌面只切屏不销毁，内存由 `nav_open()` 前的 `trimBackground()` 按 LRU 兜底
  （后台 > 4 个或 DRAM < 48KB 时回收最久未用的那个）。Launcher 进浏览器仍独占（它太重）
- **顶栏后台指示**：桌面顶栏左侧（电池右边）把真在跑的应用**图标一字排开**，
  最多 7 个；放不下时留一格显示 `+N`（如 8 个后台 = 6 图标 + `+2`）。
  点任意一个图标 = 进后台管理。没有后台时整行不显示
- **统一滑动返回**：**左右滑动退出是全页通用的**（任意起手点，滑够 48px，左滑右滑都算）；
  **上下滑动退出按页面开关**（浏览器这种要滚动的页面关掉，竖向只认上下边缘起手）。
  设置页例外：返回的是**上一级菜单**，不是直接回桌面。
  贪吃蛇 / 2048 / 画板 / 触摸测试**仍然只认左边缘**（滑动本来就是它们的操作）
  —— 见 `nav.h::swipe_back_detect`（边缘版）/ `swipe_back_any`（通用版）
- **动画**：2048 走子弹跳、返回带动画、设置二级菜单左右滑入；
  「显示与亮度 → 动画效果」可关，**重启生效**，默认开（全局开关 `g_uiAnim`）
- **TF/microSD**（`src/hal/sd_card.cpp`）：独立 SPIClass(HSPI) 实例，懒挂载
- **天气 + IP 定位**（`src/hal/geoip.cpp`）：open-meteo 取天气，ip.sb / ip-api.com 定位，按实际位置显示。
  ⚠️ 抓取走**常驻后台任务**（`weather` task），不占 UI 线程 —— 早期版本把 HTTPS 放在 tick 里同步跑，
  最坏要卡 24 秒；任务里只填数据结构，贴标签由 tick 做。
  页面分两段：上半屏固定（城市 / 温度 / 描述 / 体感），**下半屏可下滑**，
  依次是「当前实况」16 项网格 →「未来 24 小时」8 列 × 3 行 →「近 7 日预报」（温度区间 +
  降水概率 / 降水量 / 风 / 紫外 / 日出日落）。一次请求 ~3.0KB，把 open-meteo
  能给的 current 全字段 + daily 7 日 + hourly 24 条基本都摊在屏幕上了

## 待办

- [x] SERP 广告过滤（广告/推广/sponsored）—— 2026-09-24，`layout_engine.cpp::flat_is_ad_text`
- [x] 页脚备案/隐私/条款等垃圾 —— 同上，`flat_is_footer_text`。两者都带**长度闸门**防误杀正文
- [x] emoji 豆腐块 —— 2026-09-24，给 vendored `lv_draw_sw_letter.c` 打补丁：
      emoji 区码位静默跳过；**中文缺字仍保留方框**（那是"字库里没有"的信号）
- [~] 触摸坐标偏移 —— 校准框架齐了（两点线性映射 + `traw`/`tcal`/`tprobe`/`tswap`），
      并加了**触摸测试屏**（画板底部「触摸」按钮进，或串口 `nav touchtest`）：
      十字准星 + 四角靶 + 翻转X / 翻转Y / 交换XY 一键试，抬手时串口打
      `[TouchTest] screen=(x,y) raw=(rx,ry)`。端点默认仍是 0..480，**待上板量四角后填真值**
- [x] 上滑时左下角白线 —— **已确认修好**（master 上板验证：白线没了）。
      根因就是 LVGL 默认 `LV_SCROLLBAR_MODE_AUTO` 画的滚动条：
      `desktop_screen.cpp` 早已显式 OFF，`browser_screen.cpp` 的 `g_content` 漏了。
      📌 任何可滚动容器都要显式设 `LV_SCROLLBAR_MODE_OFF`，暗色 UI 上默认样式是浅色条。
- [~] 设置页改成「手机 OS 式二级菜单」—— **已铺满 7 页 / 37 项**（2026-09-24）。
      新增数据驱动的列表渲染器 `src/app/settings_menu.cpp`：**加一项 = 加一行数组元素**，
      不再是手摆绝对坐标。二级菜单 = **同一个屏 + 数据源栈**（`s_stack[4]`），
      不是每级建一个 Activity（否则 nav 表撑爆 + 每页多一棵常驻树吃 DRAM）。

      | 页 | 项数 | 内容 |
      |---|---|---|
      | 根 | 5 | 显示与亮度 / 网络与连接 / 浏览器设置 / 系统设置 / 关于本机 |
      | 显示与亮度 | 4 | 亮度、**息屏超时（滑块 0~600 秒自定义，0 = 常亮）**、立即息屏、桌面图标 |
      | 网络与连接 | 5 | WiFi（跳屏）、连接状态、IP、信号强度、定位城市 |
      | 浏览器设置 | 6 | 排版视口、页面服务器、页面缓存、已下载页面、清理缓存/下载 |
      | 系统设置 | 6 | 时间源、自动校时、校准时间、**触摸校准**（子页）、系统信息、后台管理 |
      | 触摸校准 | 6 | 当前参数、翻转X / 翻转Y / 交换XY / 恢复默认、触摸测试 |
      | 关于本机 | 5 | 固件、芯片、屏幕、空闲内存、运行时长 |

      📌 **分组口径**（加项前先读 `settings.cpp` 头注释）：改了它，影响范围是
      **屏 / 网络 / 浏览器 / 系统** 的哪一个？拿不准先放系统设置，别为凑组硬塞。
      ⛔ 三条铁律见 [`docs/09` §I5](docs/09-坑点速查表.md)；诊断：串口 `setpage <id>` / `setpage back`。

      📌 **息屏排查用串口 `sleep`**（打印 state / timeout / 已空闲多久 / 是否被 `suppressed` 压着）；
      `sleep 30000` 直接设超时，**`sleep 0` = 永不**。
      ⚠️ 息屏是**两段式**：ACTIVE --超时--> DIM（背光 15%，暗显时钟）--再 100s--> OFF（全黑）。
      所以 300 秒的超时实际要 **6 分 40 秒**才全黑，中途看着像"没灭"其实是进入 DIM 了。
      ⚠️ 默认 `300000`/`100000` 是 `793e363` 调浏览器内存时**临时放大**的（注释自己写着
      "测试用，原 30s""原 10s"），一直没还原 —— 想更快息屏直接在设置页拖滑块。


      ⚠️ **为铺满这些项而新增的底层能力**（不是纯搬 UI）：
      `ScreenSaver::setIdleTimeout/getIdleTimeout`（原来超时是编译期常量，设置页够不着，
      0 = 永不息屏）；`siAction` 增加 `valueFn` 参数（"点整行切换 + 右侧显示当前值"）。
- [x] **设置持久化（NVS）** —— 2026-09-24，`src/app/settings_store.{h,cpp}`：
      息屏超时 / 亮度 / 自动校时 / 排版视口 存进 NVS（namespace `gtset`），
      开机 `SettingsStore::loadAll()` 读出并**应用到各模块**（只 load 不 apply 等于没做）。
      ⛔ `Preferences::begin(ns, true)`（**只读**）打开一个**还不存在的** namespace 会
      `nvs_open failed: NOT_FOUND` —— **第一次开机必然失败**，必须用读写模式 `false`。
      ⛔ 写入要**延迟合并**（拖动滑块会连发几十次 VALUE_CHANGED）：标脏 + 一次性 lv_timer
      800ms 后统一落盘。诊断：串口 `sstore` / `reboot`。
- [ ] 蓝牙接入（现为占位）
- [ ] 缩略图 / 图片下载
- [~] 更多 2D / 3D 小游戏 —— **已加 2048**（`src/app/game2048_screen.cpp`，游戏栏目第三个）。
      纯回合制无 tick；合并逻辑有离线单测 `tools/test_2048_logic.py`，改规则前先跑它。
      继续加游戏只需三步：写 `XxxScreen_create()` → `nav.cpp` 注册表 → `app_registry.cpp` 加条目。
- [ ] LV_COLOR_DEPTH 16→8（性能下一步候选，待拍板）
- [ ] SD 卡路线 A（资源外置）/ B（Lua 脚本层）二选一 —— 探测数据齐了，待 master 拍板

> 完整清单与背景见 [`docs/00` §7](docs/00-项目上手指南.md)。

---

## 构建依赖：第三方库不在本仓库内

本仓库只放**自研代码**。编译需要的第三方库由 `platformio.ini` 的 `lib_extra_dirs`
指向工程目录**之外**的厂商 BSP：

    lib_extra_dirs = ${PROJECT_DIR}/../4.0inch_ESP32-4848S040/1-Demo/Demo_Arduino/Libraries

clone 之后要先自备这些库（版本需一致，否则编不过）：

| 库 | 版本 | 来源 |
|---|---|---|
| lvgl | 8.3.0-dev | 厂商 BSP（随板资料包） |
| GFX Library for Arduino | 1.2.9 | 厂商 BSP |
| ArduinoJson | 6.17.2 | 厂商 BSP |
| NtpClientLib | 3.0.2-beta | 厂商 BSP |
| Time | 1.6.1 | 厂商 BSP |
| ArduinoZlib | 0.0.1 | 厂商 BSP |
| HTTPClient / Touch_GT911 | — | 厂商 BSP |

`lib_deps` 里这两个由 PlatformIO 自动下载，不用管：Adafruit BusIO、TAMC_GT911。

若厂商库不在上述默认位置，改 `platformio.ini` 的 `lib_extra_dirs` 指向实际路径即可。

> ⚠️ **另外还对 LVGL 源码打过补丁**（不在本仓库内，需自行对照应用）：
> `Lvgl/src/draw/sw/lv_draw_sw_blend.c`（透明像素短路）、`lv_draw_sw_arc.c`、`lv_conf.h`、
> `lv_draw_sw_letter.c`（2026-09-24：emoji 区码位不画缺字形方框，中文缺字仍保留方框作为“字库里没有”的信号）。
> 不打补丁能正常编译运行，只是绘制性能退回未优化水平 / emoji 显示成豆腐块。

---

## 仓库历史说明

本仓库由 geek-terminal 子目录拆分而来：用 `git subtree split` 抽出属于本工程的
17 个 commit，再用 `tools/git_clean_history.py`（fast-export → 过滤 → fast-import
自行实现，替代本机装不上的 git-filter-repo）剔除历史里的构建日志、
arduino-cli.exe、lexbor 的 test/utils/examples 等非自研产物。
仓库体积由 81MB 降到 23MB。

---

## 协作方式

**给 AI / 协作者的工作契约见 [`AGENT.md`](AGENT.md)** —— 排查方法论、本项目红线、
知识落盘约定。开工前读完 `AGENT.md` + `docs/00`。
