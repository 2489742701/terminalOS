#include "dom_renderer.h"
#include "css_parser.h"
#include "html_parser.h"
#include "layout_engine.h"
#include "tactilebrowser_core.h"
#include "url_utils.h"
#include "lvgl_renderer.h"
#include <ctype.h>
#include <lexbor/dom/interfaces/node.h>
#include <stdlib.h>
#include <string.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* 协作式停止标志：DOM 遍历时定期检查，true 则中止构建 */
static volatile bool *g_stopFlag = nullptr;
void dom_renderer_set_stop_flag(volatile bool *flag) { g_stopFlag = flag; }

// Helper: Copy node text
static char *copy_node_text(lxb_dom_node_t *node, size_t *length) {
  if (!node) {
    if (length)
      *length = 0;
    return NULL;
  }

  size_t text_len = 0;
  lxb_char_t *text = lxb_dom_node_text_content(node, &text_len);
  if (length)
    *length = text_len;

  char *copy = NULL;
  if (text && text_len > 0) {
    copy = safe_strndup((const char *)text, text_len);
  }

  if (text) {
    lxb_dom_document_t *owner = node->owner_document;
    if (owner) {
      lxb_dom_document_destroy_text(owner, text);
    } else {
      free(text);
    }
  }

  return copy;
}

// Helper: Check if rel attribute contains "stylesheet"
static bool rel_contains_stylesheet(const char *rel, size_t length) {
  if (!rel || length == 0)
    return false;
  char *lowered = safe_strndup(rel, length);
  if (!lowered)
    return false;
  for (size_t i = 0; i < length; ++i) {
    lowered[i] = (char)tolower((unsigned char)lowered[i]);
  }
  bool is_stylesheet = strstr(lowered, "stylesheet") != NULL;
  free(lowered);
  return is_stylesheet;
}

/**
 * strip_icon_glyphs - 就地删除「图标字体」字符和不可见控制字符。
 *
 * 为什么必须做：百度/淘宝等站点用私有区码位（U+E000–U+F8FF）承载图标字体，
 * 例如百度搜索框外层 <span> 的文本就是一个 \ue610。我们的字体里没有这些字形，
 * 渲染出来是一堆豆腐块；更糟的是它让外层元素"有文本"从而吞掉整棵子树
 * （见 subtree_has_form_control 的注释）。
 *
 * 只处理 UTF-8 字节序列，不需要解码整个码点：
 *   U+E000..U+EFFF -> EE 80..BF ..
 *   U+F000..U+F8FF -> EF 80..A3 ..
 *   U+FFFD         -> EF BF BD
 *   U+200B..U+200F -> E2 80 8B..8F（零宽/方向控制）
 *   U+FEFF         -> EF BB BF（BOM）
 * NBSP(U+00A0 = C2 A0) 换成普通空格，避免被当成可见文本。
 */
static void strip_icon_glyphs(char *s) {
  if (!s)
    return;
  const unsigned char *p = (const unsigned char *)s;
  char *dst = s;
  while (*p) {
    unsigned char c = p[0];
    unsigned char c1 = p[1];
    unsigned char c2 = p[2];
    if (c == 0xEE && c1 >= 0x80) {           /* U+E000–U+EFFF */
      p += 3;
      continue;
    }
    if (c == 0xEF && c1 >= 0x80 && c1 <= 0xA3) {  /* U+F000–U+F8FF */
      p += 3;
      continue;
    }
    if (c == 0xEF && c1 == 0xBF && c2 == 0xBD) {  /* U+FFFD 替换字符 */
      p += 3;
      continue;
    }
    if (c == 0xEF && c1 == 0xBB && c2 == 0xBF) {  /* U+FEFF BOM */
      p += 3;
      continue;
    }
    if (c == 0xE2 && c1 == 0x80 && c2 >= 0x8B && c2 <= 0x8F) { /* 零宽/方向 */
      p += 3;
      continue;
    }
    if (c == 0xC2 && c1 == 0xA0) {           /* NBSP -> 空格 */
      *dst++ = ' ';
      p += 2;
      continue;
    }
    if (c < 0x20 && c != '\n' && c != '\t') { /* C0 控制符（保留换行/制表）*/
      p++;
      continue;
    }
    *dst++ = (char)c;
    p++;
  }
  *dst = '\0';
}

/**
 * subtree_has_form_control - 子树里是否含「可交互控件」。
 *
 * 为什么必须做：现代站点普遍写成
 *     <span class="ipt_wr">\ue610<input id="kw"></span>
 * 外层 span 的文本只是图标字符，但 build_layout_tree_from_dom 一旦判定
 * "该元素有文本"，就把它当纯文本叶子，**不再递归子节点** —— 输入框被整棵丢掉。
 * 百度首页搜不到搜索框就是这个原因（m.baidu.com 的 input 外层是 div，所以能活）。
 *
 * 判定命中则不抽取文本、继续递归子节点。
 */
