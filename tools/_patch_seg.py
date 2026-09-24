# -*- coding: utf-8 -*-
"""分段渲染：browser_screen.cpp 补丁（2026-09-25）"""
import io, sys, re

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"
s = io.open(P, encoding="utf-8", newline="").read()
orig = s


def sub1(old, new, tag):
    global s
    if old not in s:
        print("MISS: " + tag)
        sys.exit(1)
    if s.count(old) != 1:
        print("DUP(%d): %s" % (s.count(old), tag))
        sys.exit(1)
    s = s.replace(old, new, 1)
    print("OK: " + tag)


# ── 1. 分段全局量 + freeLayoutTree ────────────────────────────────────────
sub1(
"""String g_linkPending;
bool g_linkPendingSet = false;
""",
"""String g_linkPending;
bool g_linkPendingSet = false;

/* ── 分段渲染（master 2026-09-25：「点加载下半页，则加载下半页截断上半」）──
 * 引擎一次只铺 PAGE_SEG_TILES 块瓦片，内容末尾给一条「上一段 / 第 x/y 段 / 下一段」。
 * 布局树渲染完**不释放**，留着当本地缓存：翻段只是拿同一棵树重铺一遍，
 * 不重新联网、不重新解析（实测几十毫秒）。
 * ⛔ 翻段按钮在 g_content 里 → 绝不能在它自己的 CLICKED 回调里 contentReset()
 *    （会把正在派发事件的对象删掉）。回调只置 g_segPending，由 tick 真正执行。 */
static const int PAGE_SEG_TILES = 60;   /* 一段铺多少块瓦片 */
static int g_segStart = 0;              /* 当前段的起始瓦片下标 */
static int g_segTotal = 0;              /* 本页瓦片总数（引擎干跑得出，0=未知） */
static int g_segPending = -1;           /* >=0 = 待执行的翻段目标 */

/* 释放布局树的唯一出口：树没了，引擎里"已摊平"的标记必须一起清，
   否则新树可能复用同一块地址被误判成已准备 → 跳过摊平 → 版面错乱。 */
static void freeLayoutTree() {
  if (g_layoutRoot) {
    tactilebrowser_free_layout(g_layoutRoot);
    g_layoutRoot = nullptr;
  }
  layout_forget_prepare();
  g_segStart = 0;
  g_segTotal = 0;
  g_segPending = -1;
}
static void addSegBar();        /* 分段导航条（定义在 tick 之前） */
static void renderPageSeg(int);  /* 翻段（定义在 tick 之前） */
""",
"1 globals")

# ── 2. startFetch 里改成 freeLayoutTree ───────────────────────────────────
sub1(
"""  g_layoutRoot = nullptr;
  g_state = BROWSER_LOADING;
""",
"""  freeLayoutTree();          /* 上一次的布局树（分段缓存）到此为止 */
  g_state = BROWSER_LOADING;
""",
"2 startFetch")

# ── 3. 渲染完成：布分段 + 保留布局树 + 挂导航条 ───────────────────────────
sub1(
"""        uint32_t tRender = millis();
        RenderResult r = tactilebrowser_render_layout(g_layoutRoot, g_content, CONTENT_W, CONTENT_H);
        uint32_t renderMs = millis() - tRender;
        tactilebrowser_free_layout(g_layoutRoot);
        g_layoutRoot = nullptr;
        Serial.printf("[Browser] render_layout took %u ms\\n", (unsigned)renderMs);
""",
"""        uint32_t tRender = millis();
        g_segStart = 0;
        /* 只铺第一段：长页面不再因为撞到 widget 上限而被砍掉后半截。 */
        layout_set_segment(0, PAGE_SEG_TILES);
        RenderResult r = tactilebrowser_render_layout(g_layoutRoot, g_content, CONTENT_W, CONTENT_H);
        uint32_t renderMs = millis() - tRender;
        g_segTotal = layout_tile_total();
        /* ⚠️ 布局树**故意不释放**：它是翻段的本地缓存。
           只有下一次 startFetch() 或退出浏览器（freeLayoutTree）才释放。 */
        Serial.printf("[Browser] render_layout took %u ms, tiles=%d, seg=%d\\n",
                      (unsigned)renderMs, g_segTotal, PAGE_SEG_TILES);
        if (r == RENDER_SUCCESS && g_segTotal > PAGE_SEG_TILES) addSegBar();
""",
"3 render")

