#pragma once
#include <lvgl.h>

lv_obj_t* BrowserScreen_create();
void BrowserScreen_tick();

/* 启动早期调用：在 DRAM 还充足、尚未碎片化时把后台 fetch 任务一次性建好。
   该任务常驻不销毁，之后靠任务通知唤醒，避免运行时反复申请 48KB 连续栈。 */
void BrowserScreen_preinit();

/* 串口调试用：外部传入 URL 触发浏览器加载（不等用户点按钮） */
void BrowserScreen_navigate(const char* url);

/* 把当前页 HTML 存进 LittleFS（底栏「下载」键走的就是这个）。
   串口命令：dl —— 不用点屏也能取页面。 */
void BrowserScreen_download();

/* 内置页面服务：serve / servestop / ls（串口命令） */
void BrowserScreen_serve(bool on);
void BrowserScreen_listPages();

/* 缓存/下载查看与清理（设置屏用） */
void BrowserScreen_cacheInfo(int* pages, size_t* bytes,
                             int* dlCount, size_t* dlBytes);
void BrowserScreen_clearCache();
/* 返回删掉的个数；-1 = 存储不可用 */
int  BrowserScreen_clearDownloads();

/* 退出浏览器：释放网页内容 widget 与布局树（屏壳保留） */
void BrowserScreen_close();
/* 后台 fetch 任务是否仍在运行（nav 用它做释放守卫） */
bool BrowserScreen_isBusy();

/* 排版视口宽度（默认 1024）。网页按它排版，渲染时再等比压到屏幕宽。
   调小 = 版面更接近窄屏流式；调大 = 更接近桌面原貌但缩得更狠。
   串口命令：vp 1024 */
void BrowserScreen_setViewport(int width);
int  BrowserScreen_getViewport();

void BrowserScreen_news(const char* platform);

void BrowserScreen_ime(const char* py);

/* ── 分段渲染诊断（串口 `seg`）──
   seg / seg <n> / seg next / seg prev */
void BrowserScreen_segGo(int start);
void BrowserScreen_segDump();
int  BrowserScreen_segStart();
int  BrowserScreen_segSize();