static bool subtree_has_form_control(lxb_dom_node_t *node, int depth) {
  if (!node || depth > 12)
    return false;

  if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
    lxb_dom_element_t *element = (lxb_dom_element_t *)node;
    size_t tag_len = 0;
    const char *tag = html_parser.get_element_tag(element, &tag_len);
    if (tag) {
      if (tag_len == 5 && strncmp(tag, "input", 5) == 0) {
        /* hidden 不是给人看的，不算 */
        size_t type_len = 0;
        const char *type_attr =
            html_parser.get_element_attr(element, "type", &type_len);
        bool hidden = false;
        if (type_attr && type_len == 6) {
          char buf[8];
          memcpy(buf, type_attr, 6);
          buf[6] = '\0';
          for (size_t i = 0; i < 6; ++i)
            buf[i] = (char)tolower((unsigned char)buf[i]);
          hidden = (strcmp(buf, "hidden") == 0);
        }
        if (!hidden)
          return true;
      }
      if ((tag_len == 8 && strncmp(tag, "textarea", 8) == 0) ||
          (tag_len == 6 && strncmp(tag, "select", 6) == 0) ||
          (tag_len == 6 && strncmp(tag, "button", 6) == 0))
        return true;
    }
  }

  lxb_dom_node_t *child = html_parser.get_first_child(node);
  while (child) {
    if (subtree_has_form_control(child, depth + 1))
      return true;
    child = html_parser.get_next_sibling(child);
  }
  return false;
}

/**
 * li_is_link_wrapper - 这个 <li> 是否只是 <a> 的一层包装。
 *
 * 现代站点的导航条几乎都写成 <li><a href="...">我的关注</a></li>。
 * 这种情况下 li 自己没有独立文本，真正的可点目标在里面的 <a>。
 * 判定命中就让 li 放弃抽文本（继续递归），<a> 才能拿到 href、变蓝、将来可点。
 *
 * 判定：直接子元素中恰好有一个 <a>（忽略空白文本节点）。
 */
/**
 * subtree_first_href - 子树里第一个"值得点"的链接（返回绝对地址，需 free）。
 *
 * 为什么必须做：搜索结果 <li class="b_algo"> 会被判定"有文本"而抽成纯文本叶子，
 * 里面那个 <a href> 标题链接**根本不会进布局树** —— 整条结果就点不动。
 * 这里把子树里第一个真链接的绝对地址记到 <li> 上，渲染时挂到整条 label。
 * 跳过 # / javascript: / mailto: / tel:（点它们没有意义，还会被 app 层判为不可解析）。
 */
static char *subtree_first_href(lxb_dom_node_t *node, RenderContext *context,
                                int depth) {
  if (!node || depth > 8)
    return NULL;

  if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
    lxb_dom_element_t *element = (lxb_dom_element_t *)node;
    size_t tag_len = 0;
    const char *tag = html_parser.get_element_tag(element, &tag_len);
    if (tag && tag_len == 1 && (tag[0] == 'a' || tag[0] == 'A')) {
      size_t href_len = 0;
      const char *href_attr =
          html_parser.get_element_attr(element, "href", &href_len);
      if (href_attr && href_len > 0) {
        char *raw = safe_strndup(href_attr, href_len);
        if (raw) {
          bool bad = (raw[0] == '#') || (strncmp(raw, "javascript:", 11) == 0) ||
                     (strncmp(raw, "mailto:", 7) == 0) ||
                     (strncmp(raw, "tel:", 4) == 0);
          char *abs =
              bad ? NULL : tactilebrowser_resolve_url(context->document_url, raw);
          free(raw);
          if (abs)
            return abs;
        }
      }
    }
  }

  for (lxb_dom_node_t *c = html_parser.get_first_child(node); c;
       c = html_parser.get_next_sibling(c)) {
    char *r = subtree_first_href(c, context, depth + 1);
    if (r)
      return r;
  }
  return NULL;
}

/* li 里有没有 >=2 个"能各自成块"的子元素（搜索结果条目的判据）。
   只数元素节点；跳过 <link>/<script>/<style>/<meta>/<br>/<hr> —— 必应在
   b_algo 里塞了一长串 <link rel="stylesheet">，不跳过的话任意 li 都会被误判。 */
