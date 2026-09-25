# -*- coding: utf-8 -*-
"""1) 修 imgview/imgdl：原图落盘后 img_dsc 为空，收集函数要认缩略图
   2) 加串口 `dram` 诊断：看清 DRAM 到底被谁吃了

⚠️ 记忆：Edit 工具在 CRLF 上假成功 —— 脚本改，改完 grep 核对。
"""
import io
BASE = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal'


def w(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


def rep(s, old, new, tag, cnt=1):
    n = s.count(old)
    assert n == cnt, ('anchor %s: found %d' % (tag, n))
    return s.replace(old, new, 1)


# ───────── 1) 引擎：新增 layout_collect_shown_images ─────────
H = BASE + r'\src\browser_engine\include\layout_engine.h'
s = io.open(H, encoding='utf-8').read()
old = """int layout_collect_ready_images(LayoutNode *root, LayoutNode **out, int max);"""
new = """int layout_collect_ready_images(LayoutNode *root, LayoutNode **out, int max);

/* 页面上"实际显示得出来"的图片：img_thumb 或 img_dsc 任一非空。
   ⚠️ 和上面那个不是一回事：collect_ready 只收还拿着原始字节的（做缩略图的输入）。
   原图落盘后内存那份会被释放（img_dsc = NULL），但缩略图还在 —— 看图 / 另存 /
   imgscan 要的是这一个。只认 img_dsc 会得出"这一页 0 张图"。 */
int layout_collect_shown_images(LayoutNode *root, LayoutNode **out, int max);"""
s = rep(s, old, new, 'eng-h')
w(H, s)

C = BASE + r'\src\browser_engine\src\layout_engine.cpp'
s = io.open(C, encoding='utf-8').read()
old = """int layout_collect_ready_images(LayoutNode *root, LayoutNode **out, int max) {
  int n = 0;
  if (!root || !out || max <= 0) return 0;
  collect_ready_rec(root, out, max, &n, 0);
  return n;
}"""
new = old + """

/* 见 layout_engine.h：判定是"缩略图或原始字节任一还在"。
   兄弟用迭代、父子才递归 —— 铁律照旧。 */
static void collect_shown_rec(LayoutNode *node, LayoutNode **out, int max,
                              int *n, int depth) {
  if (!node || depth > MAX_LAYOUT_DEPTH) return;
  for (LayoutNode *c = node; c && *n < max; c = c->next_sibling) {
    if (c->type == ELEMENT_IMAGE && (c->img_thumb || c->img_dsc))
      out[(*n)++] = c;
    if (c->first_child)
      collect_shown_rec(c->first_child, out, max, n, depth + 1);
  }
}

int layout_collect_shown_images(LayoutNode *root, LayoutNode **out, int max) {
  int n = 0;
  if (!root || !out || max <= 0) return 0;
  collect_shown_rec(root, out, max, &n, 0);
  return n;
}"""
s = rep(s, old, new, 'eng-cpp')

# imgscan 的 with_pic 判定也要跟上，否则诊断会报"图都没下载"
old = """      if (c->img_dsc) (*with_pic)++;"""
new = """      if (c->img_dsc || c->img_thumb) (*with_pic)++;"""
s = rep(s, old, new, 'eng-dump')
old = """                      c->img_w, c->img_h, c->img_dsc ? "Y" : "N");"""
new = """                      c->img_w, c->img_h,
                      (c->img_thumb ? "thumb" : (c->img_dsc ? "raw" : "N")));"""
s = rep(s, old, new, 'eng-dump2')
w(C, s)
print('1) engine ok')

# ───────── 2) App：pageImageList 改用 shown ─────────
B = BASE + r'\src\app\browser_screen.cpp'
s = io.open(B, encoding='utf-8').read()
old = """static int pageImageList(LayoutNode** out, int max) {
  if (!g_layoutRoot) return 0;
  return layout_collect_ready_images(g_layoutRoot, out, max);
}"""
new = """static int pageImageList(LayoutNode** out, int max) {
  if (!g_layoutRoot) return 0;
  /* ⚠️ 必须是 shown 不是 ready：原图落盘后 img_dsc 就空了（内存只留缩略图），
     用 ready 会得到"这一页 0 张图"，imgview / imgdl 全部失效。 */
  return layout_collect_shown_images(g_layoutRoot, out, max);
}"""
s = rep(s, old, new, 'pageImageList')

# memInfo 实现：插在 BrowserScreen_imgScan 前
old = """void BrowserScreen_imgScan() {"""
new = """/* 串口 `dram`：把内存账摊开看。
   ⚠️ 运行时**不调** heap_caps_get_largest_free_block —— 它要遍历整个堆，
   实测会和 WiFi 抢时间片触发 wdt。free / minimum_free 已经够定位了。 */
void BrowserScreen_memInfo() {
  Serial.printf("[Mem] DRAM  free=%u  min_free=%u\\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  Serial.printf("[Mem] PSRAM free=%u\\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  Serial.printf("[Mem] LVGL pool %u/%u B used=%u%% frag=%u%% peak=%u\\n",
                (unsigned)(m.total_size - m.free_size), (unsigned)m.total_size,
                (unsigned)m.used_pct, (unsigned)m.frag_pct,
                (unsigned)m.max_used);

  size_t cacheBytes = 0;
  for (int i = 0; i < PAGE_CACHE_SLOTS; i++)
    if (g_pageCache[i].len) cacheBytes += g_pageCache[i].len;
  Serial.printf("[Mem] layout=%s  pageCache=%u B (2 slots)  sd=%s\\n",
                g_layoutRoot ? "held(in PSRAM)" : "none",
                (unsigned)cacheBytes, SDCard::mounted() ? "mounted" : "no");
}

void BrowserScreen_imgScan() {"""
s = rep(s, old, new, 'memInfo')
w(B, s)
print('2) app ok')

# ───────── 3) 头文件声明 ─────────
BH = BASE + r'\src\app\browser_screen.h'
s = io.open(BH, encoding='utf-8').read()
old = """void BrowserScreen_imgScan();"""
new = """void BrowserScreen_imgScan();
/* 串口 `dram`：DRAM / PSRAM / LVGL 池 / 页面缓存的占用账 */
void BrowserScreen_memInfo();"""
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)
w(BH, s)
print('3) hdr ok')

# ───────── 4) 串口命令 ─────────
S = BASE + r'\src\hal\serial_console.cpp'
s = io.open(S, encoding='utf-8').read()
old = """  } else if (strcmp(cmd, "imgview") == 0) {"""
new = """  } else if (strcmp(cmd, "dram") == 0) {
    /* 内存账：DRAM / PSRAM / LVGL 池 / 页面缓存各占多少 */
    BrowserScreen_memInfo();
  } else if (strcmp(cmd, "imgview") == 0) {"""
s = rep(s, old, new, 'console')
w(S, s)
print('4) console ok')
print('ALL PATCHED')
