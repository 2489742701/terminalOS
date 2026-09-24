#include "layout_engine.h"
#include "css_parser.h"
#include "tactilebrowser_core.h"
#include "lvgl_renderer.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


/* ── 递归深度上限（防御性）──────────────────────────────────────────────
 * 2026-09-23 崩溃复盘：原实现把 next_sibling 也做成递归，导致遍历一棵树的
 * 递归深度 = **节点总数**（几百到上千）。而 loopTask 的栈默认只有 8192 B ——
 * 必应搜索页 356 个节点侥幸过关，乐鑫官网那种更多节点的页面直接
 * "A stack overflow in task loopTask has been detected" 重启，且因为 RGB
 * 并行屏由 DMA 自行刷新，画面还在、触摸全失效，极具迷惑性。
 *
 * 修复：兄弟节点改用迭代（已改），递归深度 = 树的真实嵌套层数；
 * 再给剩下的父子递归加一道上限，畸形页面（如无限嵌套的 JS 生成 DOM）
 * 剪断子树而不是继续压栈。正常网页嵌套层数一般 < 30，这里给充分余量。
 */
#define MAX_LAYOUT_DEPTH 64
// Default box model values
static const LayoutBox DEFAULT_BOX = {
    .margin = {0, 0, 0, 0},
    .padding = {8, 8, 8, 8},
    .border = {0, 0, 0, 0},
    .width = 0,
    .height = 0,
    .width_auto = true,
    .height_auto = true,
    .x = 0,
    .y = 0,
    .is_block = true,
    .is_inline_block = false,
    .color = 0xFFFFFF,
    .bg_color = 0x000000,
    .has_explicit_color = false,
    .has_explicit_bg_color = false,
    .background = {.type = BACKGROUND_FILL_NONE},
    .font_size = 14,
    .line_height = 20,
    .text_align = 0,
    .flex_direction = 0,
    .scroll_y = false,
    .scroll_x = false};

static char *trim_whitespace_inplace(char *str) {
  if (!str)
    return NULL;
  while (*str && isspace((unsigned char)*str)) {
    str++;
  }
  char *end = str + strlen(str);
  while (end > str && isspace((unsigned char)*(end - 1))) {
    *(--end) = '\0';
  }
  return str;
}

static bool layout_parse_css_color(const char *value, uint32_t *color) {
  if (!value || !color)
    return false;
  return css_parser_parse_color_value(value, color);
}

static bool parse_angle_keyword(const char *token, float *angle_deg) {
  if (!token || !angle_deg)
    return false;
  char *copy = safe_strdup(token);
  if (!copy)
    return false;
  char *trimmed = trim_whitespace_inplace(copy);
  for (char *c = trimmed; *c; ++c) {
    *c = (char)tolower((unsigned char)*c);
  }

  bool handled = false;
  if (strstr(trimmed, "deg")) {
    double value = atof(trimmed);
    *angle_deg = (float)value;
    handled = true;
  } else if (strncmp(trimmed, "to ", 3) == 0) {
    bool to_top = strstr(trimmed, "top") != NULL;
    bool to_bottom = strstr(trimmed, "bottom") != NULL;
    bool to_left = strstr(trimmed, "left") != NULL;
    bool to_right = strstr(trimmed, "right") != NULL;
    if (to_top && to_right) {
      *angle_deg = 45.0f;
      handled = true;
    } else if (to_top && to_left) {
      *angle_deg = 315.0f;
      handled = true;
    } else if (to_bottom && to_right) {
      *angle_deg = 135.0f;
      handled = true;
    } else if (to_bottom && to_left) {
      *angle_deg = 225.0f;
      handled = true;
    } else if (to_top) {
      *angle_deg = 0.0f;
      handled = true;
    } else if (to_right) {
      *angle_deg = 90.0f;
      handled = true;
    } else if (to_bottom) {
      *angle_deg = 180.0f;
      handled = true;
    } else if (to_left) {
      *angle_deg = 270.0f;
      handled = true;
    }
  }

  free(copy);
  return handled;
}

static bool parse_gradient_color_token(const char *token, uint32_t *color) {
  if (!token || !color)
    return false;
  char *copy = safe_strdup(token);
  if (!copy)
    return false;
  char *trimmed = trim_whitespace_inplace(copy);
  bool parsed = css_parser_parse_color_value(trimmed, color);
  if (!parsed) {
    char *last_space = strrchr(trimmed, ' ');
    if (last_space) {
      *last_space = '\0';
      parsed = css_parser_parse_color_value(trimmed, color);
    }
  }
  free(copy);
  return parsed;
}

static bool layout_parse_linear_gradient(const char *value,
                                         LinearGradientFill *gradient) {
  if (!value || !gradient)
    return false;
  const char *keyword = strstr(value, "linear-gradient");
  if (!keyword)
    return false;
  const char *open = strchr(keyword, '(');
  const char *close = strrchr(keyword, ')');
  if (!open || !close || close <= open)
    return false;

  char *inner = safe_strndup(open + 1, (size_t)(close - open - 1));
  if (!inner)
    return false;

  float angle = 180.0f;
  uint32_t colors[TACTILEBROWSER_MAX_GRADIENT_STOPS] = {0};
  size_t stop_count = 0;

  char *saveptr = NULL;
  char *token = strtok_r(inner, ",", &saveptr);
  while (token && stop_count < TACTILEBROWSER_MAX_GRADIENT_STOPS) {
    char *trimmed = trim_whitespace_inplace(token);
    if (*trimmed == '\0') {
      token = strtok_r(NULL, ",", &saveptr);
      continue;
    }

    if (stop_count == 0) {
      float parsed_angle = 0.0f;
      if (parse_angle_keyword(trimmed, &parsed_angle)) {
        angle = parsed_angle;
        token = strtok_r(NULL, ",", &saveptr);
        continue;
      }
    }

    uint32_t color = 0;
    if (parse_gradient_color_token(trimmed, &color)) {
      colors[stop_count++] = color;
    }

    token = strtok_r(NULL, ",", &saveptr);
  }

  free(inner);

  if (stop_count < 2) {
    return false;
  }

  gradient->angle_deg = angle;
  gradient->stop_count = stop_count;
  for (size_t i = 0; i < stop_count; ++i) {
    gradient->stops[i].color = colors[i];
    gradient->stops[i].position =
        (stop_count == 1) ? 0.0f : (float)i / (float)(stop_count - 1);
  }

  return true;
}

static void layout_apply_background_fill(RenderInterface *iface,
                                         Renderer *renderer, LayoutBox *box,
                                         void *widget) {
  if (!iface || !renderer || !box || !widget)
    return;

  if (box->background.type == BACKGROUND_FILL_LINEAR_GRADIENT &&
      box->background.data.linear.stop_count > 0) {
    if (iface->set_bg_gradient) {
      iface->set_bg_gradient(renderer, widget, &box->background.data.linear);
    } else if (iface->set_bg_color) {
      iface->set_bg_color(renderer, widget,
                          box->background.data.linear.stops[0].color);
    }
  } else if (box->has_explicit_bg_color && iface->set_bg_color) {
    iface->set_bg_color(renderer, widget, box->bg_color);
  }
}

// Initialize layout engine
bool layout_engine_init(void) { return true; }

// Cleanup layout engine
void layout_engine_cleanup(void) {
  // Nothing to clean up yet
}

