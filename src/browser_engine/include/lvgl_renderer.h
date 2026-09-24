#pragma once

#include <lvgl.h>
#include "tactilebrowser_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  RenderInterface base;
} LvglRenderer;

LvglRenderer *lvgl_renderer_create(void);
void lvgl_renderer_destroy(LvglRenderer *renderer);

/* 链接点击回传：引擎只负责"把 URL 挂到 widget 上"，真正跳不跳、怎么进历史栈
   由 app 层决定（引擎不认识当前页/历史栈/是否正在加载）。
   ⚠️ 回调里**不要**直接 lv_obj_clean() 当前内容 —— 那个 widget 此刻正处在自己的
   事件回调里，删自己等于在事件派发循环中把 dsc 链表 free 掉（LoadProhibited）。
   正确做法：只记下 URL，等下一个 UI tick 再真正导航。 */
typedef void (*LvglLinkCallback)(const char *url);
void lvgl_renderer_set_link_callback(LvglLinkCallback cb);

/* 诊断计数：本次渲染挂上了多少个可点链接（每次渲染前 reset，渲染后读）。 */
void lvgl_renderer_reset_link_count(void);
int lvgl_renderer_link_count(void);

RenderResult arduino_download_html(const char *url, MemoryBuffer *buffer);

/* HTML 下载是否被截断（Content-Length 超过缓冲区上限）。
   截断时外部 CSS 下载应跳过，避免解析不完整 HTML 关联的样式表导致崩溃。 */
bool arduino_html_was_truncated();

/* 进度回调：下载/解析过程中定期调用，用于更新 UI 进度条。
    downloaded = 已处理量, total = 总量, stage = 当前阶段描述 */
typedef void (*HtmlProgressCallback)(int downloaded, int total, const char *stage);
void arduino_set_progress_callback(HtmlProgressCallback cb);

/* 协作式停止：设置 stop_flag=true 后，arduino_download_html 和 DOM 遍历会尽快退出。
   必须用 volatile 防止编译器优化掉检查。 */
void arduino_set_stop_flag(volatile bool *flag);

#ifdef __cplusplus
}
#endif