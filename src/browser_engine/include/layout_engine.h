#pragma once

#include "common_types.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// CSS Box Model
typedef struct {
  int top;
  int right;
  int bottom;
  int left;
} BoxSpacing;

// Layout box model with all CSS properties
typedef struct {
  BoxSpacing margin;
  BoxSpacing padding;
  BoxSpacing border;

  int width;        // Content width
  int height;       // Content height
  bool width_auto;  // Auto width calculation
  bool height_auto; // Auto height calculation

  // Positioning
  int x;
  int y;

  // Display properties
  bool is_block; // Block vs inline
  bool is_inline_block;

  // Text properties
  uint32_t color;
  uint32_t bg_color;
  bool has_explicit_color;
  bool has_explicit_bg_color;
  BackgroundFill background;
  int font_size;
  int line_height;
  int text_align; // 0=left, 1=center, 2=right

  /* 布局意图：flex 方向（0=none/继承父容器, 1=column垂直, 2=row水平） */
  int flex_direction;

  // Overflow handling
  bool scroll_y;
  bool scroll_x;
} LayoutBox;

// Layout node representing an element in the layout tree
typedef struct LayoutNode {
  ElementType type;
  LayoutBox box;

  char *text_content;
  char *href; // For links
  char *href_resolved;
  char *href_path;
  char *form_value;  // For inputs/textarea
  char *placeholder; // For inputs

  /* ── 图片（缩略图）2026-09-25 ── */
  char *img_src;  /* <img src> 的绝对地址（下载前就填好，下载后保留便于诊断） */
  int   img_w;    /* HTML width 属性，0=没写 */
  int   img_h;    /* HTML height 属性，0=没写 */
  void *img_dsc;    /* 下载到的**原始**字节（lv_img_dsc_t 包着 jpg/png 原文） */
  void *img_thumb;  /* 重采样后的小图 dsc，渲染用的就是它；NULL = 这块不占瓦片 */

  void *widget; // Platform widget

  struct LayoutNode *parent;
  struct LayoutNode *first_child;
  struct LayoutNode *next_sibling;
} LayoutNode;

// Layout context for building layout tree
typedef struct {
  LayoutNode *root;
  LayoutNode *current_container;

  RenderContext *render_context;

  // Current position tracking
  int current_x;
  int current_y;
  int max_line_height;
  int container_width;

  // Line breaking for inline elements
  bool needs_line_break;
} LayoutContext;

// Initialize layout engine
bool layout_engine_init(void);

// Cleanup layout engine
void layout_engine_cleanup(void);

// Create a new layout node
LayoutNode *layout_node_create(ElementType type);

// Destroy layout node and its children
void layout_node_destroy(LayoutNode *node);

// Add child node
void layout_node_add_child(LayoutNode *parent, LayoutNode *child);

// Initialize layout context
LayoutContext *layout_context_create(RenderContext *render_ctx);

// Destroy layout context
void layout_context_destroy(LayoutContext *ctx);

// Calculate box dimensions based on content
void layout_calculate_dimensions(LayoutNode *node, int available_width);

// Position node and its children
void layout_position_node(LayoutNode *node, int parent_x, int parent_y);

// Render layout tree to screen
void layout_render_tree(LayoutNode *root, RenderContext *render_ctx);

/* ── 分段渲染（2026-09-25）──────────────────────────────────────────────
 * 老做法：一次性把整棵树铺成 widget，撞到 MAX_WIDGETS 就把后面的内容**全部
 * 丢弃** —— 长页面（必应/360 中文搜索）后半截直接消失，这才是"页面破碎"的
 * 真因。新做法：
 *   1) 排版/摊平/去垃圾只做一次（按 root 指针记已准备），之后翻段直接复用；
 *   2) 先"干跑"一遍数出瓦片总数（label / 胶囊 / 输入框各算一块）；
 *   3) 真正渲染时只铺 [start, start+count) 这一段。
 * 于是翻段 = 用同一棵布局树重铺，不重新联网、不重新解析，代价只有几十毫秒。
 *
 * start<0 或 count<=0 = 不分段的旧行为（铺到 MAX_WIDGETS 为止）。
 * ⚠️ 布局树被释放后必须调 layout_forget_prepare()，否则新树可能复用同一块
 *    地址，被误判成"已准备"而跳过摊平。 */