// Create a new layout node
LayoutNode *layout_node_create(ElementType type) {
  LayoutNode *node = (LayoutNode *)tb_calloc(1, sizeof(LayoutNode));
  if (!node)
    return NULL;

  node->type = type;
  node->box = DEFAULT_BOX;

  // Set display properties based on element type
  switch (type) {
  case ELEMENT_HEADING1:
    node->box.font_size = 32;
    node->box.line_height = 40;
    node->box.margin.top = 20;
    node->box.margin.bottom = 16;
    node->box.is_block = true;
    break;

  case ELEMENT_HEADING2:
    node->box.font_size = 24;
    node->box.line_height = 32;
    node->box.margin.top = 16;
    node->box.margin.bottom = 12;
    node->box.is_block = true;
    break;

  case ELEMENT_HEADING3:
    node->box.font_size = 20;
    node->box.line_height = 28;
    node->box.margin.top = 12;
    node->box.margin.bottom = 10;
    node->box.is_block = true;
    break;

  case ELEMENT_HEADING4:
  case ELEMENT_HEADING5:
  case ELEMENT_HEADING6:
    node->box.font_size = 16;
    node->box.line_height = 24;
    node->box.margin.top = 10;
    node->box.margin.bottom = 8;
    node->box.is_block = true;
    break;

  case ELEMENT_PARAGRAPH:
    node->box.margin.top = 8;
    node->box.margin.bottom = 8;
    node->box.is_block = true;
    break;

  case ELEMENT_DIV:
  case ELEMENT_CONTAINER:
    node->box.is_block = true;
    node->box.padding = (BoxSpacing){0, 0, 0, 0};
    break;

  case ELEMENT_UNORDERED_LIST:
  case ELEMENT_ORDERED_LIST:
    node->box.margin.top = 8;
    node->box.margin.bottom = 8;
    node->box.padding.left = 24;
    node->box.is_block = true;
    break;

  case ELEMENT_LIST_ITEM:
    node->box.margin.bottom = 4;
    node->box.is_block = true;
    break;

  case ELEMENT_SPAN:
  case ELEMENT_STRONG:
  case ELEMENT_EM:
  case ELEMENT_BOLD:
  case ELEMENT_ITALIC:
  case ELEMENT_UNDERLINE:
  case ELEMENT_LINK:
    node->box.is_block = false;
    node->box.padding = (BoxSpacing){0, 0, 0, 0};
    node->box.margin = (BoxSpacing){0, 0, 0, 0};
    break;

  case ELEMENT_BUTTON:
    node->box.padding = (BoxSpacing){8, 16, 8, 16};
    node->box.margin = (BoxSpacing){4, 4, 4, 4};
    node->box.is_inline_block = true;
    break;
  case ELEMENT_INPUT_TEXT:
    node->box.padding = (BoxSpacing){6, 10, 6, 10};
    node->box.margin = (BoxSpacing){6, 6, 6, 6};
    node->box.is_inline_block = true;
    node->box.width = 240;
    node->box.width_auto = false;
    node->box.height = 32;
    node->box.height_auto = false;
    break;
  case ELEMENT_TEXTAREA:
    node->box.padding = (BoxSpacing){8, 8, 8, 8};
    node->box.margin = (BoxSpacing){8, 0, 8, 0};
    node->box.is_block = true;
    node->box.width_auto = true;
    node->box.height = 120;
    node->box.height_auto = false;
    break;
  case ELEMENT_BREAK:
    node->box.is_block = true;
    node->box.height = 10;
    node->box.height_auto = false;
    break;

  case ELEMENT_HORIZONTAL_RULE:
    node->box.is_block = true;
    node->box.height = 2;
    node->box.height_auto = false;
    node->box.margin.top = 12;
    node->box.margin.bottom = 12;
    break;

  default:
    node->box.is_block = true;
    break;
  }

  return node;
}

// Destroy layout node and its children
void layout_node_destroy(LayoutNode *node) {
  if (!node)
    return;

  // Destroy children recursively
  LayoutNode *child = node->first_child;
  while (child) {
    LayoutNode *next = child->next_sibling;
    layout_node_destroy(child);
    child = next;
  }

  free(node->text_content);
  free(node->href);
  free(node->href_resolved);
  free(node->href_path);
  free(node->form_value);
  free(node->placeholder);
  free(node);
}

// Add child node
void layout_node_add_child(LayoutNode *parent, LayoutNode *child) {
  if (!parent || !child)
    return;

  child->parent = parent;

  if (!parent->first_child) {
    parent->first_child = child;
  } else {
    LayoutNode *last = parent->first_child;
    while (last->next_sibling) {
      last = last->next_sibling;
    }
    last->next_sibling = child;
  }
}

/* 排版视口宽度：渲染阶段据此计算缩放系数 */
static int s_layoutWidth = 0;

/* 排版入口必须调用它记录视口宽度。
   注意：dom_renderer 的 build_layout_only 路径不经过 layout_context_create()，
   若不显式调用本函数，s_layoutWidth 恒为 0 → 缩放系数恒为 1.0 → 整页只能露出左上角。 */
void layout_set_viewport_width(int width) { s_layoutWidth = width; }

/* 屏幕内容区宽度（本项目 464）。由 UI 侧在进入浏览器时告知，
   dom_renderer 处理 <meta viewport width=device-width> 时用它作为目标宽度。 */
static int s_screenWidth = 0;
void layout_set_screen_width(int width) { s_screenWidth = width; }
int layout_get_screen_width(void) { return s_screenWidth; }

// Initialize layout context
LayoutContext *layout_context_create(RenderContext *render_ctx) {
  LayoutContext *ctx = (LayoutContext *)calloc(1, sizeof(LayoutContext));
  if (!ctx)
    return NULL;

  ctx->render_context = render_ctx;
  ctx->container_width = render_ctx->max_width;
  ctx->current_x = 0;
  ctx->current_y = 0;
  ctx->max_line_height = 0;

  /* 记住本次排版使用的视口宽度：渲染阶段要用它算缩放系数。
     排版用宽视口（如 1024，桌面页面的设计宽度），渲染时再等比压到屏幕宽度，
     这样整页能"挤"进 480px 的屏幕，而不是只露出左上角一小块。 */
  s_layoutWidth = render_ctx->max_width;

  return ctx;
}

// Destroy layout context
void layout_context_destroy(LayoutContext *ctx) {
  if (!ctx)
    return;

  if (ctx->root) {
    layout_node_destroy(ctx->root);
  }

  free(ctx);
}

// Parse CSS spacing value (e.g., "10px", "10px 20px", "10px 20px 30px 40px")
BoxSpacing layout_parse_spacing(const char *value) {
  BoxSpacing spacing = {0, 0, 0, 0};
  if (!value)
    return spacing;

  int values[4] = {0, 0, 0, 0};
  int count = 0;

  const char *p = value;
  while (*p && count < 4) {
    while (*p && isspace((unsigned char)*p))
      p++;
    if (!*p)
      break;

    values[count++] = atoi(p);

    while (*p && !isspace((unsigned char)*p))
      p++;
  }

  // Apply CSS spacing rules
  if (count == 1) {
    // All sides
    spacing.top = spacing.right = spacing.bottom = spacing.left = values[0];
  } else if (count == 2) {
    // Vertical | Horizontal
    spacing.top = spacing.bottom = values[0];
    spacing.left = spacing.right = values[1];
  } else if (count == 3) {
    // Top | Horizontal | Bottom
    spacing.top = values[0];
    spacing.left = spacing.right = values[1];
    spacing.bottom = values[2];
  } else if (count == 4) {
    // Top | Right | Bottom | Left
    spacing.top = values[0];
    spacing.right = values[1];
    spacing.bottom = values[2];
    spacing.left = values[3];
  }

  return spacing;
}

// Apply CSS property to layout box
void layout_apply_css_property(LayoutBox *box, const char *property,
                               const char *value) {
  if (!box || !property || !value)
    return;

  if (strcmp(property, "margin") == 0) {
    box->margin = layout_parse_spacing(value);
  } else if (strcmp(property, "margin-top") == 0) {
    box->margin.top = atoi(value);
  } else if (strcmp(property, "margin-right") == 0) {
    box->margin.right = atoi(value);
  } else if (strcmp(property, "margin-bottom") == 0) {
    box->margin.bottom = atoi(value);
  } else if (strcmp(property, "margin-left") == 0) {
    box->margin.left = atoi(value);
  } else if (strcmp(property, "padding") == 0) {
    box->padding = layout_parse_spacing(value);
  } else if (strcmp(property, "padding-top") == 0) {
    box->padding.top = atoi(value);
  } else if (strcmp(property, "padding-right") == 0) {
    box->padding.right = atoi(value);
  } else if (strcmp(property, "padding-bottom") == 0) {
    box->padding.bottom = atoi(value);
  } else if (strcmp(property, "padding-left") == 0) {
    box->padding.left = atoi(value);
  } else if (strcmp(property, "width") == 0) {
    if (strcmp(value, "auto") == 0) {
      box->width_auto = true;
    } else {
      box->width = atoi(value);
      box->width_auto = false;
    }
  } else if (strcmp(property, "height") == 0) {
    if (strcmp(value, "auto") == 0) {
      box->height_auto = true;
    } else {
      box->height = atoi(value);
      box->height_auto = false;
    }
  } else if (strcmp(property, "font-size") == 0) {
    box->font_size = atoi(value);
    if (box->line_height < box->font_size + 4) {
      box->line_height = box->font_size + 6;
    }
  } else if (strcmp(property, "line-height") == 0) {
    box->line_height = atoi(value);
  } else if (strcmp(property, "display") == 0) {
    if (strcmp(value, "block") == 0) {
      box->is_block = true;
      box->is_inline_block = false;
    } else if (strcmp(value, "inline") == 0) {
      box->is_block = false;
      box->is_inline_block = false;
    } else if (strcmp(value, "inline-block") == 0) {
      box->is_block = false;
      box->is_inline_block = true;
    }
  } else if (strcmp(property, "text-align") == 0) {
    if (strcmp(value, "center") == 0) {
      box->text_align = 1;
    } else if (strcmp(value, "right") == 0) {
      box->text_align = 2;
    } else {
      box->text_align = 0;
    }
  } else if (strcmp(property, "color") == 0) {
    uint32_t parsed = 0;
    if (layout_parse_css_color(value, &parsed)) {
      box->color = parsed;
      box->has_explicit_color = true;
    }
  } else if (strcmp(property, "background") == 0 ||
             strcmp(property, "background-image") == 0) {
    LinearGradientFill gradient = {0};
    if (layout_parse_linear_gradient(value, &gradient)) {
      box->background.type = BACKGROUND_FILL_LINEAR_GRADIENT;
      box->background.data.linear = gradient;
      box->bg_color = gradient.stops[0].color;
      box->has_explicit_bg_color = true;
    } else {
      uint32_t parsed = 0;
      if (layout_parse_css_color(value, &parsed)) {
        box->bg_color = parsed;
        box->has_explicit_bg_color = true;
        box->background.type = BACKGROUND_FILL_SOLID;
      }
    }
  } else if (strcmp(property, "background-color") == 0) {
    uint32_t parsed = 0;
    if (layout_parse_css_color(value, &parsed)) {
      box->bg_color = parsed;
      box->has_explicit_bg_color = true;
      box->background.type = BACKGROUND_FILL_SOLID;
    }
  }
}

