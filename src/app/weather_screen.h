#pragma once
#include <lvgl.h>

lv_obj_t* WeatherScreen_create();
void WeatherScreen_tick();

/* 串口/UI 触发一次天气拉取（会阻塞 1~2 秒，别在事件回调里调）。
   返回 true 表示拿到了数据。 */
bool WeatherScreen_fetchNow(const char* adcode);
