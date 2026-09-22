#pragma once

/**
 * SerialConsole - 串口命令监听（调试用）
 *
 * 用途：通过串口发命令控制设备，不用每次改代码重新烧录。
 *       比如串口发 "nav browser" 切到浏览器，发 "browser http://info.cern.ch/" 直接打开网页。
 *
 * 开关：把 SERIAL_CONSOLE_ENABLED 改成 0 即可完全关闭，发行版置 0。
 *
 * 命令列表：
 *   help              显示所有命令
 *   nav <screen>      导航到指定屏幕 (launcher/clock/settings/wifi/game/browser/draw/memory/sysinfo/weather)
 *   browser <url>     打开浏览器并访问指定 URL
 *   wifi              查看 WiFi 连接状态
 *   mem               查看内存信息 (DRAM + PSRAM)
 *   reboot            重启设备
 *   version           显示版本信息
 *   time              显示当前系统运行时间
 */

#define SERIAL_CONSOLE_ENABLED 1

namespace SerialConsole {

void begin();
void tick();

}  // namespace SerialConsole