static bool li_has_block_children(lxb_dom_node_t *node) {
  int n = 0;
  for (lxb_dom_node_t *c = html_parser.get_first_child(node); c;
       c = html_parser.get_next_sibling(c)) {
    if (c->type != LXB_DOM_NODE_TYPE_ELEMENT)
      continue;
    lxb_dom_element_t *el = (lxb_dom_element_t *)c;
    size_t tl = 0;
    const char *tag = html_parser.get_element_tag(el, &tl);
    if (!tag || tl == 0)
      continue;
    /* 手写下划线比较，不依赖 strncasecmp（各平台头文件不一致） */
    bool skip = false;
    static const char *kNop[] = {"link", "script", "style", "meta", "br", "hr",
                                 NULL};
    for (int k = 0; kNop[k]; k++) {
      const char *p = kNop[k];
      size_t pl = strlen(p);
      if (pl != tl)
        continue;
      bool eq = true;
      for (size_t i = 0; i < tl; i++) {
        if (tolower((unsigned char)tag[i]) != p[i]) {
          eq = false;
          break;
        }
      }
      if (eq) {
        skip = true;
        break;
      }
    }
    if (skip)
      continue;
    /* nav/ul/ol/table 是结构性容器：出现一个就说明「里面还有一层」，
       不能把整块文本压成一行。必应的分页 <li class=b_pag> 就只有
       一个 <nav> 子元素，靠 n>=2 永远判不出来（2026-09-23 实测）。 */
    static const char *kContainers[] = {"nav", "ul", "ol", "table", NULL};
    for (int k = 0; kContainers[k]; k++) {
      size_t pl = strlen(kContainers[k]);
      if (pl != tl)
        continue;
      bool eq = true;
      for (size_t i = 0; i < tl; i++) {
        if (tolower((unsigned char)tag[i]) != kContainers[k][i]) {
          eq = false;
          break;
        }
      }
      if (eq)
        return true;
    }
    n++;
    if (n >= 2)
      return true;
  }
  return false;
}

static bool li_is_link_wrapper(lxb_dom_node_t *node) {
  int anchor_count = 0;
  int other_element_count = 0;

  for (lxb_dom_node_t *child = html_parser.get_first_child(node); child;
       child = html_parser.get_next_sibling(child)) {
    if (child->type != LXB_DOM_NODE_TYPE_ELEMENT)
      continue;
    lxb_dom_element_t *el = (lxb_dom_element_t *)child;
    size_t tag_len = 0;
    const char *tag = html_parser.get_element_tag(el, &tag_len);
    if (tag && tag_len == 1 && (tag[0] == 'a' || tag[0] == 'A'))
      anchor_count++;
    else
      other_element_count++;
  }

  return (anchor_count == 1 && other_element_count == 0);
}

/**
 * detect_meta_viewport - 读 <meta name="viewport" content="width=...">。
 *
 * 排版视口一直是写死的 1024（桌面设计宽）。移动端页面（m.baidu.com）按 ~375 设计，
 * 用 1024 排版再压缩到 464，等于把手机版面强行摊开又缩小，横向溢出、内容变矮。
 * 页面自己声明了视口宽度就听它的：
 *   width=<数字>   -> 用该数字
 *   width=device-width -> 用屏幕内容宽度（传入的 screen_w）
 * 没有声明（桌面站）-> 返回 0，调用方回退到桌面默认。
 */
static int detect_meta_viewport(lxb_html_document_t *document, int screen_w) {
  lxb_dom_element_t *root_el =
      lxb_dom_document_element(lxb_dom_interface_document(document));
  if (!root_el)
    return 0;

  lxb_dom_collection_t *collection =
      lxb_dom_collection_make(lxb_dom_interface_document(document), 8);
  if (!collection)
    return 0;

  int width = 0;
  if (lxb_dom_elements_by_tag_name(root_el, collection,
                                   (const lxb_char_t *)"meta", 4) ==
      LXB_STATUS_OK) {
    for (size_t i = 0; i < lxb_dom_collection_length(collection); ++i) {
      lxb_dom_element_t *el = lxb_dom_collection_element(collection, i);
      size_t name_len = 0;
      const char *name = html_parser.get_element_attr(el, "name", &name_len);
      if (!name || name_len != 8 || strncmp(name, "viewport", 8) != 0)
        continue;

      size_t content_len = 0;
      const char *content =
          html_parser.get_element_attr(el, "content", &content_len);
      if (!content || content_len == 0 || content_len > 255)
        continue;

      char buf[256];
      memcpy(buf, content, content_len);
      buf[content_len] = '\0';
      for (size_t k = 0; k < content_len; ++k)
        buf[k] = (char)tolower((unsigned char)buf[k]);

      char *w = strstr(buf, "width=");
      if (!w)
        continue;
      w += 6;
      if (strncmp(w, "device-width", 12) == 0) {
        width = screen_w;
      } else {
        width = atoi(w);
      }
      break;
    }
  }
  lxb_dom_collection_destroy(collection, true);
  return width;
}

static void parse_and_apply_css_block(LayoutBox *target_box,
                                      char *declarations) {
  if (!target_box || !declarations)
    return;

  char *saveptr = NULL;
  char *property = strtok_r(declarations, ";", &saveptr);
  while (property) {
    char *colon = strchr(property, ':');
    if (colon) {
      *colon = '\0';
      char *prop_name = property;
      char *prop_value = colon + 1;

      while (*prop_name && isspace((unsigned char)*prop_name))
        prop_name++;
      while (*prop_value && isspace((unsigned char)*prop_value))
        prop_value++;

      char *prop_end = prop_name + strlen(prop_name);
      while (prop_end > prop_name && isspace((unsigned char)*(prop_end - 1))) {
        *(--prop_end) = '\0';
      }

      char *val_end = prop_value + strlen(prop_value);
      while (val_end > prop_value && isspace((unsigned char)*(val_end - 1))) {
        *(--val_end) = '\0';
      }

      if (*prop_name && *prop_value) {
        layout_apply_css_property(target_box, prop_name, prop_value);
      }
    }
    property = strtok_r(NULL, ";", &saveptr);
  }
}