# ── 4. 分段导航条 + 翻段实现，插在 tick 之前 ──────────────────────────────
sub1(
"""/* ── tick：状态机驱动 ── */
""",
"""/* ── 分段导航条 ──
   铺在内容末尾，随每一段一起重建（contentReset 会把它一起清掉）。
   ⚠️ 按钮回调里**只置标志**：真正翻段在 tick 里做。 */
static void seg_prev_cb(lv_event_t* e) {
  (void)e;
  if (g_segStart <= 0) return;
  g_segPending = g_segStart - PAGE_SEG_TILES;
}
static void seg_next_cb(lv_event_t* e) {
  (void)e;
  if (g_segTotal > 0 && g_segStart + PAGE_SEG_TILES >= g_segTotal) return;
  g_segPending = g_segStart + PAGE_SEG_TILES;
}

static void addSegBar() {
  if (!g_content || !lv_obj_is_valid(g_content)) return;
  int segs = (g_segTotal + PAGE_SEG_TILES - 1) / PAGE_SEG_TILES;
  int cur = g_segStart / PAGE_SEG_TILES + 1;

  lv_obj_t* row = lv_obj_create(g_content);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(row, 10, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_top(row, 10, 0);

  lv_obj_t* pb = makeChipBtn(row, "上一段", seg_prev_cb, NULL);
  lv_obj_set_size(pb, 84, 34);

  char t[32];
  snprintf(t, sizeof(t), "%d / %d 段", cur, segs);
  lv_obj_t* tl = lv_label_create(row);
  lv_label_set_text(tl, t);
  lv_obj_set_style_text_color(tl, lv_color_hex(0x999999), 0);
  lv_obj_set_style_text_font(tl, &font_zh_16, 0);

  lv_obj_t* nb = makeChipBtn(row, "下一段", seg_next_cb, NULL);
  lv_obj_set_size(nb, 84, 34);

  /* 到头/到尾的那颗压暗（set_nav_enabled 会连子 label 一起压，LVGL 8 的
     opa 不级联，只设按钮的话文字还是纯白，看不出"这颗不能按"）。 */
  if (g_segStart <= 0) set_nav_enabled(pb, false);
  if (g_segStart + PAGE_SEG_TILES >= g_segTotal) set_nav_enabled(nb, false);
}

/* 真正翻段：清掉当前段 → 用同一棵布局树重铺下一段 → 回到顶部。
   不联网、不解析，只有几十毫秒。 */
static void renderPageSeg(int start) {
  if (!g_layoutRoot || !g_content || !lv_obj_is_valid(g_content)) return;
  if (start < 0) start = 0;
  if (g_segTotal > 0 && start >= g_segTotal)
    start = ((g_segTotal - 1) / PAGE_SEG_TILES) * PAGE_SEG_TILES;
  g_segStart = start;
  contentReset();
  layout_set_segment(start, PAGE_SEG_TILES);
  uint32_t t0 = millis();
  RenderResult r = tactilebrowser_render_layout(g_layoutRoot, g_content,
                                                CONTENT_W, CONTENT_H);
  Serial.printf("[Browser] seg %d..%d of %d -> %d, %u ms\\n", start,
                start + PAGE_SEG_TILES, g_segTotal, (int)r,
                (unsigned)(millis() - t0));
  if (g_segTotal > PAGE_SEG_TILES) addSegBar();
  lv_obj_update_layout(g_content);
  lv_obj_scroll_to_y(g_content, 0, LV_ANIM_OFF);
}

/* ── tick：状态机驱动 ── */
""",
"4 segbar")

# ── 5. tick 里处理翻段 ────────────────────────────────────────────────────
sub1(
"""  /* 新闻拉完了：收掉遮罩，回搜索首页把列表画出来。""",
"""  /* 分段翻页：按钮在 g_content 里，不能在自己的回调里清内容区。 */
  if (g_segPending >= 0) {
    int st = g_segPending;
    g_segPending = -1;
    renderPageSeg(st);
    return;
  }

  /* 新闻拉完了：收掉遮罩，回搜索首页把列表画出来。""",
"5 tick")

# ── 6. 关闭时用 freeLayoutTree ────────────────────────────────────────────
sub1(
"""  g_firstLoad = true;

  if (g_layoutRoot) {
    tactilebrowser_free_layout(g_layoutRoot);
    g_layoutRoot = nullptr;
  }
""",
"""  g_firstLoad = true;

  freeLayoutTree();
""",
"6 close")

# ── 7. 回搜索首页 / 下载列表：网页布局树不再需要，释放掉 ──────────────────
sub1(
"""  if (!g_content || !lv_obj_is_valid(g_content)) return;
  hideLoadingOverlay();
  if (g_searchKb) lv_obj_add_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);
  ime_hide();
  contentReset();
""",
"""  if (!g_content || !lv_obj_is_valid(g_content)) return;
  hideLoadingOverlay();
  if (g_searchKb) lv_obj_add_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);
  ime_hide();
  freeLayoutTree();     /* 离开网页：翻段用的布局树可以丢了 */
  contentReset();
""",
"7 searchhome")

if s != orig:
    io.open(P, "w", encoding="utf-8", newline="").write(s)
    print("WROTE", len(s))
else:
    print("NO CHANGE")
