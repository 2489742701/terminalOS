# -*- coding: utf-8 -*-
"""缩略图 v3 —— app 侧：缩略图构建 / 全屏看图 / 另存 / 串口诊断。"""
import io, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fail = []


def load(rel):
    p = os.path.join(ROOT, rel)
    with io.open(p, 'r', encoding='utf-8', newline='') as f:
        return p, f.read()


def save(p, text):
    with io.open(p, 'w', encoding='utf-8', newline='') as f:
        f.write(text)


def sub(p, text, old, new):
    nl = '\r\n' if '\r\n' in text else '\n'
    o = old.replace('\n', nl)
    n = new.replace('\n', nl)
    cnt = text.count(o)
    if cnt != 1:
        fail.append('%s: count=%d :: %s' % (p, cnt, old.split('\n')[0][:70]))
        return text
    return text.replace(o, n, 1)


# ══════════════════════════════════════════════════════════════════════
# 1) sd_card.h / .cpp —— 二进制写文件
# ══════════════════════════════════════════════════════════════════════
p, t = load('src/hal/sd_card.h')
t = sub(p, t,
"""bool writeFile(const char* path, const String& data);
bool readFile(const char* path, String& out);""",
"""bool writeFile(const char* path, const String& data);
bool readFile(const char* path, String& out);

/* 二进制整文件写（图片另存用）。
   ⚠️ 必须是独立的入口：writeFile 收 String，而 String 构造/拼接都按 NUL
   结尾处理 —— JPEG/PNG 中间随便一个 0x00 就把后面全吃了（readFile 已经
   栽过一次，见 docs/09 的 L4）。 */
bool writeFileBin(const char* path, const uint8_t* data, size_t len);""")
save(p, t)

p, t = load('src/hal/sd_card.cpp')
t = sub(p, t,
"""bool readFile(const char* path, String& out) {""",
"""bool writeFileBin(const char* path, const uint8_t* data, size_t len) {
  if (!g_mounted || !path || !data || len == 0) return false;
  const char* slash = strrchr(path, '/');
  if (slash && slash > path) {
    char dir[64];
    int n = (int)(slash - path);
    if (n >= (int)sizeof(dir)) n = (int)sizeof(dir) - 1;
    memcpy(dir, path, (size_t)n);
    dir[n] = '\\0';
    if (!SD.exists(dir)) SD.mkdir(dir);
  }
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  size_t w = f.write(data, len);
  f.close();
  return w == len;
}

bool readFile(const char* path, String& out) {""")
save(p, t)

# ══════════════════════════════════════════════════════════════════════
# 2) browser_screen.cpp —— 看图 / 另存（插在 toastHide 之后）
# ══════════════════════════════════════════════════════════════════════
p, t = load('src/app/browser_screen.cpp')