static void apply_css_block(LayoutBox *target_box, const char *declarations) {
  if (!target_box || !declarations)
    return;
  char *copy = safe_strdup(declarations);
  if (!copy)
    return;
  parse_and_apply_css_block(target_box, copy);
  free(copy);
}

// Helper: Import external stylesheet
static void import_external_stylesheet(const char *href, size_t href_len,
                                       const char *base_url) {
  if (!href || href_len == 0 || !html_parser.download_html)
    return;

  /* HTML 被截断时跳过外部 CSS 下载：截断的 HTML 可能引用不完整的 URL，
     且解析大 CSS 会长时间阻塞主循环，饿死 Core1 WiFi 任务导致看门狗崩溃。 */
  if (arduino_html_was_truncated()) {
    Serial.println("[Browser] HTML truncated, skipping external CSS");
    return;
  }

  /* 平铺模式压根不下载外部 CSS：平铺不还原版面，CSS 算出来的 x/y/width 一条都不用，
     下载它们只是白白串行做几十次 TLS 握手（cn.bing.com 实测 30+ 个 → 约 1 分钟）。
     内嵌 <style> 仍然解析，因为字体/颜色这类值还是会用到。 */
  if (layout_get_flat_mode()) {
    static int skipped = 0;
    if (skipped++ == 0)
      Serial.println("[Browser] flat mode: external CSS download skipped");
    return;
  }

  char *href_copy = safe_strndup(href, href_len);
  if (!href_copy)
    return;

  char *absolute_url = tactilebrowser_resolve_url(base_url, href_copy);
  free(href_copy);
  if (!absolute_url)
    return;

  MemoryBuffer css_buffer;
  memory_buffer_init(&css_buffer);
  RenderResult result = html_parser.download_html(absolute_url, &css_buffer);

  /* CSS 超过 64KB 跳过解析：百度首页 CSS 约 26KB，之前 16KB 限制导致 text-align 等属性丢失。
     Lexbor 内存已重定向到 PSRAM，64KB 解析没问题。解析在 Core1 不阻塞 Core0 WiFi。 */
  if (result == RENDER_SUCCESS && css_buffer.data && css_buffer.size > 0 &&
      css_buffer.size <= 65536) {
    css_parser_add_stylesheet(css_buffer.data, css_buffer.size);
  } else if (css_buffer.size > 65536) {
    Serial.printf("[Browser] CSS too large (%d bytes), skipping\n",
                  (int)css_buffer.size);
  }
  memory_buffer_free(&css_buffer);
  free(absolute_url);
  yield();  /* 让 Core1 WiFi 任务喘口气 */
}

// Collect all stylesheets from document
static void collect_stylesheets(lxb_dom_node_t *node, const char *base_url) {
  if (!node)
    return;

  if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
    lxb_dom_element_t *element = (lxb_dom_element_t *)node;
    size_t tag_len = 0;
    const char *tag = html_parser.get_element_tag(element, &tag_len);

    if (tag && tag_len == 5 && strncmp(tag, "style", 5) == 0) {
      /* HTML 截断时跳过内嵌 CSS：截断的 <style> 内容会让 Lexbor CSS 解析器崩溃 */
      if (!arduino_html_was_truncated()) {
        size_t css_len = 0;
        char *css_text = html_parser.get_element_text(element, &css_len);
        if (css_text && css_len > 0 && css_len <= 32768) {
          css_parser_add_stylesheet(css_text, css_len);
        }
        free(css_text);
      }
    } else if (tag && tag_len == 4 && strncmp(tag, "link", 4) == 0) {
      size_t rel_len = 0;
      const char *rel_attr =
          html_parser.get_element_attr(element, "rel", &rel_len);
      if (rel_contains_stylesheet(rel_attr, rel_len)) {
        size_t href_len = 0;
        const char *href_attr =
            html_parser.get_element_attr(element, "href", &href_len);
        if (href_attr && href_len > 0) {
          import_external_stylesheet(href_attr, href_len, base_url);
        }
      }
    }
  }

  lxb_dom_node_t *child = html_parser.get_first_child(node);
  while (child) {
    collect_stylesheets(child, base_url);
    child = html_parser.get_next_sibling(child);
  }
}

/* DOM 节点计数（诊断用，不限制数量：Lexbor 内存已重定向到 PSRAM） */
static int g_layoutNodeCount = 0;

