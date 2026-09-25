#pragma once
#include <stdint.h>
#include <Arduino.h>

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

// 读写速度实测（kb = 测试数据量，默认 256）。评估"应用放 SD 卡按需加载"用
void bench(uint32_t kb);
uint32_t spiHz();

/* 读写自检：写一段可预测的字节模式再原样读回比对。
   用来隔离"SD 读写本身会不会改字节" —— 页面缓存读回来内容对不上时先跑它。
   kb = 测试数据量（默认 128）。串口 `sdtest [kb]`。 */
bool selfTest(uint32_t kb);

// 列目录到串口，depth 是递归层数（默认 1）
void listDir(const char* path, int depth);

/* 整文件读写（天气 JSON 缓存等）。未挂载 / 失败一律返回 false，调用方静默跳过。
   ⚠️ 写之前会建父目录（/gt 这种）。别在没挂载时调 —— 会去动 SPI。 */
bool writeFile(const char* path, const String& data);
bool readFile(const char* path, String& out);

/* 二进制整文件写（图片另存用）。
   ⚠️ 必须是独立的入口：writeFile 收 String，而 String 构造/拼接都按 NUL
   结尾处理 —— JPEG/PNG 中间随便一个 0x00 就把后面全吃了（readFile 已经
   栽过一次，见 docs/09 的 L4）。 */
bool writeFileBin(const char* path, const uint8_t* data, size_t len);

/* 二进制整文件读（图片缓存回读）。
   *out 用 heap_caps_malloc(MALLOC_CAP_SPIRAM) 分配 —— 图片字节必须落 PSRAM，
   内部 DRAM 只有几百 KB，一张 64KB 的图就能把它吃掉一大截。
   调用方负责 heap_caps_free(*out)。未挂载 / 打不开 / 长度对不上一律 false。
   ⚠️ 按**字节数**读并比对实际读到的长度，不依赖任何 NUL 结尾（见 readFile）。 */
bool readFileBin(const char* path, uint8_t** out, size_t* outLen);

/* 文件是否存在 + 大小。exists 只要"在不在"就传 NULL 给 size。 */
bool statFile(const char* path, size_t* size);

}  // namespace SDCard
