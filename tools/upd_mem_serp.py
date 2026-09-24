"""更新项目长期记忆：搜索结果按块渲染 + 高度诊断坑（CRLF 安全，按行插入）。"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\MEMORY.md"

ADD = """- **搜索结果按块渲染**（`dom_renderer.cpp::li_has_block_children()`）：
  `<li>` 有 **>=2 个块级元素子节点**（跳过 link/script/style/meta/br/hr）就**不抽文本**，
  递归让各块各自成行。否则 `get_element_text()` 把整棵子树 innerText 无分隔符拼成一坨，
  且标题里的 `<a>` 会被丢掉。必应 `b_algo` = 来源行 / `<a><h2>`标题 / `<p>`摘要 三块；
  跳 `<link rel=stylesheet>` 是必须的（b_algo 里塞了一长串）。
  副作用正是想要的：标题 `<a>` 活下来 → 可点胶囊 → **只有标题可点**，不误触。

## 排障手法"""

OLD_HEAD = "## 排障手法"

t = io.open(P, encoding="utf-8", newline="").read()
out = []
if "li_has_block_children" in t:
    out.append("ALREADY")
else:
    crlf = "\r\n" in t
    lines = t.replace("\r\n", "\n").split("\n")
    idx = [i for i, l in enumerate(lines) if l.strip() == OLD_HEAD]
    if not idx:
        out.append("*** HEAD NOT FOUND ***")
    else:
        i = idx[0]
        lines[i:i] = ADD.rstrip("\n").split("\n")
        t = "\n".join(lines)
        if crlf:
            t = t.replace("\n", "\r\n")
        io.open(P, "w", encoding="utf-8", newline="").write(t)
        out.append("inserted")

# 再补一条 LVGL 测量坑
GOTCHA_ANCHOR = "- **bootloop 先看 `Saved PC`**"
GOTCHA = ("- ⚠️ **读 LVGL 尺寸前必须 `lv_obj_update_layout()`**：刚创建完 widget 时坐标还是陈旧的，\n"
          "  直接 `lv_obj_get_scroll_bottom()` 恒为负（曾一直 total=80），看着像\"内容被压扁\"。")
if "lv_obj_update_layout" in io.open(P, encoding="utf-8").read():
    out.append("gotcha ALREADY")
else:
    t = io.open(P, encoding="utf-8", newline="").read()
    crlf = "\r\n" in t
    lines = t.replace("\r\n", "\n").split("\n")
    idx = [i for i, l in enumerate(lines) if GOTCHA_ANCHOR in l]
    if idx:
        i = idx[0]
        lines[i:i] = GOTCHA.split("\n")
        t = "\n".join(lines)
        if crlf:
            t = t.replace("\n", "\r\n")
        io.open(P, "w", encoding="utf-8", newline="").write(t)
        out.append("gotcha inserted")
    else:
        out.append("*** gotcha anchor not found ***")

v = io.open(P, encoding="utf-8").read()
out.append("len=%d" % len(v))
out.append("has serp: %s" % ("li_has_block_children" in v))
out.append("has gotcha: %s" % ("lv_obj_update_layout" in v))
print("\n".join(out))