static LayoutNode *build_layout_tree_from_dom(lxb_dom_node_t *dom_node,
                                              RenderContext *context) {
  if (!dom_node)
    return NULL;

  g_layoutNodeCount++;

  lxb_dom_node_type_t node_type = dom_node->type;

  // Handle text nodes
  if (node_type == LXB_DOM_NODE_TYPE_TEXT) {
    size_t len = 0;
    char *txt = copy_node_text(dom_node, &len);

    if (txt && len > 0) {
      strip_icon_glyphs(txt);  /* 去掉图标字体字符，别让它们变成豆腐块 */
      // Trim whitespace
      char *start = txt;
      char *end = txt + len - 1;
      while (start <= end && isspace((unsigned char)*start))
        start++;
      while (end >= start && isspace((unsigned char)*end))
        end--;

      if (start <= end) {
        LayoutNode *text_node = layout_node_create(ELEMENT_SPAN);
        if (text_node) {
          size_t trimmed_len = end - start + 1;
          text_node->text_content = (char *)malloc(trimmed_len + 1);
          memcpy(text_node->text_content, start, trimmed_len);
          text_node->text_content[trimmed_len] = '\0';
        }
        free(txt);
        return text_node;
      }
    }
    free(txt);
    return NULL;
  }

  // Handle element nodes only
  if (node_type != LXB_DOM_NODE_TYPE_ELEMENT) {
    return NULL;
  }

  lxb_dom_element_t *element = (lxb_dom_element_t *)dom_node;
  ElementType elem_type = get_element_type_from_element(element);

  // Skip scripts and styles
  size_t tag_len = 0;
  const char *tag = html_parser.get_element_tag(element, &tag_len);
  if (tag && ((tag_len == 6 && strncmp(tag, "script", 6) == 0) ||
              (tag_len == 5 && strncmp(tag, "style", 5) == 0))) {
    return NULL;
  }

  if (elem_type == ELEMENT_INPUT_TEXT) {
    bool treat_as_text_input = true;
    bool convert_to_button = false;
    size_t type_len = 0;
    const char *type_attr =
        html_parser.get_element_attr(element, "type", &type_len);
    if (type_attr && type_len > 0) {
      char *lowered = safe_strndup(type_attr, type_len);
      if (lowered) {
        for (size_t i = 0; i < type_len; ++i) {
          lowered[i] = (char)tolower((unsigned char)lowered[i]);
        }
        if (strcmp(lowered, "text") == 0 || strcmp(lowered, "search") == 0 ||
            strcmp(lowered, "url") == 0 || strcmp(lowered, "email") == 0 ||
            strcmp(lowered, "password") == 0 || strcmp(lowered, "tel") == 0) {
          treat_as_text_input = true;
        } else if (strcmp(lowered, "submit") == 0 ||
                   strcmp(lowered, "button") == 0) {
          convert_to_button = true;
          treat_as_text_input = false;
        } else {
          treat_as_text_input = false;
        }
        free(lowered);
      }
    }

    if (!treat_as_text_input && convert_to_button) {
      elem_type = ELEMENT_BUTTON;
    } else if (!treat_as_text_input) {
      return NULL;
    }
  }

  // Create layout node
  LayoutNode *layout_node = layout_node_create(elem_type);
  if (!layout_node)
    return NULL;

  // Determine if we should extract text from this element
  bool should_extract_text = false;
  switch (elem_type) {
  case ELEMENT_HEADING1:
  case ELEMENT_HEADING2:
  case ELEMENT_HEADING3:
  case ELEMENT_HEADING4:
  case ELEMENT_HEADING5:
  case ELEMENT_HEADING6:
  case ELEMENT_PARAGRAPH:
  case ELEMENT_LINK:
  case ELEMENT_BUTTON:
  case ELEMENT_SPAN:
  case ELEMENT_STRONG:
  case ELEMENT_EM:
  case ELEMENT_BOLD:
  case ELEMENT_ITALIC:
  case ELEMENT_UNDERLINE:
  case ELEMENT_LIST_ITEM:
    /* 2026-09-23：导航条 <li><a href>我的关注</a></li> 被当成纯文本叶子，
       里面的 <a> 连同 href 一起被丢掉 —— 平铺出来是四行白字，既没并成一行、
       也不是链接、将来还点不动。li 只是个 <a> 的包装时，不抽文本，让 <a> 自己活。 */
    /* 2026-09-23（二）：搜索结果 <li class="b_algo"> 同理，但它更复杂 ——
       里面是「来源行 / <a><h2>标题 / <p>摘要」三块，抽成纯文本叶子会把三块的
       innerText 首尾相接拼成一坨（espressif.comhttps://www.espressif.com...），
       480 屏上根本读不了，标题那个 <a> 还被一起丢掉（点不动）。
       li 里有 >=2 个块级子元素时不抽文本，递归下去让各块各自成行；
       标题 <a> 因此活下来，渲染成可点胶囊 —— 只有标题可点，不会误触。 */
    should_extract_text = !li_is_link_wrapper(dom_node) &&
                          !li_has_block_children(dom_node);
    break;
  case ELEMENT_INPUT_TEXT:
  case ELEMENT_TEXTAREA:
    should_extract_text = false;
    break;
  default:
    break;
  }

  /* 若子树里有可交互控件（input/textarea/select/button），就不能把本元素当纯文本
     叶子 —— 否则整棵子树连同输入框一起被丢弃。改为不抽文本、继续递归。 */
  if (should_extract_text && elem_type != ELEMENT_INPUT_TEXT &&
      elem_type != ELEMENT_TEXTAREA &&
      subtree_has_form_control(dom_node, 0)) {
    should_extract_text = false;
  }

  if (should_extract_text) {
    size_t text_len = 0;
    char *text = html_parser.get_element_text(element, &text_len);
    if (text && text_len > 0) {
      strip_icon_glyphs(text);  /* 去掉图标字体字符（百度 \ue610 之类） */
      // Trim whitespace
      char *start = text;
      char *end = text + text_len - 1;
      while (start <= end && isspace((unsigned char)*start))
        start++;
      while (end >= start && isspace((unsigned char)*end))
        end--;

      if (start <= end) {
        size_t trimmed_len = end - start + 1;
        layout_node->text_content = (char *)tb_alloc(trimmed_len + 1);
        memcpy(layout_node->text_content, start, trimmed_len);
        layout_node->text_content[trimmed_len] = '\0';
      }
    }
    free(text);
  }

  if (elem_type == ELEMENT_INPUT_TEXT) {
    size_t value_len = 0;
    const char *value_attr =
        html_parser.get_element_attr(element, "value", &value_len);
    if (value_attr && value_len > 0) {
      layout_node->form_value = safe_strndup(value_attr, value_len);
    }

    size_t placeholder_len = 0;
    const char *placeholder_attr =
        html_parser.get_element_attr(element, "placeholder", &placeholder_len);
    if (placeholder_attr && placeholder_len > 0) {
      layout_node->placeholder =
          safe_strndup(placeholder_attr, placeholder_len);
    }
  } else if (elem_type == ELEMENT_TEXTAREA) {
    size_t area_len = 0;
    char *textarea_text = html_parser.get_element_text(element, &area_len);
    if (textarea_text && area_len > 0) {
      layout_node->form_value = textarea_text;
    } else {
      free(textarea_text);
    }
  }

  /* 搜索结果整条可点：<li> 被抽成文本叶子后，里面的 <a> 就不在树里了。
     把子树里第一个链接的绝对地址记到 li 上，渲染时挂到整条 label 上。
     只对 li 做（不做 div）：div 动辄包裹半个页面，整块可点会变成误触制造机。 */
  if (elem_type == ELEMENT_LIST_ITEM && !layout_node->href_resolved &&
      !li_is_link_wrapper(dom_node)) {
    layout_node->href_resolved = subtree_first_href(dom_node, context, 0);
  }

  // Extract href for links
  if (elem_type == ELEMENT_LINK) {
    size_t href_len = 0;
    const char *href_attr =
        html_parser.get_element_attr(element, "href", &href_len);
    if (href_attr && href_len > 0) {
      layout_node->href = safe_strndup(href_attr, href_len);
      layout_node->box.color = 0x0000EE; // Blue for links

      if (layout_node->href) {
        char *absolute_url = tactilebrowser_resolve_url(context->document_url,
                                                        layout_node->href);
        if (absolute_url) {
          layout_node->href_resolved = absolute_url;
          char *path_only = tactilebrowser_extract_path(absolute_url);
          if (path_only) {
            layout_node->href_path = path_only;
          }
        }
      }
    }
  }

  // Apply tag-level selectors
  if (tag && tag_len > 0) {
    char *selector = safe_strndup(tag, tag_len);
    if (selector) {
      for (size_t i = 0; i < tag_len; ++i) {
        selector[i] = (char)tolower((unsigned char)selector[i]);
      }
      const char *css_block = css_parser_get_declarations(selector, tag_len);
      if (css_block) {
        apply_css_block(&layout_node->box, css_block);
      }
      free(selector);
    }
  }

  // Apply CSS classes
  size_t class_len = 0;
  const char *class_attr =
      html_parser.get_element_attr(element, "class", &class_len);
  if (class_attr && class_len > 0) {
    char *classes = safe_strndup(class_attr, class_len);
    if (classes) {
      char *saveptr = NULL;
      char *token = strtok_r(classes, " \t\r\n", &saveptr);
      while (token) {
        size_t token_len = strlen(token);
        if (token_len > 0) {
          size_t selector_len = token_len + 1;
          char *selector = (char *)malloc(selector_len + 1);
          if (selector) {
            selector[0] = '.';
            memcpy(selector + 1, token, token_len + 1);

            const char *css_block =
                css_parser_get_declarations(selector, selector_len);
            if (css_block) {
              apply_css_block(&layout_node->box, css_block);
            }
            free(selector);
          }
        }
        token = strtok_r(NULL, " \t\r\n", &saveptr);
      }
      free(classes);
    }
  }

  // Apply ID selector
  size_t id_len = 0;
  const char *id_attr = html_parser.get_element_attr(element, "id", &id_len);
  if (id_attr && id_len > 0) {
    char *selector = (char *)malloc(id_len + 2);
    if (selector) {
      selector[0] = '#';
      memcpy(selector + 1, id_attr, id_len);
      selector[id_len + 1] = '\0';
      const char *css_block = css_parser_get_declarations(selector, id_len + 1);
      if (css_block) {
        apply_css_block(&layout_node->box, css_block);
      }
      free(selector);
    }
  }

  // Apply inline styles
  size_t style_len = 0;
  const char *style_attr =
      html_parser.get_element_attr(element, "style", &style_len);
  if (style_attr && style_len > 0) {
    char *style_copy = safe_strndup(style_attr, style_len);
    if (style_copy) {
      parse_and_apply_css_block(&layout_node->box, style_copy);
      free(style_copy);
    }
  }

  // Process children (if we didn't extract text or text is empty)
  bool allow_children =
      (elem_type != ELEMENT_INPUT_TEXT && elem_type != ELEMENT_TEXTAREA);

  if (allow_children && (!should_extract_text || !layout_node->text_content ||
                         layout_node->text_content[0] == '\0')) {
    lxb_dom_node_t *child = html_parser.get_first_child(dom_node);
    static int s_nodeCount = 0;  /* DOM 遍历节点计数器，定期让 CPU 喘气 */
    while (child) {
      /* 协作式停止检查 */
      if (g_stopFlag && *g_stopFlag) {
        layout_node_destroy(layout_node);
        return NULL;
      }
      LayoutNode *child_layout = build_layout_tree_from_dom(child, context);
      if (child_layout) {
        layout_node_add_child(layout_node, child_layout);
      }
      child = html_parser.get_next_sibling(child);
      /* 每 50 个节点让 Core1 WiFi 任务喘 1ms，防止看门狗超时 */
      if (++s_nodeCount % 50 == 0) vTaskDelay(1);
    }
  }

  /* 过滤空容器：div/container 无文本、无子节点、无显式宽高和背景色 → 跳过。
     百度首页有大量嵌套空 div 只含 script，渲染出来是一堆空框。 */
  if ((elem_type == ELEMENT_DIV || elem_type == ELEMENT_CONTAINER) &&
      !layout_node->text_content &&
      !layout_node->first_child &&
      layout_node->box.width == 0 &&
      layout_node->box.height == 0 &&
      !layout_node->box.has_explicit_bg_color) {
    layout_node_destroy(layout_node);
    return NULL;
  }

  return layout_node;
}

