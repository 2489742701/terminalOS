#include "tactilebrowser_core.h"
#include "css_parser.h"
#include "lvgl_renderer.h"
#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <Arduino.h>
extern "C" {
#include <lexbor/core/lexbor.h>
}

/* ── Lexbor 内存重定向到 PSRAM ──
 * Lexbor 解析大 HTML 时会创建大量 DOM 节点，每个节点用 malloc 分配 DRAM。
 * 百度首页 730KB HTML 会创建数千个节点，DRAM 被占满导致 WiFi 任务饿死。
 * 重定向到 PSRAM（8MB 充裕），DRAM 不再争抢。
 * lexbor_memory_setup() 必须在任何 Lexbor 对象创建之前调用，且只需调一次。 */
static void *lexbor_psram_malloc(size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void *lexbor_psram_realloc(void *ptr, size_t size) {
  return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void *lexbor_psram_calloc(size_t n, size_t size) {
  return heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void lexbor_psram_free(void *ptr) {
  heap_caps_free(ptr);
}
static bool s_lexbor_psram_inited = false;
static void init_lexbor_psram() {
  if (s_lexbor_psram_inited) return;
  lxb_status_t st = lexbor_memory_setup(lexbor_psram_malloc, lexbor_psram_realloc,
                                        lexbor_psram_calloc, lexbor_psram_free);
  if (st == LXB_STATUS_OK) {
    s_lexbor_psram_inited = true;
    Serial.println("[Browser] Lexbor PSRAM redirect enabled");
  } else {
    Serial.printf("[Browser] Lexbor PSRAM redirect FAILED: %d\n", (int)st);
  }
}

// Global state
static RenderInterface *global_renderer = NULL;
static RenderResult (*global_html_downloader)(const char *url,
                                               MemoryBuffer *buffer) = NULL;
static Renderer global_renderer_struct = {0};

// Initialize the core library
bool tactilebrowser_core_init(void) {
  init_lexbor_psram();  /* Lexbor 内存重定向到 PSRAM，必须在任何解析之前 */
  if (!html_parser_init())
    return false;
  if (!dom_renderer_init())
    return false;
  if (!css_parser_init())
    return false;
  return true;
}

// Cleanup the core library
void tactilebrowser_core_cleanup(void) {
  dom_renderer_cleanup();
  html_parser_cleanup();
  css_parser_cleanup();
}

// Set platform-specific HTML downloader
void tactilebrowser_set_html_downloader(
    RenderResult (*downloader)(const char *url, MemoryBuffer *buffer)) {
  global_html_downloader = downloader;
  html_parser.download_html = downloader;
}

// Set platform-specific renderer
void tactilebrowser_set_renderer(RenderInterface *renderer) {
  global_renderer = renderer;
  global_renderer_struct.interface = renderer;
}

// Render URL to container
RenderResult tactilebrowser_render_url(const char *url, void *container,
                                       int max_width, int max_height) {
  if (!url || !container || !global_renderer)
    return RENDER_ERROR_UNKNOWN;

  global_renderer_struct.platform_data = container;

  RenderContext context = {.renderer = &global_renderer_struct,
                           .root_container = container,
                           .current_y = 0,
                           .max_width = max_width,
                           .max_height = max_height,
                           .document_url = url};

  return render_html_to_container(url, &context);
}

/* ══ 两阶段异步渲染 API 实现 ══ */

/* Phase 1: 下载 HTML → 解析 DOM → 构建布局树（不触碰 LVGL）。
   可在后台 FreeRTOS 任务中安全运行。stop_flag 提供协作式取消。 */
RenderResult tactilebrowser_download_and_parse(const char *url, int max_width,
                                               int max_height,
                                               volatile bool *stop_flag,
                                               LayoutNode **out_layout) {
  if (!url || !out_layout || !global_renderer)
    return RENDER_ERROR_UNKNOWN;
  *out_layout = NULL;

  /* 设置停止标志给下载函数和 DOM 遍历 */
  arduino_set_stop_flag(stop_flag);
  dom_renderer_set_stop_flag(stop_flag);

  /* 下载 HTML */
  MemoryBuffer buffer = {0};
  RenderResult dl_result = global_html_downloader(url, &buffer);
  if (dl_result != RENDER_SUCCESS) {
    arduino_set_stop_flag(nullptr);
    dom_renderer_set_stop_flag(nullptr);
    return dl_result;
  }
  if (!buffer.data || buffer.size == 0) {
    arduino_set_stop_flag(nullptr);
    dom_renderer_set_stop_flag(nullptr);
    return RENDER_ERROR_NETWORK;
  }

  /* 协作式停止检查 */
  if (stop_flag && *stop_flag) {
    free(buffer.data);
    arduino_set_stop_flag(nullptr);
    dom_renderer_set_stop_flag(nullptr);
    return RENDER_ERROR_UNKNOWN;
  }

  /* 解析 HTML → DOM */
  lxb_html_document_t *document = html_parser.parse_html(buffer.data, buffer.size);
  free(buffer.data);  /* DOM 已解析，HTML 缓冲可以释放 */
  if (!document) {
    arduino_set_stop_flag(nullptr);
    dom_renderer_set_stop_flag(nullptr);
    return RENDER_ERROR_PARSE;
  }

  /* 构建布局树（收集 CSS + 遍历 DOM + 计算尺寸 + 定位） */
  global_renderer_struct.platform_data = NULL;  /* Phase 1 不触碰 LVGL */
  RenderContext context = {.renderer = &global_renderer_struct,
                           .root_container = NULL,
                           .current_y = 0,
                           .max_width = max_width,
                           .max_height = max_height,
                           .document_url = url};

  RenderResult build_result = dom_renderer_build_layout_only(document, &context, out_layout);

  /* DOM 文档可以释放了，布局树已自包含所有数据 */
  lxb_html_document_destroy(document);

  /* 清除停止标志 */
  arduino_set_stop_flag(nullptr);
  dom_renderer_set_stop_flag(nullptr);

  return build_result;
}

/* Phase 2: 渲染布局树到 LVGL 控件（快速，在 UI 任务中调用） */
RenderResult tactilebrowser_render_layout(LayoutNode *layout_root,
                                          void *container, int max_width,
                                          int max_height) {
  if (!layout_root || !container || !global_renderer)
    return RENDER_ERROR_UNKNOWN;

  global_renderer_struct.platform_data = container;
  RenderContext context = {.renderer = &global_renderer_struct,
                           .root_container = container,
                           .current_y = 0,
                           .max_width = max_width,
                           .max_height = max_height,
                           .document_url = ""};

  return dom_renderer_render_layout_only(layout_root, &context);
}

/* Phase 3: 释放布局树 */
void tactilebrowser_free_layout(LayoutNode *layout_root) {
  dom_renderer_free_layout(layout_root);
}

RenderResult tactilebrowser_render_html_string(const char *url,
                                               const char *html, size_t length,
                                               void *container, int max_width,
                                               int max_height) {
  if (!html || !container || !global_renderer) {
    return RENDER_ERROR_UNKNOWN;
  }

  global_renderer_struct.platform_data = container;

  RenderContext context = {.renderer = &global_renderer_struct,
                           .root_container = container,
                           .current_y = 0,
                           .max_width = max_width,
                           .max_height = max_height,
                           .document_url = url};

  size_t html_length = length;
  if (html_length == 0) {
    html_length = strlen(html);
  }

  lxb_html_document_t *document = html_parser.parse_html(html, html_length);
  if (!document) {
    return RENDER_ERROR_PARSE;
  }

  RenderResult render_result = dom_renderer.render_document(document, &context);
  lxb_html_document_destroy(document);
  return render_result;
}

// Utility functions
void memory_buffer_init(MemoryBuffer *buffer) {
  if (buffer) {
    buffer->data = NULL;
    buffer->size = 0;
  }
}

void memory_buffer_free(MemoryBuffer *buffer) {
  if (buffer && buffer->data) {
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
  }
}

char *safe_strdup(const char *str) {
  if (!str)
    return NULL;
  size_t len = strlen(str) + 1;
  char *result = (char *)malloc(len);
  if (result)
    memcpy(result, str, len);
  return result;
}

char *safe_strndup(const char *str, size_t n) {
  if (!str)
    return NULL;
  char *result = (char *)malloc(n + 1);
  if (result) {
    memcpy(result, str, n);
    result[n] = '\0';
  }
  return result;
}