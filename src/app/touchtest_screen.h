#pragma once
#include <lvgl.h>

/* 触摸测试屏（诊断"坐标偏移"用，串口 `nav touchtest` 直达，画板底部也有入口）。
   玩法：手指按在哪，十字就该出现在哪 —— 不重合就是映射错了。
   底部四个开关（翻转X / 翻转Y / 交换XY / 重置）点了立即生效，**不用重新烧录**。
   每按一次都会往串口打一行 `[TouchTest] screen=(x,y) raw=(rx,ry)`，
   把四个角各点一下，串口那边就能直接算出正确的校准端点。 */
lv_obj_t* TouchTestScreen_create();