/* 诊断：递归计算布局树节点数 */
static int count_layout_nodes(LayoutNode *node) {
  if (!node) return 0;
  int count = 1;
  LayoutNode *child = node->first_child;
  while (child) {
    count += count_layout_nodes(child);
    child = child->next_sibling;
  }
  return count;
}

static RenderResult dom_renderer_render_document(lxb_html_document_t *document,
                                                 RenderContext *context) {
  if (!document || !context || !context->renderer)
    return RENDER_ERROR_UNKNOWN;


  // Collect stylesheets - 完全重建 CSS 解析器，清除上一次加载的脏状态
  // （仅 css_parser_reset 不够，Lexbor CSS parser 内部状态会残留导致下次崩溃）
  css_parser_cleanup();
  css_parser_init();
  lxb_dom_element_t *root =
      lxb_dom_document_element(lxb_dom_interface_document(document));
  if (root) {
    collect_stylesheets(lxb_dom_interface_node(root), context->document_url);
  }

  // Get body
  lxb_dom_element_t *body = html_parser.find_body_element(document);
  if (!body)
    return RENDER_ERROR_PARSE;


  // Clear container
  if (context->renderer->interface->clear_container) {
    context->renderer->interface->clear_container(context->renderer,
                                                   context->root_container);
  }

  // Build layout tree from DOM
  g_layoutNodeCount = 0;  /* 重置节点计数器 */
  LayoutNode *layout_root =
      build_layout_tree_from_dom(lxb_dom_interface_node(body), context);

  if (layout_root) {
    // Calculate dimensions
    layout_calculate_dimensions(layout_root, context->max_width);

    // Position nodes
    layout_position_node(layout_root, 0, 10);

    // Render to screen
    layout_render_tree(layout_root, context);


    // Update context Y position
    context->current_y =
        layout_root->box.y + layout_get_total_height(&layout_root->box);

    // Cleanup
    layout_node_destroy(layout_root);
  }

  return RENDER_SUCCESS;
}