VIEWER = r'''
/* ═══════════════════════════════════════════════════════════════════════════
 * 缩略图 / 全屏看图 / 另存（2026-09-25）
 *
 * 链路：<img> → 后台任务下载**原始字节**（img_dsc）→ 渲染前在 UI 线程重采样成
 *       小图（img_thumb，默认最长边 96）→ 页面上一块小图 → 点开全屏大图 → 另存。
 *
 * 为什么不一上来就按屏幕大小解码：网页图片动辄 1000+ 像素宽，整屏铺一张、
 * 版面全乱；而且 tjpgd 解一张 1920x1080 要 6MB 中间缓冲。
 * 小图 + 点开大图才是 480x480 上唯一说得通的形态。
 * ═══════════════════════════════════════════════════════════════════════ */

static void downloadImage(LayoutNode* node);   /* 前向：另存按钮要用 */

/* 全屏看图挂在 lv_layer_top() 上（整个屏之上），所以不受 g_content 的
   滚动/裁剪影响。生命周期严格跟着 g_viewNode 走 —— 退出浏览器、换页都必须拆。 */
static lv_obj_t* g_viewRoot = nullptr;
static void* g_viewDsc = nullptr;
static LayoutNode* g_viewNode = nullptr;
static LayoutNode* g_viewPending = nullptr;    /* 点击只记指针，tick 里再开 */
static bool g_viewClosePending = false;

static void closeImageViewer() {
  if (g_viewRoot && lv_obj_is_valid(g_viewRoot)) lv_obj_del(g_viewRoot);
  g_viewRoot = nullptr;
  if (g_viewDsc) {
    tb_image_dsc_free(g_viewDsc);
    g_viewDsc = nullptr;
  }
  g_viewNode = nullptr;
}

/* ⚠️ 三个回调都**不直接动手**：关覆盖层 = 删掉正在派发事件的树，
   开覆盖层 = 在自己的事件回调里往树上加节点。一律置标志，tick 里做。 */
static void viewCloseCb(lv_event_t* e) { (void)e; g_viewClosePending = true; }
static void viewSaveCb(lv_event_t* e) {
  (void)e;
  if (g_viewNode) downloadImage(g_viewNode);
}

static void openImageViewer(LayoutNode* node) {
  if (!node || !node->img_dsc) return;
  closeImageViewer();

  /* 大图按屏幕再重采样一次：这时候才值得解到 448px，缩略图那点分辨率放大
     只会更糊。box 留出上下按钮的位置。 */
  int w = 0, h = 0;
  void* dsc = tb_image_resample(node->img_dsc, 448, 384, &w, &h);
  if (!dsc) {
    toast("这张图解不出来");
    return;
  }
  g_viewDsc = dsc;
  g_viewNode = node;

  lv_obj_t* root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, 480, 480);
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  /* 点空白处关闭（图片本身不可点，所以点图不会误关） */
  lv_obj_add_event_cb(root, viewCloseCb, LV_EVENT_CLICKED, nullptr);
  g_viewRoot = root;

  lv_obj_t* img = lv_img_create(root);
  lv_img_set_src(img, (const lv_img_dsc_t*)dsc);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, -16);
  lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* close = lv_btn_create(root);
  lv_obj_remove_style_all(close);
  lv_obj_set_size(close, 52, 52);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -4, 4);
  lv_obj_set_style_radius(close, 26, 0);
  lv_obj_set_style_bg_color(close, lv_color_hex(0x222222), 0);
  lv_obj_set_style_bg_opa(close, LV_OPA_80, 0);
  lv_obj_add_event_cb(close, viewCloseCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* cl = lv_label_create(close);
  lv_label_set_text(cl, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(cl, lv_color_white(), 0);
  lv_obj_center(cl);

  lv_obj_t* sv = lv_btn_create(root);
  lv_obj_set_size(sv, 156, 44);
  lv_obj_align(sv, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_set_style_bg_color(sv, lv_color_hex(0x1F6FEB), 0);
  lv_obj_add_event_cb(sv, viewSaveCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* sl = lv_label_create(sv);
  lv_label_set_text(sl, "保存图片");
  lv_obj_set_style_text_font(sl, &font_zh_16, 0);
  lv_obj_center(sl);

  /* 左上角报尺寸：一眼看出"这张图小，放大也就这样" */
  lv_obj_t* info = lv_label_create(root);
  char buf[40];
  snprintf(buf, sizeof(buf), "%dx%d", w, h);
  lv_label_set_text(info, buf);
  lv_obj_set_style_text_color(info, lv_color_hex(0x999999), 0);
  lv_obj_align(info, LV_ALIGN_TOP_LEFT, 12, 16);

  Serial.printf("[Img] viewer %dx%d from %.64s\n", w, h,
                node->img_src ? node->img_src : "?");
}

/* 缩略图点击 → 只记指针。在事件回调里直接开覆盖层 = 在自己的事件处理过程中
   往对象树上加节点，跟链接点击踩的是同一个坑。 */
static void image_click_cb(void* node) {
  if (node) g_viewPending = (LayoutNode*)node;
}

/* 另存：SD 卡优先（容量大），没挂卡退回片内 LittleFS。
   ⚠️ 写的是**原始字节**，不是重采样后的小图 —— 存下来要能拿去别处看。 */
static void downloadImage(LayoutNode* node) {
  if (!node || !node->img_dsc) { toast("没有图片数据"); return; }
  lv_img_dsc_t* d = (lv_img_dsc_t*)node->img_dsc;
  const uint8_t* data = (const uint8_t*)d->data;
  size_t len = (size_t)d->data_size;
  if (!data || len == 0) { toast("没有图片数据"); return; }

  const char* ext =
      (len > 8 && data[0] == 0x89 && data[1] == 'P') ? "png" : "jpg";
  uint32_t hh = url_hash(String(node->img_src ? node->img_src : ""));
  char name[64];
  int written = 0;

  if (SDCard::mounted()) {
    snprintf(name, sizeof(name), "/gt/i%08lx.%s", (unsigned long)hh, ext);
    if (SDCard::writeFileBin(name, data, len)) written = 1;
  } else if (LittleFS.begin(false)) {
    snprintf(name, sizeof(name), "/i%08lx.%s", (unsigned long)hh, ext);
    File f = LittleFS.open(name, FILE_WRITE);
    if (f) {
      if (f.write(data, len) == len) written = 1;
      f.close();
    }
  } else {
    toast("存储不可用（插张卡吧）");
    return;
  }

  char msg[96];
  if (written)
    snprintf(msg, sizeof(msg), "已存 %s (%uKB)", name, (unsigned)(len / 1024));
  else
    snprintf(msg, sizeof(msg), "保存失败：%s", name);
  toast(msg);
  Serial.printf("[Img] save %s -> %s (%u B)\n", written ? "OK" : "FAIL", name,
                (unsigned)len);
}

'''
t = sub(p, t,
"""static void toastHide() {
  if (g_toast && lv_obj_is_valid(g_toast)) lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
}""",
"""static void toastHide() {
  if (g_toast && lv_obj_is_valid(g_toast)) lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
}
""" + VIEWER)

