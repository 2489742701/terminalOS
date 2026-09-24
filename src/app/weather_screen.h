#pragma once
#include <lvgl.h>

lv_obj_t* WeatherScreen_create();
void WeatherScreen_tick();

/* 串口/UI 触发一次天气拉取（会阻塞 1~2 秒，别在事件回调里调）。
   返回 true 表示拿到了数据。 */
bool WeatherScreen_fetchNow(const char* adcode);

/* 后台自动更新：开 = 常驻任务每 1 小时自己拉一次（不进天气页也在跑）。
   关 = 完全停止：任务退回 portMAX_DELAY 睡眠，不轮询、不联网、不占 CPU。 */
void WeatherScreen_setAuto(bool on);
bool WeatherScreen_auto();
