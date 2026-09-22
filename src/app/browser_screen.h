#pragma once
#include <lvgl.h>

lv_obj_t* BrowserScreen_create();
void BrowserScreen_tick();

/* 串口调试用：外部传入 URL 触发浏览器加载（不等用户点按钮） */
void BrowserScreen_navigate(const char* url);