# -*- coding: utf-8 -*-
"""docs/09 追加：分段渲染 + SD 缓存 的坑（2026-09-25）"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\docs\09-坑点速查表.md"
s = io.open(P, encoding="utf-8", newline="").read()

anchor = "---\n\n## 附：一句话红线"
add = """---

## K · 分段渲染（2026-09-25）

### K1 · MAX_WIDGETS 不是内存保护，是"直接砍内容"
它是**硬闸门**：第 201 个 widget 之后的内容全部丢弃。长页面（必应/360 中文搜索）
后半截凭空消失，看着像"加载失败"。改成**分段渲染**后不再是问题：
排版/摊平/去垃圾只做一次（`s_preparedRoot` 按 root 指针记），
先干跑数出瓦片总数，再只铺 `[segStart, segStart+segCount)`。
翻段 = 拿同一棵布局树重铺，实测 19~73 ms，不联网、不重新解析。
⚠️ 布局树渲染完**不再释放**，只有 `startFetch()` / 退出浏览器（freeLayoutTree）才释放。

### K2 · if / else-if 链会把同一个节点数两遍
`seg_take_tile()` 放在**分支条件**里：胶囊分支判定为 false 时会**掉到下一个
else-if** 再判一次 → 同一个节点领了两块瓦片。
实测干跑把每个胶囊都数了两遍（154 vs 真实 138），段数全错。
→ `seg_take_tile(node)` 按**节点**记结果，同一节点重复调用直接返回上次的值。

### K3 · 翻段按钮在 g_content 里 → 不能在自己的回调里清内容
和链接跳转同一个坑：回调里 `contentReset()` 会把正在派发事件的按钮删掉。
→ 回调只置 `g_segPending`，真正翻段在 `BrowserScreen_tick()` 里做。

### K4 · 摊平/缩放只能做一次
`layout_flatten_tree` / `layout_scale_tree` / `layout_drop_junk` 都会**改写树**，
翻段时重复跑会串味。用 `s_preparedRoot` 挡住。
⚠️ 树被释放后必须 `layout_forget_prepare()`，否则新树可能复用同一块地址，
被误判成"已准备"而跳过摊平。

### K5 · 匿名 namespace 里别重复前向声明
`static void foo();` + 后面的 `static void foo() {...}` 会被当成**两个重载**，
调用处报 `call of overloaded 'foo()' is ambiguous`。要前向声明就只留一份。

---

## L · SD 卡页面缓存（2026-09-25）

### L1 · ⛔ SD.mkdir 不递归
`SDCard::writeFile` 只 mkdir **直接父目录**。写 `/gt/pages/xxx.html` 时父目录
`/gt/pages` 建不出来（`/gt` 都不存在）→ `SD.open` 返回空 → 静默写失败。
→ 网页缓存平铺在 `/gt/p%08x.html`。

### L2 · ⛔ 缓存时间戳只记 millis() → 重启后全部判成过期
`millis()` 重启归零，`millis() - ts` 是巨大的无符号数，每一份缓存都被判过期
（实测：刚写进卡、重启再打开，照样走联网）。
→ 头部同时记 `EP:<time(nullptr)>`，有校时就按真实时间算 TTL；
没校时才退回 millis 那条路（`ts > millis()` = 上次开机写的，直接认）。

### L3 · String 拼二进制 HTML 要用 concat(ptr, len)
`String((const char*)data)` 遇到 NUL 会截断；`out.concat((const char*)d, len)` 才安全。

### L4 · SD 只在"已挂载"时参与
挂载要占用与 LCD 共用的那条 SPI，**绝不在浏览过程中偷偷 mount**（串口 `sd` 才挂）。
没挂载一律静默跳过，退回 PSRAM 2 槽缓存 / LittleFS 下载。

---

## 附：一句话红线"""

if anchor not in s:
    raise SystemExit("MISS anchor")
s = s.replace(anchor, add, 1)
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("OK docs/09")
