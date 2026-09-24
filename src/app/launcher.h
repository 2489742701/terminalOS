#pragma once
#include <lvgl.h>

// 黑底图标网格主屏（Launcher）。磁贴来自 app_registry 里可见的桌面项。
lv_obj_t* LauncherScreen_create();

/* 按注册表重画磁贴（桌面图标开关变了就调一次）。
   只动磁贴容器里的子对象，屏幕对象本身和 nav_launcher 指针都不变 ——
   ScreenSaver 持有的是这个指针，重建整屏会让屏保解锁跳到已删的屏。 */
void LauncherScreen_refresh(lv_obj_t* scr);
