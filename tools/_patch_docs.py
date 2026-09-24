# -*- coding: utf-8 -*-
"""更新待办清单 + 坑点速查表 B3。"""
import io

def rep(p, old, new, tag):
    t = io.open(p, encoding="utf-8", newline=None).read()
    assert t.count(old) == 1, "%s: got %d" % (tag, t.count(old))
    io.open(p, "w", encoding="utf-8", newline="").write(t.replace(old, new))
    print("patched", p)

GT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"

# ── README 待办 ────────────────────────────────────────────────────────
rep(GT + r"\README.md",
"""- [ ] SERP 广告过滤（广告/推广/sponsored）
- [ ] 页脚备案/隐私/条款等垃圾一并丢掉
- [ ] emoji 仍是豆腐块
- [ ] 触摸坐标偏移；上滑时左下角一条白线
- [ ] 蓝牙接入（现为占位）
- [ ] 缩略图 / 图片下载
- [ ] 更多 2D / 3D 小游戏
- [ ] LV_COLOR_DEPTH 16→8（性能下一步候选，待拍板）""",
"""- [x] SERP 广告过滤（广告/推广/sponsored）—— 2026-09-24，`layout_engine.cpp::flat_is_ad_text`
- [x] 页脚备案/隐私/条款等垃圾 —— 同上，`flat_is_footer_text`。两者都带**长度闸门**防误杀正文
- [x] emoji 豆腐块 —— 2026-09-24，给 vendored `lv_draw_sw_letter.c` 打补丁：
      emoji 区码位静默跳过；**中文缺字仍保留方框**（那是"字库里没有"的信号）
- [~] 触摸坐标偏移 —— 已加两点校准 + `traw`/`tcal`/`tprobe` 三个串口命令；
      默认端点仍是历史写死的 0..480，**待上板量四角后填真值**
- [~] 上滑时左下角白线 —— 高度怀疑是 LVGL 默认 `LV_SCROLLBAR_MODE_AUTO` 画的滚动条
      （`desktop_screen.cpp` 早已显式 OFF，`browser_screen.cpp` 漏了）。已补 OFF，**待上板确认**
- [ ] 蓝牙接入（现为占位）
- [ ] 缩略图 / 图片下载
- [ ] 更多 2D / 3D 小游戏
- [ ] LV_COLOR_DEPTH 16→8（性能下一步候选，待拍板）
- [ ] SD 卡路线 A（资源外置）/ B（Lua 脚本层）二选一 —— 探测数据齐了，待 master 拍板""",
"README todo")

# ── docs/00 待办 ───────────────────────────────────────────────────────
rep(GT + r"\docs\00-项目上手指南.md",
"""- [ ] SERP 广告过滤（广告/推广/sponsored）
- [ ] 备案/隐私/条款等页脚垃圾一并丢掉
- [ ] emoji 仍是豆腐块
- [ ] 触摸坐标偏移；上滑时左下角一条白线
- [ ] 蓝牙接入（现为占位）
- [ ] 新闻源：等 master 给源再填（首页已留占位框）""",
"""- [x] SERP 广告过滤（广告/推广/sponsored）
- [x] 备案/隐私/条款等页脚垃圾一并丢掉
- [x] emoji 不再是豆腐块
- [~] 触摸坐标偏移（校准框架已就位，待上板量四角）
- [~] 上滑时白线（怀疑是 LVGL 默认滚动条，已 OFF，待确认）
- [ ] 蓝牙接入（现为占位）""",
"docs00 todo")

# ── docs/09 B3 ─────────────────────────────────────────────────────────
rep(GT + r"\docs\09-坑点速查表.md",
"""### B3 · 上滑时左下角一条白线
未修。已知问题，坐标/裁剪边界问题。""",
"""### B3 · 上滑时左下角一条白线
**2026-09-24 定位 + 修（待上板确认）**：LVGL 的滚动条。
`lv_obj` 默认 `LV_SCROLLBAR_MODE_AUTO`（`lv_obj.c:348`），可滚动时沿边沿画一条
滚动条；本工程没给 `LV_PART_SCROLLBAR` 设自定义样式，用的是默认浅色 ——
在黑底 UI 上就是一条白线，且只在"内容比容器长"时出现，跟"上滑"高度相关。

- `desktop_screen.cpp` 早就 `lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF)`，
  `browser_screen.cpp` 的 `g_content` **漏了** → 已补。
- **自查方法**：任何可滚动容器都要显式设 `LV_SCROLLBAR_MODE_OFF`（或给 SCROLLBAR
  设一套暗色样式）。`grep -rn "scroll_dir" src/` 找出所有可滚动对象逐个核对。""",
"docs09 B3")

# ── docs/09 新增 B5：触摸偏移 ──────────────────────────────────────────
t = io.open(GT + r"\docs\09-坑点速查表.md", encoding="utf-8", newline=None).read()
old = "### B4 · IDF 4.4 升不了级"
new = """### B3b · 触摸坐标偏移（2026-09-24 定位）
根因有两个，都在 `src/hal/touch.cpp` 的映射表达式里：

1. **翻转了两次**：TAMC_GT911 库 `setRotation(ROTATION_NORMAL)` 已经做过
   `x = width - x`，旧代码又 `map(p.x, 480, 0, 0, 479)` 翻一次 —— 两者对消，
   实际等价于"裸值 ×479/480"。看代码像是翻转，其实没有。
2. **端点写死 480**：若 GT911 配置寄存器里的 `X/Y_OUTPUT_MAX` 不是 480，
   裸值根本不落在 0..480，于是整屏偏移/缩放都不对。

**已改**：两点线性映射 + 三个串口命令 ——
`tprobe`（读 GT911 的 X/Y_OUTPUT_MAX，不是 480 就是根因 2）、
`traw [s]`（10s 内连续打裸坐标，逐个点四角）、
`tcal x0 x1 y0 y1`（填实测端点，立即生效，不用重烧）。
默认值 0/480/0/480 只为了复刻旧行为，**不是真值**。

### B4 · IDF 4.4 升不了级"""
assert t.count(old) == 1
io.open(GT + r"\docs\09-坑点速查表.md", "w", encoding="utf-8", newline="").write(t.replace(old, new))
print("patched docs09 B3b")
