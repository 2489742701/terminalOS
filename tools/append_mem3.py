# -*- coding: utf-8 -*-
p = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\2026-09-23.md"
add = """
## 搜索结果条目：间隔 + 可点（已编译，待烧录 —— COM7 掉线）

master 要求：① 每条搜索结果之间"空一格"；② 结果能点进去。

### ① 间隔
- `g_content` 的 flex `pad_gap` 2 → 6（原来全挤在一起）。
- 新增 `RenderInterface.style_result_item` + `lvgl_renderer_style_result_item()`：
  列表项底部 `pad_bottom 12` + **一条 1px 底边框**（`LV_BORDER_SIDE_BOTTOM`，0x333333）。
  留白负责"空一格"，分隔线负责"这是一块"—— 小屏上光靠留白分不出来。
- 只在 `s_flatMode && node->type == ELEMENT_LIST_ITEM` 时应用。

### ② 结果可点 —— 关键坑：`<a>` 根本不在树里
搜索结果 `<li class="b_algo">` 有文本 → 被 `should_extract_text` 判成**纯文本叶子**，
不再递归子节点 → **里面的 `<a href>` 标题链接根本没进布局树**，所以整条结果点不动
（之前 `clickable links=12` 全是筛选栏那种小胶囊，结果本身一条都不可点）。

修法（`dom_renderer.cpp`）：新增 `subtree_first_href(dom_node, context, depth)`，
深度优先找子树里第一个"值得点"的 `<a href>`，用 `tactilebrowser_resolve_url()` 转绝对地址，
记到 `layout_node->href_resolved` 上。跳过 `#` / `javascript:` / `mailto:` / `tel:`。
- **只对 `ELEMENT_LIST_ITEM` 做，不做 div**：div 动辄包裹半个页面，整块可点 = 误触制造机。
- 跳过 `li_is_link_wrapper()`（那种 li 里 `<a>` 自己会活下来）。

`layout_engine.cpp` 普通文本分支的链接注册去掉了 `node->type == ELEMENT_LINK &&` 限制 ——
现在只要有 `flat_link_target()` 就挂点击（LI 因此可点）。

### 状态
- 编译通过：RAM 19.0% / Flash 69.5%。
- ❌ **没烧上**：`flash_run.py` 报 `could not open port 'COM7'`。
  排查：`Disable/Enable-PnpDevice` 都返回"常规故障"；`Get-PnpDevice -PresentOnly`
  里已经**查不到 VID_1A86** → 不是 CH340 软件卡死，是**物理掉线**，必须重新插 USB。
  （记忆里那条"先软件复位、真掉线才拔线"的判据这次用上了：看 `-PresentOnly`，别看 Status。）

### 工具
- `tools/fix_result_items.py`：本次 4 文件行级手术脚本（Edit 工具已失败 5 次，弃用）。
"""
open(p, "a", encoding="utf-8").write(add)
print("appended", len(add))
