# -*- coding: utf-8 -*-
"""一次性修补：让 <textarea>（必应搜索框就是它）也渲染成单行输入框。

⚠️ Edit 工具在本项目 CRLF 文件上多次"报成功不落盘"，所以用 python 直接做
   行级手术，并当场把改动后的行打出来核对。
"""
import io, sys

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\layout_engine.cpp"

raw = open(P, encoding="utf-8", newline="").read()
nl = "\r\n" if "\r\n" in raw else "\n"
lines = raw.split(nl)
print("newline:", repr(nl), "lines:", len(lines))


def find(pred, start=0):
    for i in range(start, len(lines)):
        if pred(lines[i]):
            return i
    return -1


# ── 1) 渲染分支：textarea 不再跳过 ──────────────────────────────────────
i_if = find(lambda l: "node->type == ELEMENT_INPUT_TEXT && iface->create_text_input" in l)
i_ta = find(lambda l: "} else if (node->type == ELEMENT_TEXTAREA) {" in l)
i_chip = find(lambda l: "} else if (s_flatMode && flat_wants_chip(node)" in l)
print("i_if=%d i_ta=%d i_chip=%d" % (i_if, i_ta, i_chip))
assert i_if > 0 and i_ta > i_if and i_chip > i_ta, "anchor not found"

# 0-based: 输入分支从 i_if 到 i_ta-1 之前的 "widget = node->widget;"
i_w = find(lambda l: l.strip() == "widget = node->widget;", i_if)
assert i_w > i_if and i_w < i_ta

new_block = [
    "  /* 输入框 **和 textarea** 都渲染成单行输入框。",
    "     ⚠️ 2026-09-23：搜索框并不一定是 <input> —— 必应的是",
    "        <textarea id=\"sb_form_q\" type=\"search\" rows=\"1\">，当年为了省 DRAM",
    "        把 textarea 整个跳过，结果必应里**根本看不见搜索框**。",
    "        现在 LVGL 池已在 PSRAM、DRAM 有 250KB，这个限制不成立。 */",
    "  if ((node->type == ELEMENT_INPUT_TEXT || node->type == ELEMENT_TEXTAREA) &&",
    "      iface->create_text_input && !widget_limit_reached) {",
    "    /* textarea 的 form_value 是它的**整段 innerText**（可能几十 KB），",
    "       原样塞给单行输入框会拖慢渲染，截到 256 B 足够看。 */",
    "    char *capped = NULL;",
    "    const char *shown_value = form_value;",
    "    if (shown_value && strlen(shown_value) > 256) {",
    "      capped = (char *)malloc(257);",
    "      if (capped) {",
    "        memcpy(capped, shown_value, 256);",
    "        capped[256] = '\\0';",
    "        shown_value = capped;",
    "      }",
    "    }",
    "    node->widget = iface->create_text_input(",
    "        render_ctx->renderer, shown_value, placeholder, node->box.x, node->box.y,",
    "        node->box.width, node->box.height);",
    "    free(capped);",
]

# 替换 i_if .. i_w-1（保留原来的 widget= / s_rowContainer= / bg fill 三行）
lines[i_if:i_w] = new_block

# 重新定位：删掉 } else if (ELEMENT_TEXTAREA) 那一支（含注释与空行）
i_ta = find(lambda l: "} else if (node->type == ELEMENT_TEXTAREA) {" in l)
i_chip = find(lambda l: "} else if (s_flatMode && flat_wants_chip(node)" in l)
print("after insert: i_ta=%d i_chip=%d" % (i_ta, i_chip))
assert i_ta > 0 and i_chip > i_ta
del lines[i_ta:i_chip]

# ── 2) 平铺尺寸：textarea 也给满宽单行 ──────────────────────────────────
i_size = find(lambda l: l.strip() == "if (node->type == ELEMENT_INPUT_TEXT) {")
assert i_size > 0, "flatten sizing anchor not found"
lines[i_size] = ("  if (node->type == ELEMENT_INPUT_TEXT || "
                 "node->type == ELEMENT_TEXTAREA) {")

out = nl.join(lines)
open(P, "w", encoding="utf-8", newline="").write(out)
print("written")

# ── 3) 核对 ─────────────────────────────────────────────────────────────
chk = open(P, encoding="utf-8", errors="replace").read()
for needle in ["node->type == ELEMENT_TEXTAREA) &&",
               "char *capped = NULL;",
               "if (node->type == ELEMENT_INPUT_TEXT || node->type == ELEMENT_TEXTAREA) {",
               "memcpy(capped, shown_value, 256);"]:
    print("%-70s %s" % (needle, "OK" if needle in chk else "*** MISSING ***"))
print("textarea-skip branch gone:",
      "跳过 textarea" not in chk)
