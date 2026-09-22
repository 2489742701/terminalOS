# Geek Terminal

ESP32-S3 上的黑底白线图标小系统：Launcher + 时钟 + 设置三屏，带息屏 / 锁屏（上滑解锁）。

**硬件**：ESP32-S3（16MB Flash / 8MB OPI PSRAM） + 4.0" 480×480 ST7701S RGB 并行屏 + GT911 触摸
**平台**：PlatformIO，`espressif32@^6.0.0` + `framework = arduino`（Arduino core 2.x / IDF 4.4）
**串口**：COM7 @ 115200

---

## 文档

| 文档 | 内容 |
|---|---|
| [`docs/01-显示花屏排查与修复.md`](docs/01-显示花屏排查与修复.md) | 刷新瞬间花屏的根因（PSRAM 带宽争抢 / LCD DMA 饿死）、有效修复、已排除嫌疑、社区方案可行性 |
| [`docs/02-板级硬事实与构建烧录.md`](docs/02-板级硬事实与构建烧录.md) | Flash 必须 DIO、PSRAM 必须 OPI、构建环境坑、标准构建/烧录/抓日志命令、已知无害告警 |
| [`docs/06-浏览器内存管理与Activity生命周期.md`](docs/06-浏览器内存管理与Activity生命周期.md) | **已实机验证**：常驻 fetch 任务、Activity 懒创建/退出回收、LVGL 池 vs DRAM 两套账本、`MAX_WIDGETS` 80→150、退出崩溃修复 |
| [`docs/03-参考项目分析.md`](docs/03-参考项目分析.md) | 参考项目（MicroPythonOS Activity/Intent、First-Start 同硬件 BSP）分析 |
| [`docs/04-当前项目知识库.md`](docs/04-当前项目知识库.md) | 硬件规格、引脚、构建配置、已解决的坑、中文字体方案 |
| [`docs/05-浏览器异步架构与Lexbor崩溃修复.md`](docs/05-浏览器异步架构与Lexbor崩溃修复.md) | 两阶段异步渲染（后台解析 / UI 渲染）、Lexbor CSS 崩溃的绕法 |

**⚠️ 动任何显示 / 构建配置之前，先读 `docs/02` 的「板级硬事实」** —— 那几条改错就直接开不了机。

---

## 三条最容易踩的硬事实

1. **Flash 模式 = DIO，PSRAM 线模式 = OPI，两条必须同时成立**（缺一开不了机）。
2. **PCLK = 10MHz，不要改回 16MHz**（改回去会刷新花屏，见 `docs/01`）。
3. **不要删 `.pio`**（会触发依赖库重下 + safe-delete 拦截，环境崩）。重编只删 `.wb_build`。
4. **编译后务必 `grep -c Compiling`**（全量应为 533）。若为 0，PIO 静默跳过了编译，
   烧录的仍是旧固件 —— 详见 `docs/02` §6。
5. **运行时不要调 `heap_caps_get_largest_free_block()` / `heap_caps_get_info()`**：
   O(n) 遍历整堆持锁，撞上 WiFi 收包直接 wdt 重启（addr2line 实证，见 `docs/06` §6.2）。

---

## 快速开始

```bash
# 1) 清构建目录（必须！否则 PIO 可能静默跳过编译，见 docs/02 §6）
"C:/Users/longyaosi/.workbuddy/binaries/python/versions/3.13.12/python.exe" \
  -c "import shutil; shutil.rmtree('.wb_build', ignore_errors=True)"

# 2) 编译（约 100 秒，日志里应有 533 行 Compiling）
"C:/Users/longyaosi/python-sdk/python3.13.2/python.exe" -m platformio run -d . -e esp32s3

# 3) 烧录（flash_mode 必须 dio）
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

- Launcher / 时钟 / 设置 三屏导航，黑底 + 白色线条图标
- 息屏状态机（`src/app/screensaver.cpp`）：ACTIVE（亮）→ DIM（PWM 15% 暗显时间）→ OFF（关背光）
  - DIM 态上滑 ≥120px 解锁；OFF 态触摸回 DIM
  - 离线软时钟（`__DATE__`/`__TIME__` + `mktime` + `settimeofday`，TZ=CST-8）
- 背光 PWM（`Display::setBacklightLevel`，LEDC，PIN_BL=38）

## 待办

- [ ] **解析阶段 DRAM 只剩几百字节**是最大隐患：布局树仍在内部 DRAM
      （`layout=0x3fce0690`），考虑挪到 PSRAM（见 `docs/06` §9.2）
- [ ] 复测锁屏上滑需偏移才响应（触摸坐标映射）
- [ ] 复测上滑时左下角白线
- [ ] WiFi / NTP 校时（注意避开 `Time` 库，见 `docs/02` §2.4）
- [ ] 主桌面双击时间校准
- [ ] Launcher 扩展（天气 / 开关屏）
- [ ] 布局引擎 CSS 支持度提升