// Calculate total width including padding, border, margin
int layout_get_total_width(const LayoutBox *box) {
  return box->margin.left + box->border.left + box->padding.left + box->width +
         box->padding.right + box->border.right + box->margin.right;
}

// Calculate total height including padding, border, margin
int layout_get_total_height(const LayoutBox *box) {
  return box->margin.top + box->border.top + box->padding.top + box->height +
         box->padding.bottom + box->border.bottom + box->margin.bottom;
}

// Estimate text width (simple approximation)
static int estimate_text_width(const char *text, int font_size) {
  if (!text)
    return 0;
  // Rough approximation: character width ≈ font_size * 0.6
  return (int)(strlen(text) * font_size * 0.6);
}

// Wrap text to fit within max_width
TextLines *layout_wrap_text(const char *text, int max_width, int font_size) {
  if (!text)
    return NULL;

  TextLines *result = (TextLines *)calloc(1, sizeof(TextLines));
  if (!result)
    return NULL;

  int text_len = strlen(text);
  if (text_len == 0)
    return result;

  // Allocate initial arrays
  int capacity = 4;
  result->lines = (char **)calloc(capacity, sizeof(char *));
  result->line_widths = (int *)calloc(capacity, sizeof(int));

  const char *start = text;
  const char *end = text + text_len;

  while (start < end) {
    // Skip leading whitespace
    while (start < end && isspace((unsigned char)*start))
      start++;
    if (start >= end)
      break;

    // Find line break point
    const char *line_end = start;
    const char *last_space = NULL;
    int current_width = 0;

    while (line_end < end) {
      if (*line_end == '\n') {
        break;
      }

      // Check if adding this character exceeds max width
      int char_width = (int)(font_size * 0.6);
      if (current_width + char_width > max_width && last_space) {
        line_end = last_space;
        break;
      }

      if (isspace((unsigned char)*line_end)) {
        last_space = line_end;
      }

      current_width += char_width;
      line_end++;
    }

    // Ensure we have capacity
    if (result->line_count >= capacity) {
      capacity *= 2;
      result->lines =
          (char **)realloc(result->lines, capacity * sizeof(char *));
      result->line_widths =
          (int *)realloc(result->line_widths, capacity * sizeof(int));
    }

    // Copy line
    int line_len = line_end - start;
    result->lines[result->line_count] = (char *)malloc(line_len + 1);
    memcpy(result->lines[result->line_count], start, line_len);
    result->lines[result->line_count][line_len] = '\0';
    result->line_widths[result->line_count] =
        estimate_text_width(result->lines[result->line_count], font_size);
    result->line_count++;

    start = line_end;
    if (start < end && *start == '\n')
      start++;
  }

  return result;
}

// Free text lines
void layout_free_text_lines(TextLines *lines) {
  if (!lines)
    return;

  for (int i = 0; i < lines->line_count; i++) {
    free(lines->lines[i]);
  }
  free(lines->lines);
  free(lines->line_widths);
  free(lines);
}

// Calculate dimensions for a node
void layout_calculate_dimensions(LayoutNode *node, int available_width) {
  if (!node)
    return;

  // Calculate content width
  if (node->box.width_auto) {
    int inner_width = available_width - node->box.margin.left -
                      node->box.margin.right - node->box.padding.left -
                      node->box.padding.right - node->box.border.left -
                      node->box.border.right;

    if (node->box.is_block) {
      // Block elements take full available width
      node->box.width = inner_width > 0 ? inner_width : available_width;
    } else {
      // Inline elements fit to content
      if (node->text_content) {
        node->box.width =
            estimate_text_width(node->text_content, node->box.font_size);
      } else {
        node->box.width = 0;
      }
    }
  }

  // Calculate content height
  if (node->box.height_auto) {
    if (node->text_content) {
      // Wrap text and calculate height
      int content_width = node->box.width;
      TextLines *lines = layout_wrap_text(node->text_content, content_width,
                                          node->box.font_size);
      if (lines) {
        node->box.height = lines->line_count * node->box.line_height;
        layout_free_text_lines(lines);
      } else {
        node->box.height = node->box.line_height;
      }
    } else if (node->first_child) {
      // Calculate height based on children
      int child_height = 0;
      LayoutNode *child = node->first_child;
      while (child) {
        layout_calculate_dimensions(child, node->box.width);
        child_height += layout_get_total_height(&child->box);
        child = child->next_sibling;
      }
      node->box.height = child_height;
    } else {
      node->box.height = node->box.line_height;
    }
  }
}

/* ── 定位 + 行内换行 ──────────────────────────────────────────────────
 * 旧逻辑：行内兄弟一路 child_x += 总宽，从不回头 —— 一行能排到几千 px 宽，
 * 屏幕上只剩左上角一小块；加上 label 是 lv_pct(100) 宽（见 lvgl_renderer），
 * 只要 x>0 就必然戳出右边界。这就是"排版混乱 / 左右越界"的根因。
 * 现在：行内元素放不下就回到行首（居左）往下换行。
 * 只在左右方向限制；上下方向不做任何限制，该多长就多长，靠滚动看。 */
void layout_position_node(LayoutNode *node, int parent_x, int parent_y) {
  if (!node)
    return;

  // Calculate position including margins
  node->box.x = parent_x + node->box.margin.left;
  node->box.y = parent_y + node->box.margin.top;

  if (!node->first_child)
    return;

  const int content_left =
      node->box.x + node->box.padding.left + node->box.border.left;
  int child_x = content_left;
  int child_y = node->box.y + node->box.padding.top + node->box.border.top;

  /* 右边界：容器没显式宽时退回屏幕内容宽 */
  const int content_w = (node->box.width > 0)
                            ? node->box.width
                            : (s_screenWidth > 0 ? s_screenWidth : 0);
  const int right_limit = node->box.x + content_w;
  int line_h = 0;

  LayoutNode *child = node->first_child;
  while (child) {
    const int cw = layout_get_total_width(&child->box);
    const int ch = layout_get_total_height(&child->box);

    /* 行内元素塞不进本行 → 回到行首、往下挪一行 */
    if (!child->box.is_block && content_w > 0 && cw > 0 &&
        child_x > content_left && child_x + cw > right_limit) {
      child_x = content_left;
      child_y += (line_h > 0) ? line_h : node->box.line_height;
      line_h = 0;
    }

    layout_position_node(child, child_x, child_y);

    if (child->box.is_block) {
      child_y += ch;
      line_h = 0;
    } else {
      child_x += cw;
      if (ch > line_h) line_h = ch;
    }

    child = child->next_sibling;
  }
}


/* 布局意图：判断 div 是否应为行容器（flex row）。
   规则：div 有 ≥2 个"简单"子节点（link/span/只含文本的div）且无块级子节点（p/h1-h6/ul/ol）→ row。
   典型场景：导航栏 <div><div>Logo</div><div>Menu</div></div>、底部链接 <div><a>L1</a><a>L2</a></div>。
   非行场景：新闻列表 <div><div><p>News1</p></div><div><p>News2</p></div></div> → column。 */