// Stub implementations for backwards compatibility (unused now)
static void dom_renderer_render_node(lxb_dom_node_t *node,
                                     RenderContext *context) {
  (void)node;
  (void)context;
}

static void *dom_renderer_create_element_widget(ElementType type,
                                                RenderContext *context,
                                                const char *text) {
  (void)type;
  (void)context;
  (void)text;
  return NULL;
}

static void dom_renderer_apply_styles(void *widget, RenderContext *context,
                                      const char *style) {
  (void)widget;
  (void)context;
  (void)style;
}

// Global DOM renderer instance
DomRendererInterface dom_renderer = {
    .render_document = dom_renderer_render_document,
    .render_node = dom_renderer_render_node,
    .create_element_widget = dom_renderer_create_element_widget,
    .apply_styles = dom_renderer_apply_styles};

// Initialize DOM renderer
bool dom_renderer_init(void) { return layout_engine_init(); }

/* ── 两阶段拆分：build（无 LVGL）+ render（LVGL）── */

/* Phase 1: 收集 CSS + 获取 body + 构建布局树 + 计算尺寸 + 定位。
   不触碰任何 LVGL 控件，可在后台任务中安全运行。 */
RenderResult dom_renderer_build_layout_only(lxb_html_document_t *document,
                                            RenderContext *context,
                                            LayoutNode **out_root) {
  if (!document || !context || !out_root)
    return RENDER_ERROR_UNKNOWN;
  *out_root = nullptr;

  /* 收集样式表（会下载外部 CSS，有 stop_flag 检查） */
  css_parser_cleanup();
  css_parser_init();
  lxb_dom_element_t *root_el =
      lxb_dom_document_element(lxb_dom_interface_document(document));
  if (root_el) {
    collect_stylesheets(lxb_dom_interface_node(root_el), context->document_url);
  }

  /* 协作式停止检查 */
  if (g_stopFlag && *g_stopFlag) return RENDER_ERROR_UNKNOWN;

  lxb_dom_element_t *body = html_parser.find_body_element(document);
  if (!body) return RENDER_ERROR_PARSE;

  g_layoutNodeCount = 0;
  LayoutNode *layout_root =
      build_layout_tree_from_dom(lxb_dom_interface_node(body), context);

  if (g_stopFlag && *g_stopFlag) {
    if (layout_root) layout_node_destroy(layout_root);
    return RENDER_ERROR_UNKNOWN;
  }

  if (!layout_root) return RENDER_ERROR_PARSE;

  /* 视口宽度：max_width <= 0 表示"自动"，交给 <meta viewport> 决定；
     桌面站没有该 meta，回退 1024。自动模式下移动端页面能按设计宽度排版，
     不再被 1024 摊开又整体缩到 0.45。 */
  int screen_w = layout_get_screen_width();
  if (screen_w <= 0)
    screen_w = 464;
  int vp = context->max_width;
  const bool vp_manual = (vp > 0);
  if (!vp_manual) {
    vp = detect_meta_viewport(document, screen_w);
    if (vp <= 0)
      vp = 1024;            /* 桌面站：无 viewport meta */
    if (vp < 320)
      vp = 320;
    if (vp > 2048)
      vp = 2048;
  }
  Serial.printf("[Browser] viewport=%d (%s) screen=%d\n", vp,
                vp_manual ? "manual" : "auto", screen_w);

  /* 记录本次排版所用视口宽度：渲染阶段要用它把整页等比压进屏幕。
     本函数路径不经过 layout_context_create()，必须显式记录，否则缩放恒为 1.0。 */
  layout_set_viewport_width(vp);
  layout_calculate_dimensions(layout_root, vp);
  layout_position_node(layout_root, 0, 10);

  *out_root = layout_root;
  return RENDER_SUCCESS;
}

