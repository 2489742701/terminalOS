"""三点(⋯)恢复成"更多"：展开底部 搜索框(URL) + 刷新 + 退出浏览器。

上一版我把三点改成了"回搜索首页"，master 纠正：
  · 三点 = 更多 → 打开底部的搜索框、刷新、退出浏览器
  · 首页键（房子）= 去我们自己做的小引擎首页（这个原本就对）
所以恢复分组：
  组 A（三点展开）：URL 搜索框 + 刷新 + 退出浏览器
  组 B（默认）：后退 / 前进 / 首页 / 刷新 / 下载
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"

t = io.open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in t
lines = t.replace("\r\n", "\n").split("\n")
log = []


def idx_of(sub, start=0):
    for i in range(start, len(lines)):
        if sub in lines[i]:
            return i
    return -1


# ── 1. 恢复 g_navModeB 声明 + 组 A 容器 ──
i = idx_of("static lv_obj_t* g_exitTopBtn = nullptr;")
if i >= 0:
    lines[i] = "static lv_obj_t* g_exitTopBtn = nullptr; /* 组 A 的「退出浏览器」 */"
    # 在它前面插入 g_navModeB 与 g_urlBar
    lines[i:i] = [
        "static bool g_navModeB = true;           /* true = 显示组 B（导航键），false = 组 A（搜索框+刷新+退出） */",
        "static lv_obj_t* g_urlBar = nullptr;     /* 组 A 容器：URL 搜索框 + 刷新 + 退出浏览器 */",
    ]
    log.append("decl: ok")
else:
    log.append("decl: *** missing ***")

# ── 2. applyBarMode 恢复显隐 ──
i0 = idx_of("static void applyBarMode() {")
i1 = idx_of("/* 三点键 = 回到我们自己的搜索首页")
if i0 >= 0 and i1 > i0:
    NEW = [
        "static void applyBarMode() {",
        "  /* true = 组 B（后退/前进/首页/刷新/下载）；false = 组 A（搜索框/刷新/退出浏览器） */",
        "  const bool b = g_navModeB;",
        "  if (g_urlBar)  b ? lv_obj_add_flag(g_urlBar, LV_OBJ_FLAG_HIDDEN)",
        "                  : lv_obj_clear_flag(g_urlBar, LV_OBJ_FLAG_HIDDEN);",
        "  if (g_navBar)  b ? lv_obj_clear_flag(g_navBar, LV_OBJ_FLAG_HIDDEN)",
        "                  : lv_obj_add_flag(g_navBar, LV_OBJ_FLAG_HIDDEN);",
        "  /* 三点 = 还有一组（组 A）；左箭头 = 回组 B。就地重画同一块画布，不再 malloc。 */",
        "  if (g_switchIcon) icon_set_type(g_switchIcon, b ? Icon::More : Icon::Back, kBrowserIconSize);",
        "  if (b) updateNavButtons();",
        "}",
        "",
    ]
    lines[i0:i1] = NEW
    log.append("applyBarMode: ok")
else:
    log.append("applyBarMode: *** missing ***")

# ── 3. switch_bar_cb 恢复为切换 ──
i0 = idx_of("/* 三点键 = 回到我们自己的搜索首页")
i1 = idx_of("lv_obj_t* BrowserScreen_create() {")
if i0 >= 0 and i1 > i0:
    NEW = [
        "/* 三点（⋯）= 更多：展开组 A —— 底部搜索框(URL) + 刷新 + 退出浏览器。",
        "   再点一次（图标变左箭头）回到组 B 的导航键。",
        "   ⚠️ 它**不是**回首页：回我们自己做的小引擎首页是组 B 里的房子键。 */",
        "static void switch_bar_cb(lv_event_t* e) {",
        "  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;",
        "  g_navModeB = !g_navModeB;",
        "  applyBarMode();",
        "}",
        "",
    ]
    lines[i0:i1] = NEW
    log.append("switch_cb: ok")
else:
    log.append("switch_cb: *** missing ***")

# ── 4. 组 A：URL 搜索框 + 刷新 + 退出浏览器 ──
i0 = idx_of("  /* 原顶栏控件（退出 / 网址 / 加载）已下移")
i1 = idx_of("  /* 状态文字：原占 406 那一格")
if i0 >= 0 and i1 > i0:
    NEW = r'''  /* ── 组 A：URL 搜索框 + 刷新 + 退出浏览器（由三点键展开，默认隐藏）──
     与组 B（后退/前进/首页/刷新/下载）互斥，两组共用底部 444 这一行。 */
  g_urlBar = lv_obj_create(scr);
  lv_obj_set_size(g_urlBar, 396, 36);
  lv_obj_align(g_urlBar, LV_ALIGN_TOP_LEFT, 2, 444);
  lv_obj_set_style_bg_opa(g_urlBar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_urlBar, 0, 0);
  lv_obj_set_style_pad_all(g_urlBar, 0, 0);
  lv_obj_clear_flag(g_urlBar, LV_OBJ_FLAG_SCROLLABLE);

  /* URL 栏用 label 而非 textarea：
     textarea 是 LVGL 里最贵的对象之一（内含 label + 光标 + 游标层），而它在本项目
     根本无法输入（键盘依赖串口）。换成 label 直接省下一块 DRAM。
     当前 URL 由 g_currentUrl 维护（label 只负责显示）。 */
  g_urlArea = lv_label_create(g_urlBar);
  lv_obj_set_size(g_urlArea, 286, 36);
  lv_obj_align(g_urlArea, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_label_set_long_mode(g_urlArea, LV_LABEL_LONG_DOT);  // 过长截断，不撑破布局
  setUrlText("搜索首页");
  lv_obj_add_flag(g_urlArea, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_text_font(g_urlArea, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(g_urlArea, lv_color_white(), 0);
  lv_obj_set_style_border_color(g_urlArea, lv_color_hex(0x444444), 0);
  lv_obj_set_style_border_width(g_urlArea, 1, 0);
  lv_obj_set_style_radius(g_urlArea, 6, 0);
  lv_obj_set_style_bg_color(g_urlArea, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_opa(g_urlArea, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_left(g_urlArea, 6, 0);
  lv_obj_add_event_cb(g_urlArea, url_focus_cb, LV_EVENT_CLICKED, NULL);

  /* 组 A 的「刷新」：与组 B 第 4 格是两颗独立按钮，回调都走 go_cb（读 g_currentUrl） */
  g_goBtn = lv_btn_create(g_urlBar);
  lv_obj_set_size(g_goBtn, 52, 36);
  lv_obj_align(g_goBtn, LV_ALIGN_TOP_LEFT, 292, 0);
  lv_obj_set_style_bg_color(g_goBtn, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_bg_color(g_goBtn, lv_color_hex(0x333333), LV_STATE_PRESSED);
  lv_obj_set_style_radius(g_goBtn, 6, 0);
  lv_obj_set_style_border_width(g_goBtn, 1, 0);
  lv_obj_set_style_border_color(g_goBtn, lv_color_hex(0x444444), 0);
  lv_obj_add_event_cb(g_goBtn, go_cb, LV_EVENT_CLICKED, NULL);
  { lv_obj_t* gi = icon_create(g_goBtn, Icon::Refresh, 26); lv_obj_center(gi); }

  /* 组 A 的「退出浏览器」：门+箭头，回启动器（不是回搜索首页） */
  g_exitTopBtn = lv_btn_create(g_urlBar);
  lv_obj_set_size(g_exitTopBtn, 52, 36);
  lv_obj_align(g_exitTopBtn, LV_ALIGN_TOP_LEFT, 348, 0);
  lv_obj_set_style_bg_color(g_exitTopBtn, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_bg_color(g_exitTopBtn, lv_color_hex(0x333333), LV_STATE_PRESSED);
  lv_obj_set_style_radius(g_exitTopBtn, 6, 0);
  lv_obj_set_style_border_width(g_exitTopBtn, 1, 0);
  lv_obj_set_style_border_color(g_exitTopBtn, lv_color_hex(0x444444), 0);
  lv_obj_add_event_cb(g_exitTopBtn, exit_event_cb, LV_EVENT_CLICKED, NULL);
  { lv_obj_t* ei = icon_create(g_exitTopBtn, Icon::ExitDoor, 26); lv_obj_center(ei); }

'''.split("\n")
    lines[i0:i1] = NEW
    log.append("groupA: ok")
else:
    log.append("groupA: *** missing ***")

out = "\n".join(lines)
if crlf:
    out = out.replace("\n", "\r\n")
io.open(P, "w", encoding="utf-8", newline="").write(out)

v = io.open(P, encoding="utf-8").read()
for k in ["g_urlBar", "g_navModeB = !g_navModeB", "Icon::More : Icon::Back",
          "g_exitTopBtn = lv_btn_create", "lv_obj_align(g_urlArea, LV_ALIGN_TOP_LEFT, 0, 0)"]:
    log.append("verify %-46s %s" % (k, k in v))
print("\n".join(log))
