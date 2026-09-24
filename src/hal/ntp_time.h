#pragma once

#include <stdint.h>
#include <stdbool.h>

/* NTP 网络校时。
 *
 * 为什么自己写而不用 NtpClientLib：那个库会把 Arduino 的 Time 库依赖塞回来，
 * 而本项目在 platformio.ini 里 `lib_ignore = Time`（Windows 大小写不敏感，
 * <time.h> 会被 Libraries/Time/Time.h 遮蔽）。所以这里只用 WiFiUDP + 手写
 * NTP 报文 + settimeofday()。
 *
 * 对时在自己的常驻任务里做（后台阻塞不影响 UI 刷新）。任务只创建一次，
 * 之后靠 task notify 唤醒 + 定时轮询，不做反复 create/delete。 */
namespace NtpTime {

/* 开机后调用一次：创建常驻对时任务。重复调用无副作用。 */
void begin();

/* 手动请求一次对时（设置页「校准时间」按钮）。异步，不阻塞 UI。 */
void requestSync();

/* 自动校时开关（联网后自动对 + 每 6 小时重对一次）。默认开。 */
void setAutoSync(bool on);
bool autoSyncEnabled();

bool isSynced();
/* 上次成功对时的 UNIX 时间（秒），未对过时为 0 */
uint32_t lastSyncUnix();
/* 上次成功对时距今多少毫秒（用于显示"x 分钟前"） */
uint32_t sinceSyncMs();

}  // namespace NtpTime