void layout_set_segment(int start, int count);
int  layout_tile_total(void);      /* 本页瓦片总数（干跑得出） */
int  layout_tile_rendered(void);   /* 本次渲染实际铺了多少块 */
void layout_forget_prepare(void);  /* 布局树释放时调用 */

/* 收集整棵树里"值得下载"的图片节点，写进 out，最多 max 个，返回个数。
   跳过：没有 img_src、已经下载过（img_dsc 非空）、HTML 里写明是 1x1~4x4
   的那种追踪像素。
   ⚠️ 只用来**挑**，不联网 —— 联网在后台 fetch 任务里做。 */
int layout_collect_images(LayoutNode *root, LayoutNode **out, int max);

/* 收集"已经拿到原始字节"的图片节点（img_dsc 非空），最多 max 个。
   渲染前用它批量做缩略图 —— 重采样要碰 LVGL 解码器，只能在 UI 线程做。 */
int layout_collect_ready_images(LayoutNode *root, LayoutNode **out, int max);

/* 把树里所有 ELEMENT_IMAGE 节点打到串口（串口 `imgscan`）。诊断用。 */
void layout_dump_images(LayoutNode *root);

/* 认领钩子：给定节点的图片地址，返回要挂上去的 dsc（NULL = 不要）。
   为什么要绕这一道：图片必须在**解析之前**就抓下来（解析会把内部 DRAM 吃光，
   之后 DNS 直接失败），那时候还没有布局树、也就没有"节点"可以挂。
   所以先扫 HTML 抓图，树建好后再按 URL 认领回来。 */
typedef void *(*LayoutImageMatcher)(const char *src, void *ctx);
int layout_assign_images(LayoutNode *root, LayoutImageMatcher matcher,
                         void *ctx);

// 记录本次排版使用的视口宽度。渲染阶段据此算缩放系数（屏幕宽/视口宽）。
// 必须在排版（layout_calculate_dimensions）之前调用，否则缩放退化为 1.0（整页露不全）。
void layout_set_viewport_width(int width);

// 屏幕内容区宽度（464）。<meta viewport width=device-width> 用它作为排版宽度。
void layout_set_screen_width(int width);
int layout_get_screen_width(void);

/* 平铺模式开关（默认开）。
   关 = 还原 CSS 版面（等比缩放 + 横向钳制）；
   开 = 放弃还原版面：不创建任何容器，所有文本/链接/输入框直接挂进根容器
        （flex column），全宽、居左、自上而下平铺。串口 `flat on|off` 可切。 */
void layout_set_flat_mode(bool on);
void layout_set_flat_dump_limit(int n);
/* 平铺模式查询。dom_renderer 用它决定要不要下载外部 CSS —— 平铺不还原版面，
   CSS 里的坐标/尺寸一条都用不上，下载纯属浪费（cn.bing.com 一次要串行拉 30+ 个
   外部 CSS，每个一次 TLS 握手，总共约 1 分钟）。 */
/* 平铺模式查询。dom_renderer 用它决定要不要下载外部 CSS —— 平铺不还原版面，
   CSS 里的坐标/尺寸一条都用不上，下载纯属浪费（cn.bing.com 一次要串行拉 30+ 个
   外部 CSS，每个一次 TLS 握手，总共约 1 分钟）。 */
bool layout_get_flat_mode(void);

// Apply CSS properties to layout box
void layout_apply_css_property(LayoutBox *box, const char *property,
                               const char *value);

// Parse CSS spacing (margin, padding, border)
BoxSpacing layout_parse_spacing(const char *value);

// Calculate total width including padding, border, margin
int layout_get_total_width(const LayoutBox *box);

// Calculate total height including padding, border, margin
int layout_get_total_height(const LayoutBox *box);

// Text wrapping utilities
typedef struct {
  char **lines;
  int line_count;
  int *line_widths;
} TextLines;

TextLines *layout_wrap_text(const char *text, int max_width, int font_size);
void layout_free_text_lines(TextLines *lines);

#ifdef __cplusplus
}
#endif
