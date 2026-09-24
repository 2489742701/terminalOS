#pragma once
#include <lvgl.h>

// 桌面图标管理：逐个开关某个应用在桌面上显不显示。
// 改动立刻写 NVS 并让 launcher 重画，退出后回桌面就能看到效果。
lv_obj_t* DesktopScreen_create();
