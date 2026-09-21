#pragma once
#include <lvgl.h>

// 白色线条图标（矢量，运行时用 lv_canvas 绘制，透明底、可任意缩放）
enum class Icon {
  Clock,
  Settings,
  Back,
  Wifi,
  Weather,
  Switch,
  Terminal,
  Music,
  Power,
  Game,
  Browser,
};

// 在 parent 内创建一个 size×size 的白色线条图标，返回 canvas 对象。
// 调用方负责布局（align / set_pos）。buffer 分配在 PSRAM，生命周期跟随对象。
lv_obj_t* icon_create(lv_obj_t* parent, Icon type, uint16_t size);
