#pragma once
#include <lvgl.h>

/* 2048 —— 游戏栏目里的第二个 2D 小游戏。
   纯回合制（滑动才走一步），所以**没有 tick**：不需要在 App::loop 里挂任何东西。 */
lv_obj_t* Game2048Screen_create();
