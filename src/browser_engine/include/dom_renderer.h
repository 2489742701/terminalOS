#pragma once

#include "common_types.h"
#include "html_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

// DOM renderer interface
typedef struct {
  // Render HTML document to container
  RenderResult (*render_document)(lxb_html_document_t *document,
                                  RenderContext *context);
  // Render DOM node recursively
  void (*render_node)(lxb_dom_node_t *node, RenderContext *context);
  // Create widget for element
  void *(*create_element_widget)(ElementType type, RenderContext *context,
                                 const char *text);
  // Apply CSS styles to widget
  void (*apply_styles)(void *widget, RenderContext *context, const char *style);
} DomRendererInterface;

// Global DOM renderer instance
extern DomRendererInterface dom_renderer;

// Initialize DOM renderer
bool dom_renderer_init(void);

// Cleanup DOM renderer
void dom_renderer_cleanup(void);

// Main rendering function
RenderResult render_html_to_container(const char *url, RenderContext *context);

/* 两阶段拆分：build 只做下载+解析+构建布局树（不触碰 LVGL）；
   render 只做 LVGL 控件创建（快速）。layout_engine.h 的 LayoutNode 前向声明。 */
struct LayoutNode;
void dom_renderer_set_stop_flag(volatile bool *flag);
RenderResult dom_renderer_build_layout_only(lxb_html_document_t *document,
                                            RenderContext *context,
                                            struct LayoutNode **out_root);
RenderResult dom_renderer_render_layout_only(struct LayoutNode *layout_root,
                                             RenderContext *context);
void dom_renderer_free_layout(struct LayoutNode *root);

#ifdef __cplusplus
}
#endif