/* Phase 2: 清空容器 + 渲染布局树到 LVGL 控件。
   快速，在 UI 任务中调用。 */
RenderResult dom_renderer_render_layout_only(LayoutNode *layout_root,
                                             RenderContext *context) {
  if (!layout_root || !context || !context->renderer)
    return RENDER_ERROR_UNKNOWN;

  /* 清空旧内容 */
  if (context->renderer->interface->clear_container) {
    context->renderer->interface->clear_container(context->renderer,
                                                  context->root_container);
  }

  layout_render_tree(layout_root, context);

  context->current_y =
      layout_root->box.y + layout_get_total_height(&layout_root->box);

  return RENDER_SUCCESS;
}

/* Phase 3: 释放布局树 */
void dom_renderer_free_layout(LayoutNode *root) {
  if (root) layout_node_destroy(root);
}

// Cleanup DOM renderer
void dom_renderer_cleanup(void) { layout_engine_cleanup(); }

// Main rendering function
RenderResult render_html_to_container(const char *url, RenderContext *context) {
  if (!url || !context)
    return RENDER_ERROR_UNKNOWN;

  // Download HTML
  MemoryBuffer buffer = {0};
  RenderResult download_result = html_parser.download_html(url, &buffer);
  if (download_result != RENDER_SUCCESS) {
    return download_result;
  }

  if (!buffer.data || buffer.size == 0) {
    return RENDER_ERROR_NETWORK;
  }


  // Parse HTML
  lxb_html_document_t *document =
      html_parser.parse_html(buffer.data, buffer.size);
  if (!document) {
    free(buffer.data);
    return RENDER_ERROR_PARSE;
  }
  yield();  /* HTML 解析耗时，让 Core1 WiFi 任务运行 */

  // Render document using THE POWER ENGINE
  RenderResult render_result = dom_renderer.render_document(document, context);
  yield();  /* 渲染耗时，让 Core1 WiFi 任务运行 */

  // Cleanup
  lxb_html_document_destroy(document);
  free(buffer.data);

  return render_result;
}
