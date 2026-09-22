#pragma once

#include "common_types.h"
#include "dom_renderer.h"
#include "html_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

// Main TactileBrowser Core API

// Initialize the core library
bool tactilebrowser_core_init(void);

// Cleanup the core library
void tactilebrowser_core_cleanup(void);

// Set platform-specific HTML downloader
void tactilebrowser_set_html_downloader(
    RenderResult (*downloader)(const char *url, MemoryBuffer *buffer));

// Set platform-specific renderer
void tactilebrowser_set_renderer(RenderInterface *renderer);

// Render URL to container
RenderResult tactilebrowser_render_url(const char *url, void *container,
                                       int max_width, int max_height);

/* ══ 两阶段异步渲染 API（后台下载解析 + UI 渲染分离）══
 * Phase 1: tactilebrowser_download_and_parse — 后台任务调用，不触碰 LVGL。
 *   下载 HTML → 解析 DOM → 收集 CSS → 构建布局树 → 返回 LayoutNode*。
 *   stop_flag 非 NULL 时，每块检查标志，true 则尽快返回 RENDER_ERROR_UNKNOWN。
 * Phase 2: tactilebrowser_render_layout — UI 任务调用，快速创建 LVGL 控件。
 * Phase 3: tactilebrowser_free_layout — 释放布局树。
 * ══════════════════════════════════════════════════════ */
typedef struct LayoutNode LayoutNode;

RenderResult tactilebrowser_download_and_parse(const char *url, int max_width,
                                               int max_height,
                                               volatile bool *stop_flag,
                                               LayoutNode **out_layout);
RenderResult tactilebrowser_render_layout(LayoutNode *layout_root,
                                          void *container, int max_width,
                                          int max_height);
void tactilebrowser_free_layout(LayoutNode *layout_root);

// Render already-downloaded HTML string to a container
RenderResult tactilebrowser_render_html_string(const char *url,
                                               const char *html, size_t length,
                                               void *container, int max_width,
                                               int max_height);

// Utility functions
void memory_buffer_init(MemoryBuffer *buffer);
void memory_buffer_free(MemoryBuffer *buffer);
char *safe_strdup(const char *str);
char *safe_strndup(const char *str, size_t n);

#ifdef __cplusplus
}
#endif