"""把「汉字渲染根因 = RLE 解压」与 atoi(nullptr) 崩溃两个结论写进项目日志。"""
import io
import os

P = os.path.join(r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040",
                 ".workbuddy", "memory", "2026-09-24.md")

NOTE = """

## 文字渲染根因（已钉死并修复）：字形位图每次都重跑一遍 RLE 解压

### ⛔ 串口命令分发处 `arg` 可能是 nullptr —— atoi(NULL) 直接 LoadProhibited
`executeLine()` 里 `char* arg = nullptr;`，命令不带参数时 arg 就是 NULL。
`cmdPerf` 当初写了 `(arg && *arg)` 保护，后加的命令照抄 `int n = atoi(arg);` 就漏了。
实测 `perflet`（不带参数）直接崩在 serial_console.cpp:554，
addr2line 显示 PC 落在 ROM(0x4002xxxx) + EXCVADDR=0 —— **看到 0x4002xxxx 的栈帧别去
查 ROM，那是 libc 拿到空指针**。已修：`perflet` / `perftxt` / `vp` / `flatdump` 全部判空。
**以后加新命令，第一件事就是判 arg。**

### perflet 实测：一个汉字的成本构成（n=100 x 20 字）
| 环节 | ns/char | 占比 |
|---|---|---|
| A 字形查找 get_glyph_dsc | 2 573 | 0.8% |
| **B 取位图 get_glyph_bitmap** | **132 007** | **44%** |
| C mem_buf get+release | 322 | 0.1% |
| D 4bpp 解包（含 B） | 162 022 | — |
| D-B 纯解包 | 30 014 | 10% |
| 未计入（blend 混合等） | ~138 000 | 45% |

**B 才是真凶**：`lv_font_get_bitmap_fmt_txt()` 在 `bitmap_format != PLAIN` 时
走的是 **RLE 解压分支** —— 每次取一个字的位图都要 `decompress()` 一遍，
而且结果只存在全局单例 `_lv_font_decompr_buf` 里，**没有任何字形缓存**。
中文点阵 RLE 几乎压不动：去掉压缩后 flash 只增 157KB（72.3% -> 77.0%），
却省掉 132us/字。纯赚。

### 修法：字体生成加 `--no-compress`
`tools/gen_fonts.py` 的 lv_font_conv 命令补了 `--no-compress`
（`--no-prefilter` 不用加，prefilter 只影响压缩格式）。
生成后 `bitmap_format` 从 1(COMPRESSED) 变 0(PLAIN)，
`lv_font_get_bitmap_fmt_txt` 直接 `return &glyph_bitmap[bitmap_index]`，零解压。
观感**完全不变**（bpp 仍是 4，抗锯齿照旧）。

⚠️ 三个坑：
1. **备份别放在 `src/app/` 下** —— PlatformIO 会把 `src/` 下所有 .c 都编进去，
   直接 `multiple definition of font_zh_16`。备份放 `tools/` 下。
2. 生成产物会把 `.fallback` 重置成 NULL。目前两个字体本来就是 NULL
   （英文/数字走 montserrat 是别处处理的），所以没损失 —— 但 MEMORY 里
   "fallback 已写进生成后处理"这条**对不上当前代码**，待核对。
3. 符号表已被更新过：16px 3785 -> 3927 字，24px 996 -> 1052 字。

### 下一步候选（按性价比）
- **bpp 4 -> 1**：解包(30us) + blend(138us) 一起省，字会变硬（失去抗锯齿）。
  观感受影响，**要 master 拍板**。
- blend 那 138us 还没拆：每个汉字是按行调 blend（16 行 = 16 次调用），
  看看有没有一次性整块混合的路径。
"""

s = io.open(P, "r", encoding="utf-8", newline="").read()
io.open(P, "w", encoding="utf-8", newline="").write(s + NOTE)
print("APPENDED", len(s), "->", len(s) + len(NOTE))
