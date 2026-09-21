#pragma once
#include <lvgl.h>

// 设置屏：返回按钮 + 时间源显示 + 校准入口（v1 占位，待接入 WiFi/NTP）。
lv_obj_t* SettingsScreen_create();
