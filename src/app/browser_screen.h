#pragma once
#include <lvgl.h>

lv_obj_t* BrowserScreen_create();
void BrowserScreen_tick();

/* 启动早期调用：在 DRAM 还充足、尚未碎片化时把后台 fetch 任务一次性建好。
   该任务常驻不销毁，之后靠任务通知唤醒，避免运行时反复申请 48KB 连续栈。 */
void BrowserScreen_preinit();

/* 串口调试用：外部传入 URL 触发浏览器加载（不等用户点按钮） */
void BrowserScreen_navigate(const char* url);

/* 退出浏览器：释放网页内容 widget 与布局树（屏壳保留） */
void BrowserScreen_close();