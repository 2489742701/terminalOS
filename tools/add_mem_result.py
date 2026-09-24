"""写入去压缩后的实测收益与下一步路线判断。"""
import io
import os

P = os.path.join(r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040",
                 ".workbuddy", "memory", "2026-09-24.md")

NOTE = """

### 去压缩后实测收益（已烧录验证，11 屏无崩溃）

perflet 单项：B 取位图 **132007 -> 2164 ns/char（快 61 倍）**，假说完全坐实。
perftxt 整字：**303 -> 134 us/char**。

| 屏 | 改前 fps | 改后 fps | 整帧 us |
|---|---|---|---|
| 时钟 | 27.3 | **28.8** | 34724 |
| 游戏栏目 | 25.8 | **28.2** | 35455 |
| 天气 | 25.6 | **27.8** | 35987 |
| 画板 | 24.6 | **25.3** | 39577 |
| 系统信息 | 19.7 | **24.9** | 40156 |
| 桌面(磁贴) | 17.1 | **19.9** | 50312 |
| 设置 | 13.2 | **16.4** | 61147 |
| 桌面图标 | — | 15.4 | 64752 |
| 无线网络 | 13.2 | **15.2** | 65707 |
| 浏览器 | 11.5 | **12.9** | 77478 |
| 记忆卡牌 | 12.2 | **12.8** | 78357 |

桌面 draw/flush 拆分：draw 38640 -> **30507**，flush 19738（没变，本来就是 PSRAM 拷贝）。

### 下一步的性价比判断（关键）
现在桌面整帧 50.3ms = draw 30.5 + flush 19.8。
- **flush 19.8ms 是纯浪费**（PSRAM->PSRAM 拷贝）。direct mode 能整个砍掉它，
  但要求 draw < 26ms（屏扫一帧的时间）才不撕裂。**现在差 4.5ms。**
- 剩下的 draw 30.5ms 里：解包 30us/字 + blend ~138us/字。
  **bpp 4 -> 1 能把这两块一起干掉**（1bpp 不需要解包，mask 只有 0/255 时
  blend 退化成纯拷贝），draw 有望降到 ~15ms。
- 若 bpp=1 后 draw<26ms → **开 direct mode** → 整帧 ≈ 15ms ≈ **66 fps**。
  这是目前看得到的最优解，但代价是**字形失去抗锯齿、边缘变硬**。
  ⛔ 观感会变，要 master 拍板才能做。
- 另外 EN(montserrat16) 也是 134us/字，说明**内置英文字体同样是压缩格式**。
  要动它得改 vendored LVGL 里那几个 lv_font_montserrat_*.c（也是 --no-compress
  重新生成），影响面比中文字体大，先不动。

### 缓存机制观察
`get_glyph_bitmap` 没有字形缓存，解压结果只放全局单例 `_lv_font_decompr_buf`。
`font_dsc.cache`（`lv_font_fmt_txt_glyph_cache_t`）在 LVGL 8.3 里**只对
get_glyph_dsc 生效**（缓存上一次查找的 gid），不缓存位图。
所以"同一帧里重复出现的字"也得各解一遍 —— 去压缩后这项已经不花钱了。
"""

s = io.open(P, "r", encoding="utf-8", newline="").read()
io.open(P, "w", encoding="utf-8", newline="").write(s + NOTE)
print("APPENDED", len(s), "->", len(s) + len(NOTE))