static bool layout_should_be_row(LayoutNode *node) {
  if (!node || (node->type != ELEMENT_DIV && node->type != ELEMENT_CONTAINER))
    return false;

  int row_candidate_count = 0;
  int block_child_count = 0;
  LayoutNode *child = node->first_child;
  while (child) {
    /* 块级元素 → column */
    if (child->type == ELEMENT_PARAGRAPH ||
        (child->type >= ELEMENT_HEADING1 && child->type <= ELEMENT_HEADING6) ||
        child->type == ELEMENT_UNORDERED_LIST ||
        child->type == ELEMENT_ORDERED_LIST) {
      block_child_count++;
    }

    /* 行候选：inline 元素 */
    if (child->type == ELEMENT_LINK || child->type == ELEMENT_SPAN ||
        child->type == ELEMENT_STRONG || child->type == ELEMENT_EM ||
        child->type == ELEMENT_BOLD || child->type == ELEMENT_BUTTON) {
      row_candidate_count++;
    } else if (child->type == ELEMENT_DIV || child->type == ELEMENT_CONTAINER) {
      /* div 若只含文本/链接（无 p/h1-h6 子节点）→ 行候选；否则 → block */
      bool has_block_descendant = false;
      LayoutNode *gc = child->first_child;
      while (gc) {
        if (gc->type == ELEMENT_PARAGRAPH ||
            (gc->type >= ELEMENT_HEADING1 && gc->type <= ELEMENT_HEADING6)) {
          has_block_descendant = true;
          break;
        }
        gc = gc->next_sibling;
      }
      if (has_block_descendant)
        block_child_count++;
      else
        row_candidate_count++;
    }

    child = child->next_sibling;
  }

  /* ≥2 个行候选且无块级子节点 → row */
  return row_candidate_count >= 2 && block_child_count == 0;
}

/* 布局意图：trim 文本前导空格（dom_renderer 已 trim，但 Lexbor 有时会残留 \n + 空格） */
static char *layout_trim_text(const char *text) {
  if (!text)
    return NULL;
  const char *start = text;
  while (*start && isspace((unsigned char)*start))
    start++;
  if (*start == '\0')
    return NULL;
  size_t len = strlen(start);
  while (len > 0 && isspace((unsigned char)start[len - 1]))
    len--;
  if (len == 0)
    return NULL;
  char *result = (char *)malloc(len + 1);
  if (!result)
    return NULL;
  memcpy(result, start, len);
  result[len] = '\0';
  return result;
}

/* widget 数量限制。
   2026-09-23 实测（480x480，LVGL 池 128KB）：
     屏壳常驻        ~23 KB（used 18%）
     40 个网页 widget ~15 KB（40 个 → used 30%）
     推算 150 个    ~57 KB → used 约 61%，池仍有 ~51KB 余量
   所以 150 是安全的。原值 80 是当初 DRAM 紧张时的保守闸门，
   但它并非内存保护 —— 它只是把第 81 个及以后的内容**直接丢弃**，
   这才是页面"破碎"的真正原因。详见 docs/06。
   2026-09-23 二修：修掉"外层元素有文本就吞掉整棵子树"的 bug 后，
   节点能真正走到底，150 会在长页面上撞顶。按上面同一套推算，
   200 个 ≈ 76KB → used 约 78%，池仍有 ~28KB 余量。 */
#define MAX_WIDGETS 200
static int s_widgetCount = 0;
/* 平铺诊断 dump 的行上限（串口 `flatdump <n>` 可调）。
   默认 60：再多就刷屏，且会拖慢渲染。查「下一页」这类排在
   后面的行时把它调大（实测必应分页在第 60 行之后）。 */
static int s_flatDumpLimit = 60;

/* 平铺：胶囊最多画多长的文字（字节）。超过就不当"小按钮"了，退回普通整行文本，
   免得一整句话被塞进一个框里。24 个汉字 / 72 个字母以内算小按钮。 */
#define FLAT_CHIP_MAX_BYTES 72

/* 平铺：当前正在往里塞胶囊的那个 flex row-wrap 行容器。
   遇到任何非胶囊控件（普通文本/输入框）就置空 —— 那是区块边界，要另起一行。
   渲染跑在 UI 任务里、单线程，用文件静态量即可。 */
static void *s_rowContainer = NULL;

static bool flat_wants_chip(LayoutNode *n) {
  if (n->type != ELEMENT_LINK && n->type != ELEMENT_BUTTON)
    return false;
  if (!n->text_content || !n->text_content[0])
    return false;
  return (strlen(n->text_content) <= FLAT_CHIP_MAX_BYTES);
}

/* 点击后要跳的目标 URL。
   ⚠️ **必须优先绝对 URL**：href_path 是 tactilebrowser_extract_path() 去掉
   scheme+host 后的纯路径（"/s?word=x"），拿去重新 fetch 连主机都没有，必然失败。
   href_resolved 才是 dom_renderer 用 document_url 解析过的完整地址。
   只有它缺失时才退到原始 href（可能是相对路径，app 层再做一次兜底解析）。 */
static const char *flat_link_target(LayoutNode *n) {
  if (n->href_resolved && n->href_resolved[0]) return n->href_resolved;
  if (n->href && n->href[0])                   return n->href;
  if (n->href_path && n->href_path[0])         return n->href_path;
  return NULL;
}

/* 平铺模式开关。完整说明见文件后段 layout_flatten_tree() 上方的注释块；
   这里提前定义，因为 layout_render_node() 要读它。 */
static bool s_flatMode = true;

