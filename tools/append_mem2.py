# -*- coding: utf-8 -*-
p = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\2026-09-23.md"
add = """
## 必应搜索框看不见（2026-09-23 晚）—— 根因：它是 textarea 不是 input

现象：搜完落到 cn.bing.com/search?q=esp32，页面顶部**没有搜索框**，只有筛选胶囊和结果。

排查：
1. 串口里搜 `[Browser] input:` —— 一行都没有 → 引擎**压根没建输入框**，不是被 flex 挤走。
2. PC 上抓同一 URL（KitKat UA，60KB）看 input 标签：只有 3 个，非 hidden 的只有
   type=submit（「搜索」按钮）。真正的搜索框是
   `<textarea id="sb_form_q" name="q" type="search" rows="1">esp32</textarea>`。

根因：layout_render_node() 里有一支
    } else if (node->type == ELEMENT_TEXTAREA) {
      /* 跳过 textarea：lv_textarea_create 创建大量 LVGL 对象导致 DRAM 不足崩溃。 */
    }
当年 DRAM 只有 45KB 时的规避；现在 LVGL 池已在 PSRAM、DRAM 250KB，限制不成立，
于是必应的搜索框被静默吞掉。

修复（layout_engine.cpp）：
- input **和 textarea 合并成一支**，都走 iface->create_text_input()（单行）。
- layout_flatten_tree() 尺寸规则同样覆盖 textarea：满宽 maxW-24、高 40。
- ⚠️ textarea 的 form_value = **整段 innerText**（几十 KB 也可能）→ 渲染前截到 256 B。
- 顺带修掉一处 Edit 静默失败：胶囊 chip 分支还在用旧的 href_path 优先逻辑，
  已统一走 flat_link_target()（优先 href_resolved 绝对 URL）。

验证：`[Browser] input: ph="" val="esp32" 440x40 @(0,530)`，widget 序号 1（排在结果前），
内容可滚高度多出 64px（说明它真的进了 flex 流，不是被 set_pos 丢到屏外）。
widgets created: 24 (limit 200), clickable links=12。

⚠️ 遗留：这个输入框能点能打字，但**提交没有实现**（表单提交本来就在待办里）——
敲完回车不会搜索。要真能用得接 form action + input name → GET/POST。

### 工具
- tools/fix_textarea.py / tools/fix_link_target.py：行级 python 手术脚本
  （Edit 工具在本项目上第 5 次静默失败，改用 python 直接改并当场核对）。
"""
open(p, "a", encoding="utf-8").write(add)
print("appended", len(add))
