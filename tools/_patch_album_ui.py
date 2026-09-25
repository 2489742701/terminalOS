# -*- coding: utf-8 -*-
"""缓存相册 · 第二步：相册本体 + 入口 + 串口命令

相册 = 本地缓存下来的网页图片的可视化浏览。数据就是 storeSave 已经落盘的
t<hash>.thm（缩略图）和 i<hash>.bin（原图），相册全程不联网。
"""
import io
BASE = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal'
B = BASE + r'\src\app\browser_screen.cpp'


def w(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


def rep(s, old, new, tag, cnt=1):
    n = s.count(old)
    assert n == cnt, ('anchor %s: found %d' % (tag, n))
    return s.replace(old, new, 1)


s = io.open(B, encoding='utf-8').read()

# ── 1) thumbFromBlob 实现（store 段末尾）──
old = """static void imgBlobName(char* out, size_t cap, const char* url, bool thumb) {
  uint32_t hh = url_hash(String(url ? url : ""));
  snprintf(out, cap, thumb ? "t%08lx.thm" : "i%08lx.bin", (unsigned long)hh);
}"""
new = """static void imgBlobName(char* out, size_t cap, const char* url, bool thumb) {
  char h[12];
  imgHashOf(h, sizeof(h), url);
  snprintf(out, cap, thumb ? "t%s.thm" : "i%s.bin", h);
}

/* 从 GTT1 blob 直接构造一个**可以拿去显示**的 dsc —— 缩略图存的就是 RGB565
   像素，读回来不用解码，这就是"二次打开更快"的本钱。
   bpp 由 len/(w*h) 反推：2 = RGB565，3 = RGB565A。 */
static void* thumbFromBlob(const uint8_t* b, size_t n) {
  int w = 0, h = 0;
  const uint8_t* px = nullptr;
  size_t plen = 0;
  if (!blobUnpack(b, n, "GTT1", &w, &h, &px, &plen)) return nullptr;
  if (w <= 0 || h <= 0 || plen == 0) return nullptr;
  size_t bpp = plen / ((size_t)w * (size_t)h);
  if (bpp != 2 && bpp != 3) return nullptr;

  uint8_t* dst = (uint8_t*)heap_caps_malloc(plen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!dst) return nullptr;
  memcpy(dst, px, plen);
  lv_img_dsc_t* d = (lv_img_dsc_t*)heap_caps_malloc(sizeof(lv_img_dsc_t),
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!d) { heap_caps_free(dst); return nullptr; }
  memset(d, 0, sizeof(*d));
  d->header.always_zero = 0;
  d->header.w = (uint32_t)w;
  d->header.h = (uint32_t)h;
  d->header.cf = (bpp == 3) ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_TRUE_COLOR;
  d->data = dst;
  d->data_size = (uint32_t)plen;
  return d;
}"""
s = rep(s, old, new, 'thumbFromBlob')

# ── 2) 前向声明 ──
old = """static void showDownloadsHome();  /* 下载列表（定义在下方） */"""
new = """static void showDownloadsHome();  /* 下载列表（定义在下方） */
static void showCacheAlbum();     /* 缓存相册（定义在下方） */
static void albumRelease();       /* contentReset 里要调：换页就释放相册缩略图 */
static void album_open_list_cb(lv_event_t* e);   /* 搜索首页 -> 缓存相册 */"""
s = rep(s, old, new, 'fwd')

# ── 3) contentReset 里释放 ──
old = """static void contentReset() {
  if (g_content && lv_obj_is_valid(g_content)) lv_obj_clean(g_content);
  g_searchTa = nullptr;
}"""
new = """static void contentReset() {
  if (g_content && lv_obj_is_valid(g_content)) lv_obj_clean(g_content);
  g_searchTa = nullptr;
  /* ⚠️ 顺序不能反：先把引用这些 dsc 的 lv_img 清掉，再释放像素。
     反过来的话 LVGL 图片缓存里还留着指向已释放内存的条目，下次命中就炸。 */
  albumRelease();
}"""
s = rep(s, old, new, 'contentReset')

# ── 4) 枚举 ──
old = """enum PendingUiKind { UI_PEND_NONE = 0, UI_PEND_SEARCH, UI_PEND_DOWNLOADS };"""
new = """enum PendingUiKind { UI_PEND_NONE = 0, UI_PEND_SEARCH, UI_PEND_DOWNLOADS,
                     UI_PEND_ALBUM };"""
s = rep(s, old, new, 'enum')

# ── 5) tick 处理 ──
old = """    if (kind == UI_PEND_DOWNLOADS) showDownloadsHome();"""
new = """    if (kind == UI_PEND_DOWNLOADS) showDownloadsHome();
    else if (kind == UI_PEND_ALBUM) showCacheAlbum();"""
s = rep(s, old, new, 'tick')

# ── 6) 搜索首页入口 chip ──
old = """  makeChipBtn(dlRow, "下载的网站", dl_open_list_cb, NULL);"""
new = """  makeChipBtn(dlRow, "下载的网站", dl_open_list_cb, NULL);
  makeChipBtn(dlRow, "缓存相册", album_open_list_cb, NULL);"""
s = rep(s, old, new, 'chip')

# ── 7) 入口回调（放在 dl_open_list_cb 之后）──
old = """static void dl_back_cb(lv_event_t* e) {"""
new = """static void album_open_list_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  dl_goto(UI_PEND_ALBUM);
}

static void dl_back_cb(lv_event_t* e) {"""
s = rep(s, old, new, 'album-entry')

# ── 8) 相册本体（插在匿名 namespace 结束前）──
album = r"""
/* ═══ 缓存相册（2026-09-25，master 要的）══════════════════════════════════
 * 把缓存下来的网页图片当相册翻。
 *   数据 = storeSave 已经落盘的 t<hash>.thm（缩略图）+ i<hash>.bin（原图）
 *   全程不联网 —— 断网、离线也能看，这正是"下载到本地"的意义。
 *
 * ⚠️ 缩略图 dsc 的生命周期绑在**页面**上：contentReset() 会调 albumRelease()。
 *    别在别处单独释放，也别在 UI 还引用它们的时候释放。
 */
#define ALBUM_MAX 36

static void* g_albumDsc[ALBUM_MAX];
static char  g_albumHash[ALBUM_MAX][12];
static int   g_albumN = 0;
static int   g_albumPending = -1;

static void albumRelease() {
  for (int i = 0; i < ALBUM_MAX; i++) {
    if (g_albumDsc[i]) {
      /* tb_image_dsc_free 会 invalidate LVGL 图片缓存，所以 UI 先清掉就安全 */
      tb_image_dsc_free(g_albumDsc[i]);
      g_albumDsc[i] = nullptr;
    }
    g_albumHash[i][0] = '\0';
  }
  g_albumN = 0;
  g_albumPending = -1;
}

/* 收一张：base 形如 "t1234abcd.thm"。返回 true = 收下了（或已经有了）。 */
static bool albumAdd(const char* base) {
  if (g_albumN >= ALBUM_MAX) return false;
  const char* dot = strchr(base, '.');
  if (!dot) return false;
  size_t hl = (size_t)(dot - base - 1);      /* 去掉开头的 t 和 ".thm" */
  if (hl < 1 || hl > 8) return false;
  char h[12];
  memcpy(h, base + 1, hl);
  h[hl] = '\0';

  for (int i = 0; i < g_albumN; i++)
    if (strcmp(g_albumHash[i], h) == 0) return true;   /* 去重 */

  char nm[24];
  snprintf(nm, sizeof(nm), "t%s.thm", h);
  uint8_t* blob = nullptr;
  size_t blen = 0;
  if (!storeLoad(nm, &blob, &blen)) return false;
  void* d = thumbFromBlob(blob, blen);
  heap_caps_free(blob);
  if (!d) return false;

  snprintf(g_albumHash[g_albumN], sizeof(g_albumHash[0]), "%s", h);
  g_albumDsc[g_albumN] = d;
  g_albumN++;
  return true;
}

static int albumScan() {
  albumRelease();
  if (SDCard::mounted()) {
    String names[64];
    int cnt = SDCard::listDirNames("/gt", names, 64);
    for (int i = 0; i < cnt; i++) {
      const char* nm = names[i].c_str();
      if (nm[0] == 't' && strstr(nm, ".thm")) albumAdd(nm);
    }
  } else if (LittleFS.begin(false)) {
    File root = LittleFS.open("/");
    File f = root.openNextFile();
    while (f) {
      String nm = String(f.name());
      f = root.openNextFile();              /* 先推进再处理 */
      const char* slash = strrchr(nm.c_str(), '/');
      const char* base = slash ? slash + 1 : nm.c_str();
      if (base[0] == 't' && strstr(base, ".thm")) albumAdd(base);
    }
    /* 挂载后不 end()：会把正在跑的页面服务器弄成 404 */
  }
  Serial.printf("[Album] scanned %d thumb(s)\n", g_albumN);
  return g_albumN;
}

/* 点缩略图 → 开全屏。开覆盖层同样是"在自己的回调里加节点"，延迟一拍。 */
static void albumOpenTimerCb(lv_timer_t* t) {
  lv_timer_del(t);
  int i = g_albumPending;
  g_albumPending = -1;
  if (i >= 0 && i < g_albumN) openImageViewerHash(g_albumHash[i]);
}

static void albumOpenCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_albumPending = (int)(intptr_t)lv_event_get_user_data(e);
  lv_timer_t* t = lv_timer_create(albumOpenTimerCb, 1, nullptr);
  if (t) lv_timer_set_repeat_count(t, 1);
}

static void albumBackCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  dl_goto(UI_PEND_SEARCH);
}

static void showCacheAlbum() {
  if (!g_content || !lv_obj_is_valid(g_content)) return;
  hideLoadingOverlay();
  contentReset();     /* ⚠️ 内部会 albumRelease —— 先清 UI 再释放 dsc */
  g_state = BROWSER_LOADED;
  g_currentUrl = "";
  if (g_urlArea && lv_obj_is_valid(g_urlArea))
    lv_label_set_text(g_urlArea, "缓存相册");
  updateNavButtons();

  lv_obj_t* title = lv_label_create(g_content);
  lv_label_set_text(title, "缓存相册");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &font_zh_16, 0);

  int n = albumScan();

  lv_obj_t* row = makeRow(g_content, false);
  makeChipBtn(row, "返回", albumBackCb, NULL);
  char cnt[24];
  snprintf(cnt, sizeof(cnt), "%d 张", n);
  lv_obj_t* cl = lv_label_create(row);
  lv_label_set_text(cl, cnt);
  lv_obj_set_style_text_color(cl, lv_color_hex(0x999999), 0);
  lv_obj_set_style_text_font(cl, &font_zh_16, 0);

  if (n <= 0) {
    lv_obj_t* m = lv_label_create(g_content);
    lv_label_set_text(m, "还没有缓存的图片（先逛个网页）");
    lv_obj_set_style_text_color(m, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(m, &font_zh_16, 0);
    return;
  }

  lv_obj_t* grid = lv_obj_create(g_content);
  lv_obj_set_size(grid, CONTENT_W, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_all(grid, 4, 0);
  lv_obj_set_style_pad_gap(grid, 6, 0);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < n; i++) {
    lv_obj_t* cell = lv_btn_create(grid);
    lv_obj_set_size(cell, 140, 140);
    lv_obj_set_style_bg_color(cell, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_color(cell, lv_color_hex(0x333333), LV_STATE_PRESSED);
    lv_obj_set_style_radius(cell, 6, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(cell, albumOpenCb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)i);
    lv_obj_t* im = lv_img_create(cell);
    lv_img_set_src(im, (const lv_img_dsc_t*)g_albumDsc[i]);
    lv_obj_center(im);
  }
}

"""
old = """}  // namespace

/* ═══════════════════════════════════════════════════════════════════════════
 * 创建浏览器屏幕"""
new = album + """}  // namespace

/* ═══════════════════════════════════════════════════════════════════════════
 * 创建浏览器屏幕"""
s = rep(s, old, new, 'album-body')
w(B, s)
print('album ok')

# ── 9) 串口命令 ──
BH = BASE + r'\src\app\browser_screen.h'
s = io.open(BH, encoding='utf-8').read()
old = """/* 串口 `imgclose`：关掉看图覆盖层 */
void BrowserScreen_imgClose();"""
new = """/* 串口 `imgclose`：关掉看图覆盖层 */
void BrowserScreen_imgClose();
/* 串口 `album`：直接跳到缓存相册 */
void BrowserScreen_album();"""
s = rep(s, old, new, 'hdr2')
w(BH, s)

S = BASE + r'\src\hal\serial_console.cpp'
s = io.open(S, encoding='utf-8').read()
old = """  } else if (strcmp(cmd, "imgclose") == 0) {"""
new = """  } else if (strcmp(cmd, "album") == 0) {
    /* 缓存相册：不点屏也能进 */
    BrowserScreen_album();
  } else if (strcmp(cmd, "imgclose") == 0) {"""
s = rep(s, old, new, 'console2')
w(S, s)
print('console ok')
