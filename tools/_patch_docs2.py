# -*- coding: utf-8 -*-
"""文档：白线已确认修好 + 新增 2048 游戏。"""
import io
GT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"


def rep(rel, old, new):
    p = GT + "\\" + rel
    t = io.open(p, encoding="utf-8", newline=None).read()
    assert t.count(old) == 1, "%s: count=%d" % (rel, t.count(old))
    io.open(p, "w", encoding="utf-8", newline="").write(t.replace(old, new))
    print("patched", rel)


rep("README.md",
"- [~] 上滑时左下角白线 —— 高度怀疑是 LVGL 默认 `LV_SCROLLBAR_MODE_AUTO` 画的滚动条\n"
"      （`desktop_screen.cpp` 早已显式 OFF，`browser_screen.cpp` 漏了）。已补 OFF，**待上板确认**",
"- [x] 上滑时左下角白线 —— **已确认修好**（master 上板验证：白线没了）。\n"
"      根因就是 LVGL 默认 `LV_SCROLLBAR_MODE_AUTO` 画的滚动条：\n"
"      `desktop_screen.cpp` 早已显式 OFF，`browser_screen.cpp` 的 `g_content` 漏了。\n"
"      📌 任何可滚动容器都要显式设 `LV_SCROLLBAR_MODE_OFF`，暗色 UI 上默认样式是浅色条。")

rep("README.md",
"- [ ] 更多 2D / 3D 小游戏",
"- [~] 更多 2D / 3D 小游戏 —— **已加 2048**（`src/app/game2048_screen.cpp`，游戏栏目第三个）。\n"
"      纯回合制无 tick；合并逻辑有离线单测 `tools/test_2048_logic.py`，改规则前先跑它。\n"
"      继续加游戏只需三步：写 `XxxScreen_create()` → `nav.cpp` 注册表 → `app_registry.cpp` 加条目。")

rep("README.md",
"- [x] emoji 豆腐块 —— 2026-09-24，给 vendored `lv_draw_sw_letter.c` 打补丁：",
"- [x] emoji 豆腐块 —— 2026-09-24，给 vendored `lv_draw_sw_letter.c` 打补丁：")

rep(r"docs\00-项目上手指南.md",
"- [~] 上滑时白线（怀疑是 LVGL 默认滚动条，已 OFF，待确认）",
"- [x] 上滑时白线（LVGL 默认滚动条，已 OFF，master 上板确认已消失）\n"
"- [x] 新增 2048 小游戏（游戏栏目第三个，纯回合制无 tick）")

rep(r"docs\09-坑点速查表.md",
"### B3 · 上滑时左下角一条白线\n**2026-09-24 定位 + 修（待上板确认）**：LVGL 的滚动条。",
"### B3 · 上滑时左下角一条白线\n**2026-09-24 定位 + 修 + 上板确认已消失**：LVGL 的滚动条。")
print("ALL OK")