# ── buildThumbnails：放在 IMG_MAX 定义之后 ──
t = sub(p, t,
"""  Serial.printf("[Img] ready %d/%d, PSRAM free=%u\\n", ok, n,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
""",
"""  Serial.printf("[Img] ready %d/%d, PSRAM free=%u\\n", ok, n,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

/* 把已拿到字节的图**重采样成小缩略图**。
   ⚠️ 必须在 UI 线程、且必须在 layout_render_tree 之前调完：
   干跑数瓦片时看的就是 img_thumb，晚一步这批图就一条瓦片都不占。 */
static int buildThumbnails() {
  if (!g_layoutRoot) return 0;
  LayoutNode* imgs[IMG_MAX];
  int n = layout_collect_ready_images(g_layoutRoot, imgs, IMG_MAX);
  if (n <= 0) return 0;
  uint32_t t0 = millis();
  int made = 0;
  for (int i = 0; i < n; i++) {
    int w = 0, h = 0;
    void* th = tb_image_resample(imgs[i]->img_dsc, g_imgThumbPx, g_imgThumbPx,
                                 &w, &h);
    if (!th) continue;
    imgs[i]->img_thumb = th;
    made++;
  }
  Serial.printf("[Img] thumbs %d/%d in %u ms (box=%d)\\n", made, n,
                (unsigned)(millis() - t0), g_imgThumbPx);
  return made;
}
""")

# ── 全局缩略图尺寸（跟 IMG_MAX 放一起）──
t = sub(p, t,
"""static bool g_imgEnabled = true;  /* 串口 `img on|off`；慢页面可以临时关掉 */""",
"""static bool g_imgEnabled = true;  /* 串口 `img on|off`；慢页面可以临时关掉 */
static int  g_imgThumbPx = 96;    /* 缩略图长边上限（串口 `thumb <px>`） */""")

# ── tick：处理"待开/待关看图" ──
t = sub(p, t,
"""  if (g_linkPendingSet) {
    String target = g_linkPending;""",
"""  /* 看图覆盖层：开关都推迟到这里做，别在事件回调里动树 */
  if (g_viewClosePending) {
    g_viewClosePending = false;
    closeImageViewer();
  }
  if (g_viewPending) {
    LayoutNode* nd = g_viewPending;
    g_viewPending = nullptr;
    openImageViewer(nd);
  }

  if (g_linkPendingSet) {
    String target = g_linkPending;""")

# ── 渲染前先做缩略图 ──
t = sub(p, t,
"""        g_segStart = 0;
        /* 只铺第一段：长页面不再因为撞到 widget 上限而被砍掉后半截。 */
        layout_set_segment(0, PAGE_SEG_TILES);""",
"""        g_segStart = 0;
        /* 缩略图必须在渲染之前做完：干跑数瓦片时要看 img_thumb。 */
        buildThumbnails();
        /* 只铺第一段：长页面不再因为撞到 widget 上限而被砍掉后半截。 */
        layout_set_segment(0, PAGE_SEG_TILES);""")

# ── startFetch：换页前把看图覆盖层和旧树一起拆掉 ──
t = sub(p, t,
"""  freeLayoutTree();          /* 上一次的布局树（分段缓存）到此为止 */""",
"""  /* 覆盖层里握着旧树的节点指针，必须**先**拆覆盖层再释放布局树 */
  closeImageViewer();
  freeLayoutTree();          /* 上一次的布局树（分段缓存）到此为止 */""")

