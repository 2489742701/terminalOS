#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""把今天两个崩溃根因写进 docs/09-坑点速查表.md"""
import io
import os

P = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\docs\09-坑点速查表.md'

raw = io.open(P, encoding='utf-8', errors='replace').read()
NL = '\r\n' if '\r\n' in raw else '\n'
S = raw.replace('\r\n', '\n')

# ── C8：loopTask 栈溢出（插到 D 段之前）───────────────────────────────────
ANCHOR_D = '\n## D. 内存\n'
C8 = """
### C8 · ⚠️ `A stack overflow in task loopTask has been detected`（2026-09-23 实测）

**症状**（极易误判成"内存不够"）：
- 打开某个网页时整机重启
- **画面还停着，但触摸完全失效** —— RGB 并行屏由 GDMA 自行刷新，
  主任务已经死了，DMA 还在刷最后一帧
- 串口在重启前会明确写：
  `***ERROR*** A stack overflow in task loopTask has been detected.`
- `Backtrace` 结尾常带 `|<-CORRUPTED`（栈被打烂，回溯不可信）
- **跟内存无关** —— 实测出事时 DRAM 还剩 203KB

**根因：是栈不是堆。**
Arduino-ESP32 的 `loopTask` 栈默认只有 **8192 B**。而 layout_engine 里有 4 个
全树遍历函数把 `next_sibling` 也写成了递归：

```c
layout_tree_stats        / layout_flatten_tree
layout_scale_tree        / layout_clamp_horizontal
    ...
    f(node->first_child, ...);
    f(node->next_sibling, ...);   /* ← 凶手 */
```

于是递归深度 = **树的节点总数**（几百到上千），而不是嵌套层数。
必应搜索页 356 个节点侥幸过关，乐鑫官网 977 个节点直接炸。

**怎么判别**：报错里写的是 `task loopTask` = UI 主任务；
若后台任务崩，写的是 `task browser_fetch`（那是另一回事，见 D4）。

**修复（已在 2026-09-23 做完）**：
- 上面 4 个函数 sibling 改迭代：`while (node) { ...; node = node->next_sibling; }`
- `platformio.ini` 加 `-DARDUINO_LOOP_STACK_SIZE=32768`（8KB → 32KB）
- 剩下纯父子递归的 `layout_render_node` / `layout_drop_junk` 加 `MAX_LAYOUT_DEPTH 64` 上限
- ⛔ **`layout_node_destroy` 绝不能加深** —— 中途截断 = 内存泄漏
- 加了永续预警：每次渲染完串口打
  `[Browser] loopTask stack: xxxx B left (peak used xxxx B)`
  低于 ~8KB 就该回头查递归是不是又失控了

**修复后的实测对照**：同一条三级跳转路径，977 节点的页面从「必崩」变成
正常渲染，栈峰值 11108 B、剩余 21660 B。

---
"""

assert ANCHOR_D in S, 'D anchor missing'
if '### C8 ·' not in S:
    S = S.replace(ANCHOR_D, C8 + ANCHOR_D.lstrip('\n'), 1)
    print('  C8 插入 OK')
else:
    print('  C8 已存在')

# ── F10：伪链接 ───────────────────────────────────────────────────────────
ANCHOR_G = '\n## G. 字体（改前必读 `09`）\n'
F10 = """
### F10 · ⚠️ 伪链接被当成真网页下载（`javascript:` 等）

**症状**：点网页上的空按钮 / 菜单项后，进度条走到 60~70% 停下或直接重启；
日志里出现 `read 500000+ bytes`、节点数暴涨到几千、DRAM 一次掉上百 KB。

**根因**：现代网站大量 href 根本不是真地址 ——
`javascript: void(0);`（纯 JS 空按钮，乐鑫官网菜单里一大堆）、
`mailto:` `tel:` `#anchor`。这些一旦走下载流程，服务器往往返回一个大号 404 页。

更难的是它们**常被拼上域名前缀**，变成：
```
https://www.espressif.com.cn/zh-hans/products/socs//javascript: void(0);
```
只看开头是不是 `javascript:` 拦不住，必须查**子串**。

**实测代价**（2026-09-23）：一次这种点击下载了 507440 B、解析出 4780 个节点、
DRAM 从 207KB 掉到 72KB。连点几下就该重启了。

**动作**：`isPseudoUrl()` 在两个入口拦 —— `link_click_cb` 和 `startFetch`。
判定 = 前缀命中伪协议列表（javascript:/mailto:/tel:/data:/about:/blob:/sms:/intent:/…）
**或** 子串含 `javascript:` / `void(0)` **或** 以 `#` 开头。

---
"""

assert ANCHOR_G in S, 'G anchor missing'
if '### F10 ·' not in S:
    S = S.replace(ANCHOR_G, F10 + ANCHOR_G.lstrip('\n'), 1)
    print('  F10 插入 OK')
else:
    print('  F10 已存在')

# ── 一句话红线补两条 ─────────────────────────────────────────────────────
OLD_R = '## 附：一句话红线'
NEW_R = ('## 附：一句话红线')
if '见 stack overflow in task loopTask → 是栈不是堆' not in S:
    # 找到红线区块，追加两条
    i = S.find(OLD_R)
    if i >= 0:
        # 在该章节第一段之后插入
        j = S.find('\n', i + len(OLD_R))
        add = ('\n- **见 `stack overflow in task loopTask` → 是栈不是堆**，去加 '
               '`ARDUINO_LOOP_STACK_SIZE`，别去加 PSRAM\n'
               '- **遍历树：兄弟用迭代，父子才递归**（写反 = 递归深度等于节点总数）\n'
               '- **`layout_node_destroy` 不许加深**：截断 = 泄漏\n'
               '- **链接不一定是 URL**：点之前先过 `isPseudoUrl()`\n')
        S = S[:j + 1] + add + S[j + 1:]
        print('  红线补充 OK')

io.open(P, 'w', encoding='utf-8', newline='').write(S.replace('\n', NL))
print('written, size =', os.path.getsize(P))
