# -*- coding: utf-8 -*-
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\docs\09-坑点速查表.md"
s = io.open(P, encoding="utf-8", newline="").read()

old = "### L4 · SD 只在\"已挂载\"时参与"
new = """### L4 · ⛔⛔ String 读文件遇 0x00 会截断（本次最贵的坑）
`SDCard::readFile` 以前写的是 `out += buf`（`buf` 是 `char[256]`）。
`String::operator+=(const char*)` 走 **strlen** —— 文件里每出现一个 `0x00`，
它所在的那一块剩下的字节**全部丢掉**。
实测：131072 B 的测试文件只读回 71892 B，内容从头就错位。

网页 HTML 的内联 JS 里就带 `0x00`，于是缓存读回来是**错位的字节流**，
lexbor 把 JS 当成标签解析 —— 表面上看到的症状是：
- 页面上冒出一坨脚本源码（`[Flat] 8 ];window.onunhandledrejection=...`）；
- 出现假标签（`<bodyzoomparams.browserwidth&&_w.innerheight<...>`）；
- 瓦片数从 62 掉到 32，整页内容缩水。

**看着像"垃圾过滤没做"，其实是 SD 读坏了。** 加 junk 过滤只会把真问题盖住。
→ 用 `out.concat((const char*)buf, n)` 按长度拷，不依赖 NUL 结尾；
  顺便 `reserve(f.size()+1)` 免得每块都重新分配。
→ 排查这类问题用 `SDCard::selfTest()`（串口 `sdtest [kb]`）：
  写一段已知字节模式再读回逐字节比对，一次分清是 SD 层还是 HTML 层。

### L5 · SD 只在"已挂载"时参与"""

if old not in s:
    raise SystemExit("MISS")
s = s.replace(old, new, 1)
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("OK docs/09 L4")
