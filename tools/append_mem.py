import io

p = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\2026-09-23.md'
txt = """

---

## 平铺模式收尾（v4 定稿）+ WiFi 图标崩溃修复

### WiFi 图标放大到 22 后开机必崩 LoadProhibited

栈（addr2line 还原）：

    lv_tlsf_free(lv_tlsf.c:1166) <- lv_mem_free <- lv_draw_mask_free_param(lv_draw_mask.c:217)
    <- lv_draw_sw_arc.c:168 <- lv_canvas_draw_arc <- drawWifi(icons.cpp:52)
    <- icon_wifi_set_level <- StatusBar_create <- App::begin

根因：**随包 LVGL 的 bug**。`lv_draw_sw_arc.c` 画**部分圆弧**的分支无条件调用
`lv_draw_mask_free_param(&mask_in_param)`，而该分支里 `mask_in_param` **从未初始化**
（上面 full-ring 分支 line 121 是有 `if(mask_in_id != LV_MASK_ID_INV)` 保护的）。
栈垃圾被当成 mask type 去 free -> 崩。放大图标只是让它每次都走到这条路径。

双保险修复：

1. `Libraries/Lvgl/src/draw/sw/lv_draw_sw_arc.c` 补 `if(mask_in_id != LV_MASK_ID_INV)`；
2. `src/app/icons.cpp` 的 `drawWifi()` 改用 `arc_polyline()`（14 段折线 + `lv_canvas_draw_line`），
   彻底不碰 `lv_draw_arc`。

> 改 vendored 库要改 `Libraries/Lvgl/`，不是 `.pio/libdeps`（后者会被 PIO 覆盖回去）。

### 平铺"行合并"规则踩了三轮才定

目标：导航条「我的关注/我的收藏/皮肤中心/用户反馈」别拆成四行。

| 版本 | 做法 | 结果 |
|---|---|---|
| v1 | 只看同层兄弟合并 | 4 行（坏） |
| v2 | 无文字壳子全透明，钻进去不限种类 | 1 行 OK，但「设置 ©2026…」「百度一下 正在刷新」串味，14 行并到 7 行 |
| v3 | 只剥"独子空壳" | 又退回 4 行（`<li>` 里还有图标 span，不是独子） |
| **v4** | **以"直接父节点是否相同"判跨壳；跨壳只放行链接(kind=1)，普通文本不放行** | 1 行 OK，且各区块不串味 OK |

关键认知：`<ul><li><a>x</a></li>…</ul>` 里每个 `<a>` 的爹是各自的 `<li>`，
**只看同层兄弟永远并不起来**。`FlatRow` 结构体加了 `ownerParent` 字段。
另：`<li><a>` 要在 `dom_renderer` 用 `li_is_link_wrapper()` 保留内层 `<a>`
（否则整块被抽成纯文本，连链接都不是）。

实测 m.baidu.com：92 节点 -> 10 widget；www.baidu.com：208 节点 -> 36 widget（232ms）。
数字 / `©` / `〔2026〕` 都正常（字体 ASCII 已补）。遗留：emoji（U+1F448 区）仍是豆腐块。

### 顺带修掉的

- `font_zh_16` 缺全部 95 个 ASCII（当年 `fix_font_symbols.py` 只扫 CJK）
  -> `tools/fix_font_ascii.py` 补，重生成 3927 / 1052 字。
- HTTP 不跟 3xx：`www.baidu.com` 返回 302 跳 https，旧代码 `status!=200 -> 网络错误`。
  现在解析 `Location:` 递归重发（上限 5 跳，支持相对路径）。

### 工具

- `tools/check_mtime.py`（比 .o 与 .cpp 的 mtime，防"静默跳过编译"）—— 已固化，每编必跑。
- `tools/probe_flat.py COM7 <url> <秒>`：nav browser + browser url + 抓串口。
- 完整方案与路线对比见 `docs/12-浏览器排版路线与平铺方案.md`。
"""
with io.open(p, 'a', encoding='utf-8') as f:
    f.write(txt)
print('appended', len(txt))