static void layout_render_node(LayoutNode *node, RenderContext *render_ctx,
                               void *parent_widget, int depth) {
  if (depth > MAX_LAYOUT_DEPTH) return;
  if (!node || !render_ctx || !render_ctx->renderer)
    return;

  /* widget 数量超限时停止创建新 widget，但仍递归子节点（已有容器内平铺） */
  bool widget_limit_reached = (s_widgetCount >= MAX_WIDGETS);

  RenderInterface *iface = render_ctx->renderer->interface;
  if (!iface)
    return;

  Renderer *renderer = render_ctx->renderer;
  void *parent = parent_widget ? parent_widget : render_ctx->root_container;
  void *widget = NULL;


  const char *form_value = node->form_value ? node->form_value : "";
  const char *placeholder = node->placeholder ? node->placeholder : NULL;

  void *saved_parent = renderer->platform_data;
  if (parent) {
    renderer->platform_data = parent;
  }

  /* 输入框 **和 textarea** 都渲染成单行输入框。
     ⚠️ 2026-09-23：搜索框并不一定是 <input> —— 必应的是
        <textarea id="sb_form_q" type="search" rows="1">，当年为了省 DRAM
        把 textarea 整个跳过，结果必应里**根本看不见搜索框**。
        现在 LVGL 池已在 PSRAM、DRAM 有 250KB，这个限制不成立。 */
  if ((node->type == ELEMENT_INPUT_TEXT || node->type == ELEMENT_TEXTAREA) &&
      iface->create_text_input && !widget_limit_reached) {
    /* textarea 的 form_value 是它的**整段 innerText**（可能几十 KB），
       原样塞给单行输入框会拖慢渲染，截到 256 B 足够看。 */
    char *capped = NULL;
    const char *shown_value = form_value;
    if (shown_value && strlen(shown_value) > 256) {
      capped = (char *)malloc(257);
      if (capped) {
        memcpy(capped, shown_value, 256);
        capped[256] = '\0';
        shown_value = capped;
      }
    }
    node->widget = iface->create_text_input(
        render_ctx->renderer, shown_value, placeholder, node->box.x, node->box.y,
        node->box.width, node->box.height);
    free(capped);
    widget = node->widget;
    if (widget) s_widgetCount++;
    s_rowContainer = NULL;            /* 输入框是区块，胶囊行到此为止 */
    layout_apply_background_fill(iface, render_ctx->renderer, &node->box,
                                 widget);
  } else if (s_flatMode && flat_wants_chip(node) && !widget_limit_reached &&
             iface->create_chip && iface->create_row_wrap) {
    /* 平铺：链接/小按钮 → 带框胶囊，排进一个 flex row-wrap 行里。
       这样"能点"看得出来，导航条也还是横的一排（放不下自动换行）。 */
    char *trimmed_text = layout_trim_text(node->text_content);
    if (trimmed_text && trimmed_text[0]) {
      if (!s_rowContainer) {
        void *saved = renderer->platform_data;
        renderer->platform_data = parent;
        s_rowContainer = iface->create_row_wrap(renderer, render_ctx->max_width);
        renderer->platform_data = saved;
        if (s_rowContainer) s_widgetCount++;
      }
      if (s_rowContainer) {
        void *saved = renderer->platform_data;
        renderer->platform_data = s_rowContainer;
        node->widget = iface->create_chip(renderer, trimmed_text,
                                          render_ctx->max_width - 8,
                                          node->box.color);
        renderer->platform_data = saved;
        widget = node->widget;
        if (widget) {
          s_widgetCount++;
          if (s_widgetCount <= s_flatDumpLimit) {
            Serial.printf("[Flat] %3d [%s]\n", s_widgetCount - 1, trimmed_text);
          }
          if (iface->set_text_color)
            iface->set_text_color(renderer, widget, node->box.color);
          if (node->type == ELEMENT_LINK && iface->register_link_handler) {
            const char *link_target = flat_link_target(node);
            if (link_target && link_target[0] != '\0')
              iface->register_link_handler(renderer, widget, link_target);
          }
        }
      }
    }
    free(trimmed_text);
  } else if (node->text_content && strlen(node->text_content) > 0 && !widget_limit_reached) {
    /* 布局意图：trim 前导/尾部空格，避免开头空格太多 */
    char *trimmed_text = layout_trim_text(node->text_content);
    if (trimmed_text) {
      /* 平铺装饰：列表项加 "- " 前缀。平铺后没有缩进也没有项目符号，
         不加标记就和普通段落完全一样，看不出这是列表。 */
      char *shown = trimmed_text;
      char *decorated = NULL;
      if (s_flatMode && node->type == ELEMENT_LIST_ITEM) {
        size_t n = strlen(trimmed_text) + 3;
        decorated = (char *)malloc(n);
        if (decorated) {
          snprintf(decorated, n, "- %s", trimmed_text);
          shown = decorated;
        }
      }

      /* 平铺下按钮也走 label：create_button 造的是固定 70x35 的 lv_btn，
         中文两三个字就被切掉，还不如一行满宽文本 */
      if (node->type == ELEMENT_BUTTON && iface->create_button && !s_flatMode) {
        node->widget = iface->create_button(
            render_ctx->renderer, shown, node->box.x, node->box.y);
      } else if (iface->create_label) {
        node->widget = iface->create_label(
            render_ctx->renderer, shown, node->box.x, node->box.y);
        /* 搜索结果：条目之间空一格 + 一条分隔线，一眼分得出哪条是哪条 */
        if (s_flatMode && node->type == ELEMENT_LIST_ITEM && node->widget &&
            iface->style_result_item)
          iface->style_result_item(render_ctx->renderer, node->widget);
        /* 诊断：平铺模式下把实际渲染出来的每一行打到串口（只打前 60 行）。
           没有它就没法确认"行内合并"到底有没有把导航条并成一行。 */
        if (s_flatMode && s_widgetCount <= s_flatDumpLimit) {
          Serial.printf("[Flat] %3d %c %s\n", s_widgetCount,
                        (node->type == ELEMENT_LINK) ? 'L' : ' ', shown);
        }
      }
      free(decorated);
      free(trimmed_text);
    }

    widget = node->widget;
    if (widget) s_widgetCount++;
    if (widget)
      s_rowContainer = NULL;        /* 普通整行文本是区块，胶囊行到此为止 */

    if (widget && iface->set_text_color) {
      iface->set_text_color(render_ctx->renderer, widget, node->box.color);
    }
    /* 布局意图：应用 text-align（0=left, 1=center, 2=right） */
    if (widget && iface->set_text_align && node->box.text_align != 0) {
      iface->set_text_align(render_ctx->renderer, widget, node->box.text_align);
    }
    layout_apply_background_fill(iface, render_ctx->renderer, &node->box,
                                 widget);

    if (widget && iface->register_link_handler) {
      const char *link_target = flat_link_target(node);
      if (link_target && link_target[0] != '\0') {
        iface->register_link_handler(render_ctx->renderer, widget, link_target);
      }
    }
  } else if (node->type == ELEMENT_DIV || node->type == ELEMENT_CONTAINER) {

    bool reuse_parent =
        (node->parent == NULL && parent == render_ctx->root_container);

    if (s_flatMode) {
      /* 平铺：div 不产生任何盒子，子节点直接挂到最近的真实容器。
         只有根节点复用 root_container，其余一律 widget = NULL。 */
      if (reuse_parent)
        node->widget = parent;
      widget = node->widget;
    } else {
      /* 布局意图：有 bg_color 或 text_align 或显式宽高 → 创建容器；
         行布局（多个子 div/link）→ 创建容器；
         无样式 div → 透明传递，子节点直接平铺到 parent */
      bool has_layout_intent = node->box.has_explicit_bg_color ||
                               node->box.text_align != 0 ||
                               (node->box.width > 0 && !node->box.width_auto);
      bool should_be_row = layout_should_be_row(node);
      if (reuse_parent) {
        node->widget = parent;
        /* 根容器若是行布局，切换 flex 方向 */
        if (should_be_row && iface->set_flex_direction) {
          iface->set_flex_direction(render_ctx->renderer, parent, 2);
        }
      } else if ((has_layout_intent || should_be_row) && iface->create_container && !widget_limit_reached) {
        node->widget = iface->create_container(render_ctx->renderer, node->box.x,
                                               node->box.y, node->box.width,
                                               node->box.height);
        if (node->widget) s_widgetCount++;
      }

      widget = node->widget;
      if (widget && widget != parent) {
        layout_apply_background_fill(iface, render_ctx->renderer, &node->box,
                                     widget);
        /* 容器也应用 text_align */
        if (iface->set_text_align && node->box.text_align != 0) {
          iface->set_text_align(render_ctx->renderer, widget, node->box.text_align);
        }
        /* 行布局：切换为 flex row */
        if (should_be_row && iface->set_flex_direction) {
          iface->set_flex_direction(render_ctx->renderer, widget, 2);
        }
      }
    }
  }

  renderer->platform_data = saved_parent;


  void *next_parent = widget ? widget : parent;

  LayoutNode *child = node->first_child;
  static int s_renderCount = 0;  /* 渲染节点计数器，定期让 CPU 喘气 */
  while (child) {
    layout_render_node(child, render_ctx, next_parent,
                     depth + 1);
    child = child->next_sibling;
    /* 每 30 个节点让 Core1 WiFi 任务喘 1ms */
    if (++s_renderCount % 30 == 0) vTaskDelay(1);
  }
}

/* ── 整页缩放 ──
   排版按宽视口（如 1024 = 桌面页面的设计宽度）进行，屏幕只有 ~460px 宽。
   不缩放的话，宽视口排出来的内容会横向溢出，屏幕上只剩左上角一小块。
   这里在渲染前把整棵布局树的几何量等比压缩到屏幕宽度，让整页"挤"进屏幕。
   注意：LVGL 字体是离散点阵（本项目中文只有 16px），文字无法跟着连续缩放，
   所以缩放只作用于几何（位置/尺寸/间距），文字仍走最小可读字号。 */
static void layout_scale_spacing(BoxSpacing *s, float f) {
  s->top = (int)(s->top * f);
  s->right = (int)(s->right * f);
  s->bottom = (int)(s->bottom * f);
  s->left = (int)(s->left * f);
}

static void layout_scale_tree(LayoutNode *node, float f) {
  if (!node) return;
  while (node) {
    LayoutBox *b = &node->box;
    b->x = (int)(b->x * f);
    b->y = (int)(b->y * f);
    if (b->width > 0) {
      b->width = (int)(b->width * f);
      if (b->width < 1) b->width = 1;
    }
    if (b->height > 0) {
      b->height = (int)(b->height * f);
      if (b->height < 1) b->height = 1;
    }
    if (b->font_size > 0) {
      b->font_size = (int)(b->font_size * f);
      if (b->font_size < 6) b->font_size = 6;
    }
    if (b->line_height > 0) {
      b->line_height = (int)(b->line_height * f);
      if (b->line_height < 6) b->line_height = 6;
    }
    layout_scale_spacing(&b->margin, f);
    layout_scale_spacing(&b->padding, f);
    layout_scale_spacing(&b->border, f);

    layout_scale_tree(node->first_child, f);
    node = node->next_sibling;
  }
}

/* ── 横向钳制（兜底）────────────────────────────────────────────────────
 * 换行只能处理"行内兄弟排得太长"，挡不住显式宽高超大、负 margin、以及
 * 缩放后残留的越界。这里在缩放之后统一把每个盒子夹进 [0, maxW]：
 *   · 比屏幕还宽的盒子 → 砍到屏幕宽（标签是 lv_pct(100)，光挪 x 没用）
 *   · 左边越界 → 拉回 0
 *   · 右边越界 → 整体左移到贴住右边界
 * 例外：x < -maxW 的认为是站点刻意藏到屏外（skip-link 之类），不去动它。
 * 上下方向完全不碰。 */
static void layout_clamp_horizontal(LayoutNode *node, int maxW) {
  if (!node || maxW <= 0) return;
  while (node) {
    LayoutBox *b = &node->box;

    if (b->width > maxW) b->width = maxW;
    if (b->x >= -maxW && b->x < 0) b->x = 0;
    if (b->width > 0 && b->x + b->width > maxW) {
      int nx = maxW - b->width;
      b->x = (nx > 0) ? nx : 0;
    }

    layout_clamp_horizontal(node->first_child, maxW);
    node = node->next_sibling;
  }
}