# ── 引擎初始化：注册缩略图点击回调 ──
t = sub(p, t,
"""  /* 网页里链接/胶囊被点击 → link_click_cb（只存 URL，tick 里再真正导航） */
  lvgl_renderer_set_link_callback(link_click_cb);""",
"""  /* 网页里链接/胶囊被点击 → link_click_cb（只存 URL，tick 里再真正导航） */
  lvgl_renderer_set_link_callback(link_click_cb);
  /* 缩略图被点 → image_click_cb（只存节点指针，tick 里再开全屏） */
  lvgl_renderer_set_image_callback(image_click_cb);""")

# ── 退出浏览器：拆覆盖层 ──
t = sub(p, t,
"""void BrowserScreen_close() {
  /* toast 的消失定时器持有 g_toast 指针，屏要拆了必须先注销，""",
"""void BrowserScreen_close() {
  /* 看图覆盖层挂在 lv_layer_top() 上（不属于任何屏），退出时必须自己拆，
     否则它会一直盖在桌面上，而且手里握着即将失效的节点指针。 */
  closeImageViewer();
  /* toast 的消失定时器持有 g_toast 指针，屏要拆了必须先注销，""")

# ── 外部入口：串口用 ──
t = sub(p, t,
"""void BrowserScreen_setImages(bool on) {""",
"""/* 串口：把本页第 idx 张（1 起）图开成全屏，不点屏也能验。
   顺带把"这一页到底有几张图"报出来。 */
static int pageImageList(LayoutNode** out, int max) {
  if (!g_layoutRoot) return 0;
  return layout_collect_ready_images(g_layoutRoot, out, max);
}

void BrowserScreen_imgView(int idx) {
  LayoutNode* imgs[IMG_MAX];
  int n = pageImageList(imgs, IMG_MAX);
  Serial.printf("[Img] page has %d image(s)\\n", n);
  if (idx < 1 || idx > n) {
    Serial.printf("[Img] usage: imgview 1..%d\\n", n);
    return;
  }
  openImageViewer(imgs[idx - 1]);
}

void BrowserScreen_imgDownload(int idx) {
  LayoutNode* imgs[IMG_MAX];
  int n = pageImageList(imgs, IMG_MAX);
  if (idx < 1 || idx > n) {
    Serial.printf("[Img] usage: imgdl 1..%d (page has %d)\\n", n, n);
    return;
  }
  downloadImage(imgs[idx - 1]);
}

void BrowserScreen_setThumbPx(int px) {
  if (px < 32) px = 32;
  if (px > 240) px = 240;
  g_imgThumbPx = px;
  Serial.printf("[Img] thumb box = %d px（重新加载后生效）\\n", g_imgThumbPx);
}

void BrowserScreen_setImages(bool on) {""")
save(p, t)

# ══════════════════════════════════════════════════════════════════════
# 3) browser_screen.h
# ══════════════════════════════════════════════════════════════════════
p, t = load('src/app/browser_screen.h')
t = sub(p, t,
"""void BrowserScreen_setImages(bool on);
bool BrowserScreen_imagesEnabled();
void BrowserScreen_imgTest(const char* url);""",
"""void BrowserScreen_setImages(bool on);
bool BrowserScreen_imagesEnabled();
void BrowserScreen_imgTest(const char* url);
/* 缩略图长边上限（像素），串口 `thumb <px>`；重新加载页面后生效 */
void BrowserScreen_setThumbPx(int px);
/* 把本页第 idx 张图（1 起）开成全屏 / 另存 —— 不点屏也能验。串口 imgview / imgdl */
void BrowserScreen_imgView(int idx);
void BrowserScreen_imgDownload(int idx);""")
save(p, t)

# ══════════════════════════════════════════════════════════════════════
# 4) serial_console.cpp
# ══════════════════════════════════════════════════════════════════════
p, t = load('src/hal/serial_console.cpp')
t = sub(p, t,
"""  } else if (strcmp(cmd, "imgtest") == 0) {""",
"""  } else if (strcmp(cmd, "thumb") == 0) {
    /* 缩略图长边上限：thumb 96 / thumb 160。只影响重采样，不影响下载。 */
    BrowserScreen_setThumbPx((arg && *arg) ? atoi(arg) : 96);
  } else if (strcmp(cmd, "imgview") == 0) {
    /* 不开屏也能验全屏看图：imgview 1 */
    BrowserScreen_imgView((arg && *arg) ? atoi(arg) : 1);
  } else if (strcmp(cmd, "imgdl") == 0) {
    /* 验另存：imgdl 1 */
    BrowserScreen_imgDownload((arg && *arg) ? atoi(arg) : 1);
  } else if (strcmp(cmd, "imgtest") == 0) {""")
save(p, t)

print('FAILURES: %d' % len(fail))
for f in fail:
    print(' -', f)
sys.exit(1 if fail else 0)
