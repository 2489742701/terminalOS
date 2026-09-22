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

RenderResult arduino_download_html(const char *url, MemoryBuffer *buffer);

/* HTML 下载是否被截断（Content-Length 超过缓冲区上限）。
   截断时外部 CSS 下载应跳过，避免解析不完整 HTML 关联的样式表导致崩溃。 */
bool arduino_html_was_truncated();

/* 进度回调：下载/解析过程中定期调用，用于更新 UI 进度条。
   downloaded = 已处理量, total = 总量, stage = 当前阶段描述 */
typedef void (*HtmlProgressCallback)(int downloaded, int total, const char *stage);
void arduino_set_progress_callback(HtmlProgressCallback cb);

#ifdef __cplusplus
}
#endif