/* ── 平铺模式 ────────────────────────────────────────────────────────────
 * 2026-09-23：放弃"还原 CSS 版面"这条路，改为手工平铺。
 *
 * 为什么 CSS 版面救不回来：www.baidu.com 有 400 个节点，但排出来 contentH 只有
 * 187px —— 因为 PC 站大量用 float / absolute / flex，本引擎一个都不支持，
 * 绝大多数块被算成 0 高，页面直接塌掉。横向钳制救不了"高度为 0"。
 *
 * 平铺为什么能根治"排版混乱"（代码层面三件事同时消失）：
 *   1) create_label 根本不用 x/y —— lvgl_renderer 里是 (void)x; (void)y;，
 *      位置交给父容器的 flex 排。所以几何量算错对 label 没影响。
 *   2) 唯一吃绝对坐标的是 create_container（lv_obj_set_pos(x, y)）。
 *      CSS 算出来的 x/y/height 在本引擎里大量是 0 或离谱值 → 容器互相压盖、
 *      零高度、子元素整体不可见。这就是"搜索框不见了""整块内容空白"的真凶。
 *   3) should_be_row 还会把容器切成 LV_FLEX_FLOW_ROW → 内容一路往右溢出，
 *      配合 label 的 lv_pct(100) 必然戳出右边界。
 *   不建容器 → 1/2/3 一起消失，且左右方向天然不可能越界（label 恒为满宽）。
 *
 * 结果：每个文本/链接/输入框都是满宽、居左、自上而下的一块，靠滚动看。
 * 这就是 master 要的"按 div 换行 / 平铺"。 */
void layout_set_flat_mode(bool on) { s_flatMode = on; }
void layout_set_flat_dump_limit(int n) {
  s_flatDumpLimit = (n < 0) ? 0 : (n > 2000 ? 2000 : n);
}
bool layout_get_flat_mode(void) { return s_flatMode; }

/* 平铺模式的几何规范化：只改造渲染器真正会用的那几个字段。 */
static void layout_flatten_tree(LayoutNode *node, int maxW) {
  if (!node) return;
  while (node) {
    LayoutBox *b = &node->box;

    b->x = 0;                 /* 一律居左 */
    b->text_align = 0;
    b->is_block = true;       /* 不再有"行内并排"，全部各占一行 */
    b->is_inline_block = false;
    if (maxW > 0)
      b->width = maxW;

    /* 字号/行高给可读下限：CSS 里常见的 12px 会把行高压没 */
    if (b->font_size < 12)
      b->font_size = 14;
    if (b->line_height < b->font_size + 4)
      b->line_height = b->font_size + 6;

    /* 配色：链接蓝、标题白、正文灰 —— 平铺下没有版面，只能靠颜色分层 */
    if (!b->has_explicit_color) {
      if (node->type == ELEMENT_LINK)
        b->color = 0x6AB7FF;
      else if (node->type >= ELEMENT_HEADING1 && node->type <= ELEMENT_HEADING6)
        b->color = 0xFFFFFF;
      else
        b->color = 0xCCCCCC;
    }

    /* 输入框：平铺里给满宽单行，别再用 CSS 的 240x32（也别用多行 textarea） */
    if (node->type == ELEMENT_INPUT_TEXT || node->type == ELEMENT_TEXTAREA) {
      b->width = (maxW > 24) ? (maxW - 24) : maxW;
      b->height = 40;
      b->height_auto = false;
      b->width_auto = false;
    }

    layout_flatten_tree(node->first_child, maxW);
    node = node->next_sibling;
  }
}

/* ── 垃圾过滤（平铺模式下的"少渲染"规则集）──────────────────────────────
 * 480x480 的屏上一行就是钱。这些文本对"看内容"毫无贡献：
 *   - 加载/刷新状态占位（"正在加载"、"正在刷新"、"上滑加载更多"…）
 *   - 纯分隔符（"|" "·" ">" 这类导航条里用来分隔的零碎节点）
 * 命中就把它 text_content 清掉 → 渲染时自然不产控件，子树不受影响。
 */
static const char *kFlatJunkPhrases[] = {
    "正在刷新",   "正在加载",   "正在编译",   "正在解析",   "正在渲染",
    "正在获取",   "正在提交",   "正在请求",   "正在打开",   "正在跳转",
    "加载中",     "刷新中",     "上滑加载更多", "下拉加载更多", "上拉加载更多",
    "下拉刷新",   "上拉刷新",   "下拉加载",   "上拉加载",   "加载更多",
    /* 必应 SERP 的壳子文本（有些是 span 不是链接，只能按文本杀） */
    "切换到国际版", "时间不限", "搜索工具", "分页",
    NULL,
};

/* ASCII 垃圾短语（大小写不敏感） */
static const char *kFlatJunkPhrasesAscii[] = {"loading", "please wait",
                                              "just a moment", NULL};

/* 广告 / 推广标记（2026-09-24）。
   ⚠️ 必须带**长度闸门**：搜「广告投放」时结果摘要里满是"广告"二字，那是正经内容。
   真正当标记用的都是独立短节点（「广告」6 字节、「推广」6 字节），
   所以超过 16 字节的一律放过。 */
#define FLAT_AD_TEXT_MAX_BYTES 16
static const char *kFlatAdPhrases[] = {"广告", "推广", "赞助", "广告信息",
                                       "商业推广", "推广链接", NULL};

/* 页脚法定文本：备案号 / 隐私 / 条款 / 版权。
   比广告标记长 ——「京ICP备05002793号-1」≈20 字节，「隐私政策 | 服务条款」≈20 字节，
   闸门放到 40。再长的正文里出现"条款"是正常的，不杀。 */
#define FLAT_FOOTER_TEXT_MAX_BYTES 40
static const char *kFlatFooterPhrases[] = {
    "备案",     "公网安备",   "隐私",     "条款",     "版权所有",
    "著作权",   "法律声明",   "免责声明", "侵权投诉", "用户协议",
    "意见反馈", NULL};

/* ASCII 广告标记（长度闸门同中文那档，略放宽到 24 以容纳 "all rights reserved" 的前半） */
static const char *kFlatAdPhrasesAscii[] = {"sponsored", "advertisement",
                                            "ad choices", "ads by", NULL};
static const char *kFlatFooterPhrasesAscii[] = {
    "all rights reserved", "privacy policy", "terms of service",
    "cookie policy",       "privacy",        "terms of",
    "copyright",           "icp",            NULL};

/* 短文本转小写到栈上（<256 B 才转，够用且不上堆） */
static void flat_lower_copy(char *out, const char *s, size_t n) {
  for (size_t i = 0; i <= n; i++) {
    char c = s[i];
    out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
  }
}

static bool flat_is_ad_text(const char *s) {
  size_t n = strlen(s);
  if (n == 0 || n > FLAT_AD_TEXT_MAX_BYTES) return false;
  /* ⚠️ 必须**全等**（trim 后 strcmp），不能用 strstr：
     2026-09-24 实测搜「广告投放」，子串规则把 '广告投放' / '投放广告'
     各丢了 6 次 —— 那是搜索词和相关搜索，是正经内容。
     广告标记是徽章，文本就是「广告」二字，全等才不会误伤。 */
  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
  for (int i = 0; kFlatAdPhrases[i]; i++)
    if (strcmp(s, kFlatAdPhrases[i]) == 0) return true;
  char low[64];
  flat_lower_copy(low, s, strlen(s));
  for (int i = 0; kFlatAdPhrasesAscii[i]; i++)
    if (strcmp(low, kFlatAdPhrasesAscii[i]) == 0) return true;
  return false;
}

static bool flat_is_footer_text(const char *s) {
  size_t n = strlen(s);
  if (n == 0 || n > FLAT_FOOTER_TEXT_MAX_BYTES) return false;
  for (int i = 0; kFlatFooterPhrases[i]; i++)
    if (strstr(s, kFlatFooterPhrases[i])) return true;
  char low[64];
  flat_lower_copy(low, s, n);
  for (int i = 0; kFlatFooterPhrasesAscii[i]; i++)
    if (strstr(low, kFlatFooterPhrasesAscii[i])) return true;
  return false;
}

static bool flat_is_separator_only(const char *s) {
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '|' ||
        *p == '>' || *p == '/' || *p == '\\' || *p == '-' || *p == '_') {
      p++;
      continue;
    }
    if (*p < 0x80)
      return false;                     /* 其它 ASCII 实字 */
    /* 非 ASCII：把 UTF-8 整字跳过去。· — » 这类全角分隔符单独判 */
    int nb = 2;
    if ((*p & 0xF0) == 0xE0)
      nb = 3;
    else if ((*p & 0xF8) == 0xF0)
      nb = 4;
    if (nb == 3 && p[1] == 0xC2 && p[2] == 0xB7)
      ;                                 /* "·" 中点，算分隔符 */
    else
      return false;
    p += nb;
  }
  return true;                          /* 全是分隔符/空白 */
}

