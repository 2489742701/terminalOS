#pragma once
#include <lvgl.h>

lv_obj_t* WifiScreen_create();
void WifiScreen_tick();
bool WifiScreen_isConnecting();