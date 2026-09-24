# -*- coding: utf-8 -*-
"""临时诊断：长文本节点到底挂在哪个标签下（查完就删）"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\dom_renderer.cpp"
s = io.open(P, encoding="utf-8", newline="").read()

old = """      if (start <= end) {
        LayoutNode *text_node = layout_node_create(ELEMENT_SPAN);"""
new = """      if (start <= end) {
        /* [DIAG] 长文本：打印父标签，查清那坨 JS 到底挂在哪 */
        {
          size_t tlen = (size_t)(end - start + 1);
          if (tlen > 150) {
            const char *ptag = "?";
            size_t plen = 0;
            if (dom_node->parent &&
                dom_node->parent->type == LXB_DOM_NODE_TYPE_ELEMENT) {
              const char *t = html_parser.get_element_tag(
                  lxb_dom_interface_element(dom_node->parent), &plen);
              if (t) ptag = t;
            }
            static int s_diag = 0;
            if (s_diag < 12) {
              Serial.printf("[DiagText] parent=<%.*s> len=%u head=%.60s\\n",
                            (int)plen, ptag, (unsigned)tlen, start);
              s_diag++;
            }
          }
        }
        LayoutNode *text_node = layout_node_create(ELEMENT_SPAN);"""

if old not in s:
    raise SystemExit("MISS")
s = s.replace(old, new, 1)
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("OK diag inserted")