static bool flat_is_junk_text(const char *s) {
  if (!s || !s[0])
    return true;
  if (flat_is_separator_only(s))
    return true;
  if (flat_is_ad_text(s))     return true;
  if (flat_is_footer_text(s)) return true;
  for (int i = 0; kFlatJunkPhrases[i]; i++) {
    if (strstr(s, kFlatJunkPhrases[i]))
      return true;
  }
  /* ASCII 短语：临时转小写再比（文本短，栈上够用） */
  size_t n = strlen(s);
  if (n < 256) {
    char low[256];
    for (size_t i = 0; i <= n; i++) {
      char c = s[i];
      low[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    for (int i = 0; kFlatJunkPhrasesAscii[i]; i++) {
      if (strstr(low, kFlatJunkPhrasesAscii[i]))
        return true;
    }
  }
  return false;
}

/* 必应的时间筛选链接（filters=ex1...）：本机上点了没效果（必应对我们的 UA
   直接忽略 filters 参数，实测结果与不带筛选完全相同），只会挤掉真结果。 */
static bool flat_is_serp_chrome_link(LayoutNode *n) {
  if (!n) return false;
  const char *h = n->href_resolved ? n->href_resolved
                                   : (n->href ? n->href : n->href_path);
  if (!h) return false;
  /* 「下一页」是必应唯一的真翻页入口，必须留（FORM=PORE）。
     ⚠️ 实测提醒：必应对我们这种无 JS、无历史 cookie 的客户端只给一份固定
     ~10 条的语料，即使带 FPIG token + first=11 结果也 90% 重叠。留着它是
     因为这是官方入口，删了用户就彻底没得翻；但别指望它能翻出新东西。 */
  if (strstr(h, "FORM=PORE") != NULL) return false;
  if (strstr(h, "filters=ex1") != NULL) return true;   /* 24小时/一周/一个月/去年 */
  if (strstr(h, "qpvt=") != NULL) return true;         /* 「全部」「时间不限」「网页」 */
  if (strstr(h, "FORM=HDRSC") != NULL) return true;    /* 顶部导航：图片/视频/学术/词典/航班 */
  if (strstr(h, "first=") != NULL) {
    /* 数字页码「2」「3」：只在**文本是纯数字**时才删。
       ⚠️ 不能无条件删：<li class="b_pag"> 这个分页容器会从子树继承
       href_resolved（dom_renderer.cpp 的 subtree_first_href 只对 li 做），
       它的 href 同样带 first=，无条件删会把整块分页连坐掉，「下一页」
       也就跟着没了（2026-09-23 实测踩到）。容器文本是「分页123下一页」
       这种长的，纯数字判断正好把它排除。 */
    const char *t = n->text_content;
    if (!t || !t[0]) return true;                 /* 无文本：纯壳子，删 */
    size_t len = strlen(t);
    if (len > 3) return false;                    /* 太长 = 容器，放过 */
    for (size_t i = 0; i < len; i++) {
      if (t[i] < '0' || t[i] > '9') return false;
    }
    return true;                                  /* 1~3 位纯数字 = 页码 */
  }
  static const char *kSerpVerticals[] = {
      "/images/search", "/videos/search", "/academic/search",
      "/dict/search",   "/travel/search", "/maps/", NULL};
  for (int i = 0; kSerpVerticals[i]; i++) {
    if (strstr(h, kSerpVerticals[i]) != NULL) return true;
  }
  return false;
}
/* ── 伪链接判定：这些 href 点了本就不该有反应 ──────────────────────────────
 * 现代站点的导航里塞满了 javascript: void(0);（纯 JS 下拉菜单占位按钮）、
 * mailto:/tel:（唤起别的应用）、#anchor（页内锚点）。
 *
 * 2026-09-23 实测（乐鑫官网）：一页 197 个"可点链接"里大半是这类东西，
 * 而 widget 配额只有 MAX_WIDGETS=200 —— 菜单把配额吃光，正文
 * （id="main" 落在 54.9% 处）永远排不到号，表现就是"页面加载了但什么都看不到"。
 *
 * ⚠️ 难点：这些 href 常被拼上域名前缀，变成
 *    https://host/path/javascript: void(0);
 * 所以除了看前缀，还得看**子串**。
 */
static bool flat_is_pseudo_href(const char* h) {
  if (!h || !h[0]) return true;
  if (h[0] == '#') return true;                 /* 页内锚点 */
  static const char* kSchemes[] = {
      "javascript:", "mailto:", "tel:", "data:", "about:", "blob:",
      "sms:", "intent:", "weixin:", "alipays:", nullptr};
  for (int i = 0; kSchemes[i]; i++) {
    size_t n = strlen(kSchemes[i]);
    if (strncasecmp(h, kSchemes[i], n) == 0) return true;
  }
  if (strstr(h, "javascript:") != NULL) return true;
  if (strstr(h, "void(0)") != NULL) return true;
  return false;
}

static int layout_drop_junk(LayoutNode *node, int depth) {
  if (depth > MAX_LAYOUT_DEPTH) return 0;
  if (!node)
    return 0;
  int n = 0;
  for (LayoutNode *c = node->first_child; c; c = c->next_sibling)
    n += layout_drop_junk(c, depth + 1);
  if (node->text_content && flat_is_junk_text(node->text_content)) {
    /* 广告/页脚命中值得看一眼（分隔符合并类命中太多，不打） */
    static int s_junkLog = 0;
    if (s_junkLog < 24 &&
        (flat_is_ad_text(node->text_content) ||
         flat_is_footer_text(node->text_content))) {
      Serial.printf("[Junk] drop '%s'\n", node->text_content);
      s_junkLog++;
    }
    free(node->text_content);
    node->text_content = NULL;
    n++;
  }
  if (flat_is_serp_chrome_link(node)) {
    if (node->text_content) { free(node->text_content); node->text_content = NULL; }
    if (node->href)          { free(node->href);         node->href = NULL; }
    if (node->href_resolved) { free(node->href_resolved); node->href_resolved = NULL; }
    if (node->href_path)     { free(node->href_path);     node->href_path = NULL; }
    n++;
  }
  /* 伪链接（JS 菜单占位 / 锚点 / mailto:）整块不渲染：
     点了也不会有反应，却占着 MAX_WIDGETS 的配额。清掉 text_content
     渲染时就不再产控件 —— 省下的配额留给正文。 */
  if (node->href && flat_is_pseudo_href(node->href)) {
    if (node->text_content) { free(node->text_content); node->text_content = NULL; }
    if (node->href)          { free(node->href);         node->href = NULL; }
    if (node->href_resolved) { free(node->href_resolved); node->href_resolved = NULL; }
    if (node->href_path)     { free(node->href_path);     node->href_path = NULL; }
    n++;
  }
  return n;
}

/* ── 行内合并（平铺模式下的"行"规则集）──────────────────────────────────
 * 纯平铺会把「我的关注」「我的收藏」「皮肤中心」「用户反馈」这种导航条拆成四行，
 * 一屏就废了。规则：
 *   1. 参与合并的只有行内元素（a/span/strong/em/b/i/u），块级元素各占一行；
 *   2. 同一"种类"才合并：链接只跟链接并，普通文本只跟普通文本并
 *      —— 否则整行会因为其中一个是链接而全染成蓝色，语义就糊了；
 *   3. 按估算宽度折行：累计超过容器宽就另起一行（估算按全角=字号、半角=0.5字号）；
 *   4. 分隔符：链接行用 " · "（导航条观感），普通文本行用空格；
 *   5. 合并是把后续兄弟的文本**并进第一个兄弟**，后者 text_content 置空 →
 *      渲染时它自然不再产生 label。不需要动渲染器。
 *   6. **无文字的纯结构壳子对行透明**：<ul><li><a>…</a></li>…</ul> 里每个 <a>
 *      都是各自 <li> 的独子，只看"同一层兄弟"永远并不起来（导航条拆四行的真正
 *      原因）。所以合并遇到"自己不写字、只包子节点"的壳子时钻进去继续同一行。
 *   7. **跨壳只有链接能并**：判断依据是节点的**直接父节点**是否相同
 *      （`FlatRow::ownerParent`）。同父 → 按规则 2 正常并；不同父 → 只有链接
 *      （kind=1）能继续并。因为普通文本跨壳会把「设置」和「©2026 Baidu…」这种
 *      两个区块的东西串成一行（实测过，一屏从 14 行并到 7 行，串味明显）。
 *   8. 一行最多 FLAT_ROW_MAX_ITEMS 项，防止整页链接被 " · " 串成一坨。
 */
static int flat_text_width(const char *s, int fs) {
  if (!s)
    return 0;
  int w = 0;
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    if (*p < 0x80) {          /* ASCII / 半角 */
      w += (int)(fs * 0.5f);
      p += 1;
    } else {                  /* 全角（中文/日文/全角标点） */
      int nb = 2;
      if ((*p & 0xF0) == 0xE0)
        nb = 3;
      else if ((*p & 0xF8) == 0xF0)
        nb = 4;
      w += fs;
      p += nb;
    }
  }
  return w;
}

/* 0 = 普通文本，1 = 链接，-1 = 不参与合并（块级） */
static int flat_inline_kind(LayoutNode *n) {
  if (!n->text_content || !n->text_content[0])
    return -1;
  switch (n->type) {
  case ELEMENT_LINK:
    /* 链接**不参与合并**了：平铺下链接会被画成带框的小胶囊（chip），
       一个个排进 flex row-wrap 行里。并成 " · " 一串就画不了框，也点不了。 */
    return -1;
  case ELEMENT_LIST_ITEM:
    /* 导航条 <li> 也参与合并。li 本身不是链接，但如果 dom 层把它当纯文本抽了，
       里面那个 <a> 就没了（见 dom_renderer 的 li_is_link_wrapper）——
       那种情况按普通文本并，至少能横排。 */
    return 0;
  case ELEMENT_SPAN:
  case ELEMENT_STRONG:
  case ELEMENT_EM:
  case ELEMENT_BOLD:
  case ELEMENT_ITALIC:
  case ELEMENT_UNDERLINE:
    return 0;
  default:
    return -1;
  }
}

/* 自己没有可见文字（NULL 或纯空白） */
static bool flat_is_textless(LayoutNode *n) {
  const char *t = n ? n->text_content : NULL;
  if (!t)
    return true;
  for (const unsigned char *p = (const unsigned char *)t; *p; p++) {
    if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
      return false;               /* 有实字 → 它是正文，不是壳子 */
  }
  return true;
}

/* 纯结构壳子：自己没有可见文字、但包着子节点。
 * <ul><li><a>我的关注</a></li><li><a>我的收藏</a></li>…</ul> 里的 <li>、
 * 包着一堆 <span> 的页脚 <div>，都是这种壳子。
 * 叶子节点（输入框/图片/按钮）不算壳子 —— 它是内容本身。 */
static bool flat_is_transparent(LayoutNode *n) {
  if (!n || !n->first_child)
    return false;
  return flat_is_textless(n);
}

/* 一行最多并多少个。不设上限的话，整页的链接会被 " · " 串成一坨糊。 */
#define FLAT_ROW_MAX_ITEMS 8

typedef struct {
  LayoutNode *leader;       /* 当前行的"组长"，后续兄弟的文本并进它 */
  LayoutNode *ownerParent;  /* 组长的直接父节点 —— 用来判断这次合并是不是"跨壳" */
  int kind;                 /* 组长种类：0 普通文本 / 1 链接 */
  int width;                /* 当前行已用宽度（估算） */
  int items;                /* 当前行已并进来的条数 */
} FlatRow;

static void merge_row(LayoutNode *parent, int maxW, FlatRow *row);

static void merge_child(LayoutNode *parent, LayoutNode *c, int maxW, FlatRow *row) {
  int kind = flat_inline_kind(c);

  if (kind < 0 && flat_is_transparent(c)) {
    /* 壳子：不打断当前行，钻进去按同一行继续 */
    merge_row(c, maxW, row);
    return;
  }

  if (kind >= 0 && maxW > 0) {
    int w = flat_text_width(c->text_content, c->box.font_size);

    bool sameRow = (row->leader != NULL);
    /* 跨壳（父节点不同）：只有链接能继续并。
       导航条 <ul><li><a>…</a></li>…</ul> 里每个 <a> 的爹是各自的 <li>，
       不跨壳合并就永远并不到一起；而普通文本跨壳会把「设置」和
       「©2026 Baidu…」这种两个区块的东西串成一行，所以只放行链接。 */
    if (sameRow && row->ownerParent != parent && kind != 1)
      sameRow = false;
    if (sameRow &&
        (row->width + (int)(row->leader->box.font_size * 0.8f) + w > maxW))
      sameRow = false;                                  /* 超宽 → 换行 */
    if (sameRow && row->items >= FLAT_ROW_MAX_ITEMS)
      sameRow = false;                                  /* 一行并够了 */
    if (sameRow && kind != row->kind)
      sameRow = false;                                  /* 种类不同 → 换行 */

    if (sameRow) {
      const char *sep = (kind == 1) ? " · " : " ";
      size_t need = strlen(row->leader->text_content) + strlen(sep) +
                    strlen(c->text_content) + 1;
      char *merged = (char *)realloc(row->leader->text_content, need);
      if (merged) {
        strcat(merged, sep);
        strcat(merged, c->text_content);
        row->leader->text_content = merged;
        row->width += (int)(row->leader->box.font_size * 0.8f) + w;
        row->items++;
        /* 被吞并的节点不再单独成行 */
        free(c->text_content);
        c->text_content = NULL;
        /* 并成了行的 li 不再是列表项：降级成 span，渲染时就不会加 "- " 前缀
           （横排导航条前面挂个 "- " 很怪）。 */
        if (row->leader->type == ELEMENT_LIST_ITEM)
          row->leader->type = ELEMENT_SPAN;
      }
    } else {
      row->leader = c;
      row->ownerParent = parent;
      row->kind = kind;
      row->width = w;
      row->items = 1;
    }
    /* 它自己的子节点另起一套行状态，不跟父层混 */
    FlatRow sub = {NULL, NULL, -1, 0, 0};
    merge_row(c, maxW, &sub);
  } else {
    row->leader = NULL;           /* 块级元素打断当前行 */
    row->ownerParent = NULL;
    row->kind = -1;
    row->width = 0;
    row->items = 0;
    FlatRow sub = {NULL, NULL, -1, 0, 0};
    merge_row(c, maxW, &sub);
  }
}

static void merge_row(LayoutNode *parent, int maxW, FlatRow *row) {
  if (!parent)
    return;
  for (LayoutNode *c = parent->first_child; c; c = c->next_sibling)
    merge_child(parent, c, maxW, row);
}

static void layout_merge_inline_rows(LayoutNode *node, int maxW) {
  if (!node)
    return;
  FlatRow row = {NULL, NULL, -1, 0, 0};
  merge_row(node, maxW, &row);
}

/* 布局树统计：节点数 + 内容总高（用于诊断"页面到底有多大"） */
static void layout_tree_stats(LayoutNode *node, int *count, int *maxBottom) {
  if (!node) return;
  while (node) {
    (*count)++;
    int bottom = node->box.y + node->box.height;
    if (bottom > *maxBottom)
      *maxBottom = bottom;
    layout_tree_stats(node->first_child, count, maxBottom);
    node = node->next_sibling;
  }
}

// Render layout tree to widgets
void layout_render_tree(LayoutNode *root, RenderContext *render_ctx) {
  if (!root || !render_ctx)
    return;
  s_widgetCount = 0;     /* 重置 widget 计数器 */
  s_rowContainer = NULL; /* 重置"当前胶囊行"，每次渲染从头开始 */
  lvgl_renderer_reset_link_count();

  int nodes = 0, heightBefore = 0;

  if (s_flatMode) {
    /* 平铺：不缩放、不钳制、不还原版面 */
    layout_tree_stats(root, &nodes, &heightBefore);
    layout_flatten_tree(root, render_ctx->max_width);
    int dropped = layout_drop_junk(root, 0);
    layout_merge_inline_rows(root, render_ctx->max_width);
    Serial.printf("[Browser] layout: FLAT tiles, nodes=%d width=%d junkDropped=%d\n",
                  nodes, render_ctx->max_width, dropped);
  } else {
    /* 缩放系数 = 屏幕可用宽 / 排版视口宽。只缩不放。 */
    float f = 1.0f;
    if (s_layoutWidth > 0 && render_ctx->max_width > 0) {
      f = (float)render_ctx->max_width / (float)s_layoutWidth;
    }
    if (f > 1.0f) f = 1.0f;    /* 视口比屏幕窄时无需放大 */
    if (f < 0.15f) f = 0.15f;  /* 下限，避免缩到不可见 */

    int heightAfter = 0;
    layout_tree_stats(root, &nodes, &heightBefore);
    if (f < 0.999f)
      layout_scale_tree(root, f);
    /* 兜底：钳进屏幕宽度（左右不越界，上下不管） */
    layout_clamp_horizontal(root, render_ctx->max_width);
    if (f < 0.999f)
      layout_tree_stats(root, &nodes, &heightAfter);
    else
      heightAfter = heightBefore;

    Serial.printf("[Browser] layout: viewport=%d screen=%d scale=%.2f nodes=%d contentH=%d->%d\n",
                  s_layoutWidth, render_ctx->max_width, (double)f, nodes,
                  heightBefore, heightAfter);
  }

  layout_render_node(root, render_ctx, render_ctx->root_container, 0);
  Serial.printf("[Browser] widgets created: %d (limit %d), clickable links=%d\n",
                s_widgetCount, MAX_WIDGETS, lvgl_renderer_link_count());
}
