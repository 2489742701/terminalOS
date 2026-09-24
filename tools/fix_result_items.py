# -*- coding: utf-8 -*-
"""搜索结果小优化：
  1) 结果条目之间"空一格"：条目底部留白 + 一条分隔线
  2) 结果条目可点：整条 <li> 也能带上它内部第一个 <a> 的绝对地址

⚠️ Edit 工具在本项目上已静默失败 5 次，这里一律用 python 行级手术 + 当场核对。
"""
import sys

BASE = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine"


def load(rel):
    p = BASE + "\\" + rel.replace("/", "\\")
    raw = open(p, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in raw else "\n"
    return p, raw.split(nl), nl


def save(p, lines, nl):
    open(p, "w", encoding="utf-8", newline="").write(nl.join(lines))


def find(lines, needle, start=0):
    for i in range(start, len(lines)):
        if needle in lines[i]:
            return i
    return -1


# ══ 1) common_types.h：RenderInterface 加 style_result_item ═══════════════
p, L, nl = load("include/common_types.h")
i = find(L, "void *(*create_chip)(Renderer *renderer, const char *text, int max_width,")
assert i > 0, "create_chip decl not found"
# 声明跨两行，找到结尾那一行
j = i
while ";" not in L[j]:
    j += 1
new = [
    "  /* 平铺专用：把一条\"搜索结果\"打扮成能一眼分辨的一块 —— 底部留白 + 一条分隔线。",
    "     480 屏上一行就是钱，条目挤在一起根本分不出哪条是哪条。 */",
    "  void (*style_result_item)(Renderer *renderer, void *widget);",
]
L[j + 1:j + 1] = new
save(p, L, nl)
print("common_types.h OK")

# ══ 2) lvgl_renderer.cpp：实现 + 注册 ════════════════════════════════════
p, L, nl = load("src/lvgl_renderer.cpp")
i = find(L, "LvglRenderer *lvgl_renderer_create(void) {")
assert i > 0
impl = [
    "/* 平铺：一条搜索结果 = 底部留白 + 一条分隔线。",
    "   留白让条目之间\"空一格\"，分隔线让\"这是一块\"在小屏上肉眼可辨。 */",
    "static void lvgl_renderer_style_result_item(Renderer *renderer, void *widget) {",
    "  (void)renderer;",
    "  if (!widget)",
    "    return;",
    "  lv_obj_t *obj = (lv_obj_t *)widget;",
    "  lv_obj_set_style_pad_bottom(obj, 12, 0);",
    "  lv_obj_set_style_border_width(obj, 1, 0);",
    "  lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_BOTTOM, 0);",
    "  lv_obj_set_style_border_color(obj, lv_color_hex(0x333333), 0);",
    "}",
    "",
]
L[i:i] = impl
i = find(L, "  renderer->base.create_chip = lvgl_renderer_create_chip;")
assert i > 0
L[i + 1:i + 1] = ["  renderer->base.style_result_item = lvgl_renderer_style_result_item;"]
save(p, L, nl)
print("lvgl_renderer.cpp OK")

# ══ 3) layout_engine.cpp：条目样式 + 非链接节点也可点 ════════════════════
p, L, nl = load("src/layout_engine.cpp")

# 3a) 结果条目样式：紧跟 create_label 之后（widget 此时已建好）
i = find(L, "        node->widget = iface->create_label(")
assert i > 0, "create_label call not found"
j = i
while L[j].rstrip()[-1] != ";":
    j += 1
block = [
    "        /* 搜索结果：条目之间空一格 + 一条分隔线，一眼分得出哪条是哪条 */",
    "        if (s_flatMode && node->type == ELEMENT_LIST_ITEM && node->widget &&",
    "            iface->style_result_item)",
    "          iface->style_result_item(render_ctx->renderer, node->widget);",
]
L[j + 1:j + 1] = block

# 3b) 普通文本也可点：去掉 "node->type == ELEMENT_LINK &&" 限制
old = ("    if (widget && node->type == ELEMENT_LINK && iface->register_link_handler) {")
i = find(L, old)
assert i > 0, "plain-label link registration not found"
L[i] = "    if (widget && iface->register_link_handler) {"
save(p, L, nl)
print("layout_engine.cpp OK")

# ══ 4) dom_renderer.cpp：给 <li> 挂上子树里第一个链接 ════════════════════
p, L, nl = load("src/dom_renderer.cpp")

i = find(L, "static bool li_is_link_wrapper(lxb_dom_node_t *node) {")
assert i > 0
helper = [
    "/**",
    " * subtree_first_href - 子树里第一个\"值得点\"的链接（返回绝对地址，需 free）。",
    " *",
    " * 为什么必须做：搜索结果 <li class=\"b_algo\"> 会被判定\"有文本\"而抽成纯文本叶子，",
    " * 里面那个 <a href> 标题链接**根本不会进布局树** —— 整条结果就点不动。",
    " * 这里把子树里第一个真链接的绝对地址记到 <li> 上，渲染时挂到整条 label。",
    " * 跳过 # / javascript: / mailto: / tel:（点它们没有意义，还会被 app 层判为不可解析）。",
    " */",
    "static char *subtree_first_href(lxb_dom_node_t *node, RenderContext *context,",
    "                                int depth) {",
    "  if (!node || depth > 8)",
    "    return NULL;",
    "",
    "  if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {",
    "    lxb_dom_element_t *element = (lxb_dom_element_t *)node;",
    "    size_t tag_len = 0;",
    "    const char *tag = html_parser.get_element_tag(element, &tag_len);",
    "    if (tag && tag_len == 1 && (tag[0] == 'a' || tag[0] == 'A')) {",
    "      size_t href_len = 0;",
    "      const char *href_attr =",
    "          html_parser.get_element_attr(element, \"href\", &href_len);",
    "      if (href_attr && href_len > 0) {",
    "        char *raw = safe_strndup(href_attr, href_len);",
    "        if (raw) {",
    "          bool bad = (raw[0] == '#') || (strncmp(raw, \"javascript:\", 11) == 0) ||",
    "                     (strncmp(raw, \"mailto:\", 7) == 0) ||",
    "                     (strncmp(raw, \"tel:\", 4) == 0);",
    "          char *abs =",
    "              bad ? NULL : tactilebrowser_resolve_url(context->document_url, raw);",
    "          free(raw);",
    "          if (abs)",
    "            return abs;",
    "        }",
    "      }",
    "    }",
    "  }",
    "",
    "  for (lxb_dom_node_t *c = html_parser.get_first_child(node); c;",
    "       c = html_parser.get_next_sibling(c)) {",
    "    char *r = subtree_first_href(c, context, depth + 1);",
    "    if (r)",
    "      return r;",
    "  }",
    "  return NULL;",
    "}",
    "",
]
L[i:i] = helper

# 挂到 li：放在 href 提取（ELEMENT_LINK 分支）之后
i = find(L, "  // Extract href for links")
assert i > 0, "href extraction anchor not found"
attach = [
    "  /* 搜索结果整条可点：<li> 被抽成文本叶子后，里面的 <a> 就不在树里了。",
    "     把子树里第一个链接的绝对地址记到 li 上，渲染时挂到整条 label 上。",
    "     只对 li 做（不做 div）：div 动辄包裹半个页面，整块可点会变成误触制造机。 */",
    "  if (elem_type == ELEMENT_LIST_ITEM && !layout_node->href_resolved &&",
    "      !li_is_link_wrapper(dom_node)) {",
    "    layout_node->href_resolved = subtree_first_href(dom_node, context, 0);",
    "  }",
    "",
]
L[i:i] = attach
save(p, L, nl)
print("dom_renderer.cpp OK")

# ══ 5) 核对 ═════════════════════════════════════════════════════════════
checks = [
    ("include/common_types.h", "style_result_item"),
    ("src/lvgl_renderer.cpp", "lvgl_renderer_style_result_item"),
    ("src/lvgl_renderer.cpp", "renderer->base.style_result_item"),
    ("src/lvgl_renderer.cpp", "LV_BORDER_SIDE_BOTTOM"),
    ("src/layout_engine.cpp", "iface->style_result_item(render_ctx->renderer"),
    ("src/layout_engine.cpp", "if (widget && iface->register_link_handler) {"),
    ("src/dom_renderer.cpp", "subtree_first_href"),
    ("src/dom_renderer.cpp", "layout_node->href_resolved = subtree_first_href"),
]
ok = True
for rel, needle in checks:
    t = open(BASE + "\\" + rel.replace("/", "\\"), encoding="utf-8", errors="replace").read()
    good = needle in t
    ok = ok and good
    print("%-28s %-52s %s" % (rel, needle[:50], "OK" if good else "*** MISSING ***"))
print("ALL OK" if ok else "SOMETHING MISSING")
sys.exit(0 if ok else 1)
