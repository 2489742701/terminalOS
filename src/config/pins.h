#ifndef CONFIG_PINS_H
#define CONFIG_PINS_H

// 显示引脚 (ST7701S RGB 并行接口)
constexpr int PIN_BL = 38;          // 背光
constexpr int PIN_CS = 39;          // LCD CS
constexpr int PIN_SCK = 48;         // LCD SCK (SPI 配置用)
constexpr int PIN_SDA = 47;         // LCD SDA (SPI 配置用)
constexpr int PIN_DE = 18;          // DE 信号
constexpr int PIN_VSYNC = 17;       // 垂直同步
constexpr int PIN_HSYNC = 16;       // 水平同步
constexpr int PIN_PCLK = 21;        // 像素时钟

// RGB 数据线 - Red
constexpr int PIN_R0 = 11;
constexpr int PIN_R1 = 12;
constexpr int PIN_R2 = 13;
constexpr int PIN_R3 = 14;
constexpr int PIN_R4 = 0;

// RGB 数据线 - Green
constexpr int PIN_G0 = 8;
constexpr int PIN_G1 = 20;
constexpr int PIN_G2 = 3;
constexpr int PIN_G3 = 46;
constexpr int PIN_G4 = 9;
constexpr int PIN_G5 = 10;

// RGB 数据线 - Blue
constexpr int PIN_B0 = 4;
constexpr int PIN_B1 = 5;
constexpr int PIN_B2 = 6;
constexpr int PIN_B3 = 7;
constexpr int PIN_B4 = 15;

// 触摸引脚 (GT911)
constexpr int PIN_TOUCH_SDA = 19;
constexpr int PIN_TOUCH_SCL = 45;
constexpr int PIN_TOUCH_INT = -1;
constexpr int PIN_TOUCH_RST = -1;

// 显示参数
constexpr int SCREEN_WIDTH = 480;
constexpr int SCREEN_HEIGHT = 480;
constexpr uint32_t RGB_BUS_SPEED = 10000000;  // 10MHz（降 PCLK 给 PSRAM 留带宽，抗 DMA 饿死花屏）

// ST7701 时序参数
constexpr int HSYNC_FRONT_PORCH = 10;
constexpr int HSYNC_PULSE_WIDTH = 8;
constexpr int HSYNC_BACK_PORCH = 50;
constexpr int VSYNC_FRONT_PORCH = 10;
constexpr int VSYNC_PULSE_WIDTH = 8;
constexpr int VSYNC_BACK_PORCH = 20;

#endif