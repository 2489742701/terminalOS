#pragma once
#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * TF / microSD 卡（SPI 模式）
 *
 * 引脚来自厂家 Demo（1_3_switch86_lvgl_music/HAL.h），这块板子的 TF 卡槽
 * 跟 ST7701S **共用一条 SPI**：
 *     SD_CS  = 42
 *     MOSI   = 47   （LCD 的 SDA 也是 47）
 *     MISO   = 41
 *     SCK    = 48   （LCD 的 SCK 也是 48）
 * LCD 自己用 CS=39，两边靠 CS 分时会用同一条总线（板子就这一种接法）。
 *
 * ⚠️ 因此**绝不能**用全局的 `SPI` 对象去 SD.begin()：Arduino_GFX 那边也在用
 *    SPI（FSPI），重新 begin() 等于把 SPI 引脚映射改掉，屏幕会花。
 *    这里给 SD 单独开一个 SPIClass(HSPI) 实例，两边互不打扰。
 *
 * ⚠️ 挂载是**懒加载**：开机不自动 mount（怕干扰 LCD 初始化），
 *    由串口 `sd` 命令 / 需要读卡的地方显式调用。
 * ═══════════════════════════════════════════════════════════════════════════ */

#define PIN_SD_CS   42
#define PIN_SD_MOSI 47
#define PIN_SD_MISO 41
#define PIN_SD_SCK  48

namespace SDCard {

// 挂载（幂等：已挂载直接返回 true）。失败原因打串口。
bool begin();

// 结束挂载（释放 SPI 总线，避免长期占用与 LCD 抢）
void end();

bool mounted();

// 容量（字节）。未挂载返回 0
uint64_t cardBytes();
uint64_t totalBytes();
uint64_t usedBytes();

// 卡类型字符串："NONE" / "MMC" / "SD" / "SDHC" / "UNKNOWN"
const char* typeName();

// 列目录到串口，depth 是递归层数（默认 1）
void listDir(const char* path, int depth);

}  // namespace SDCard
