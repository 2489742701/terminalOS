#pragma once
#include <lvgl.h>

// 时钟屏：大字号时间 + 日期，每秒刷新（ClockScreen_update）。
lv_obj_t* ClockScreen_create();
void ClockScreen_update();
