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

**⚠️ 动任何显示 / 构建配置之前，先读 `docs/02` 的「板级硬事实」** —— 那几条改错就直接开不了机。

---

## 三条最容易踩的硬事实

1. **Flash 模式 = DIO，PSRAM 线模式 = OPI，两条必须同时成立**（缺一开不了机）。
2. **PCLK = 10MHz，不要改回 16MHz**（改回去会刷新花屏，见 `docs/01`）。
3. **不要删 `.pio`**（会触发依赖库重下 + safe-delete 拦截，环境崩）。重编只删 `.wb_build`。

---

## 快速开始

```powershell
# 构建 + 烧录 + 抓串口日志（一键）
powershell -File "C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\refresh_test.ps1"
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

- [ ] 复测锁屏上滑需偏移才响应（触摸坐标映射）
- [ ] 复测上滑时左下角白线
- [ ] WiFi / NTP 校时（注意避开 `Time` 库，见 `docs/02` §2.4）
- [ ] 主桌面双击时间校准
- [ ] Launcher 扩展（天气 / 开关屏）
