# -*- coding: utf-8 -*-
"""缓存相册 · 第一步：把"看图"从 LayoutNode 解耦出来

相册里的图没有 LayoutNode（它们只是本地 blob），所以要把 openImageViewer 拆成
   openImageViewer(node)       网页里的图（有 src）
   openImageViewerHash(hash)   相册里的图（只有本地 hash）
两者共用 openImageViewWithDsc() 建 UI。

顺带把"按 hash 导出原图"抽出来（saveImageByHash），网页图片和相册共用。
"""
import io
B = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp'
s = io.open(B, encoding='utf-8').read()


def rep(s, old, new, tag, cnt=1):
    n = s.count(old)
    assert n == cnt, ('anchor %s: found %d' % (tag, n))
    return s.replace(old, new, 1)


# ── 1) 补 hash 工具 + 两个前向声明 ──
# ⚠️ 必须插在 viewSaveCb **之前**：viewSaveCb 要调 saveImageByHash。
old = """/* 覆盖层全屏盖在 lv_layer_top() 上 —— 底下 screen 的统一返回手势**收不到"""
new = """/* URL → 8 位十六进制 hash（blob 文件名的唯一来源，两边必须一致）。 */
static void imgHashOf(char* out, size_t cap, const char* url) {
  uint32_t hh = url_hash(String(url ? url : ""));
  snprintf(out, cap, "%08lx", (unsigned long)hh);
}

/* 把本地原图导出成"能看的名字"（pic<hash>.png/jpg）。
   网页图片（downloadImage）和缓存相册共用这一条 —— 定义在下面，但
   viewSaveCb / downloadImage 在它前面就要用，所以先声明。 */
static void saveImageByHash(const char* hash);
/* 从本地缩略图 blob 直接构造可显示的 dsc（RGB565 像素，不用解码）。 */
static void* thumbFromBlob(const uint8_t* b, size_t n);

/* 覆盖层全屏盖在 lv_layer_top() 上 —— 底下 screen 的统一返回手势**收不到"""
s = rep(s, old, new, 'decl')

# ── 2) saveImageByHash 实现（放在 loadFullImage 之前）──
old = """/* 按需加载原图：优先从本地读，盘上没有才联网。"""
new = """/* 按 hash 导出一张原图到"能看的名字"。原图平时就在本地（抓取时落的盘），
   所以这里只是读出来另写一个名字，不再联网。
   ⚠️ 走 blob 而不是 node->img_dsc：内存里那份早就还回去了。 */
static void saveImageByHash(const char* hash) {
  char nm[24];
  snprintf(nm, sizeof(nm), "i%s.bin", hash);
  uint8_t* blob = nullptr;
  size_t blen = 0;
  const uint8_t* data = nullptr;
  size_t len = 0;
  int iw = 0, ih = 0;
  if (!storeLoad(nm, &blob, &blen) ||
      !blobUnpack(blob, blen, "GTI1", &iw, &ih, &data, &len)) {
    if (blob) heap_caps_free(blob);
    toast("本地没有这张图");
    return;
  }

  const char* ext =
      (len > 8 && data[0] == 0x89 && data[1] == 'P') ? "png" : "jpg";
  char name[64];
  int written = 0;
  if (SDCard::mounted()) {
    snprintf(name, sizeof(name), "/gt/pic%s.%s", hash, ext);
    if (SDCard::writeFileBin(name, data, len)) written = 1;
  } else if (LittleFS.begin(false)) {
    snprintf(name, sizeof(name), "/pic%s.%s", hash, ext);
    File f = LittleFS.open(name, FILE_WRITE);
    if (f) {
      if (f.write(data, len) == len) written = 1;
      f.close();
    }
  } else {
    heap_caps_free(blob);
    toast("存储不可用（插张卡吧）");
    return;
  }
  heap_caps_free(blob);

  char msg[96];
  if (written)
    snprintf(msg, sizeof(msg), "已存 %s (%uKB)", name, (unsigned)(len / 1024));
  else
    snprintf(msg, sizeof(msg), "保存失败：%s", name);
  toast(msg);
  Serial.printf("[Img] save %s -> %s (%u B)\\n", written ? "OK" : "FAIL", name,
                (unsigned)len);
}

/* 按需加载原图：优先从本地读，盘上没有才联网。"""
s = rep(s, old, new, 'saveByHash')

# ── 3) downloadImage 瘦身 ──
old = """static void downloadImage(LayoutNode* node) {
  if (!node || !node->img_src) { toast("没有图片数据"); return; }

  /* 原图已经在本地（抓取时就落盘了）—— 另存只是把它导出成能看的名字。 */
  char nm[32];
  imgBlobName(nm, sizeof(nm), node->img_src, false);
  uint8_t* blob = nullptr;
  size_t blen = 0;
  const uint8_t* data = nullptr;
  size_t len = 0;
  int iw = 0, ih = 0;
  if (!storeLoad(nm, &blob, &blen) ||
      !blobUnpack(blob, blen, "GTI1", &iw, &ih, &data, &len)) {
    if (blob) heap_caps_free(blob);
    toast("本地没有这张图");
    return;
  }

  const char* ext =
      (len > 8 && data[0] == 0x89 && data[1] == 'P') ? "png" : "jpg";
  uint32_t hh = url_hash(String(node->img_src ? node->img_src : ""));
  char name[64];
  int written = 0;

  if (SDCard::mounted()) {
    snprintf(name, sizeof(name), "/gt/pic%08lx.%s", (unsigned long)hh, ext);
    if (SDCard::writeFileBin(name, data, len)) written = 1;
  } else if (LittleFS.begin(false)) {
    snprintf(name, sizeof(name), "/pic%08lx.%s", (unsigned long)hh, ext);
    File f = LittleFS.open(name, FILE_WRITE);
    if (f) {
      if (f.write(data, len) == len) written = 1;
      f.close();
    }
  } else {
    heap_caps_free(blob);
    toast("存储不可用（插张卡吧）");
    return;
  }
  heap_caps_free(blob);

  char msg[96];
  if (written)
    snprintf(msg, sizeof(msg), "已存 %s (%uKB)", name, (unsigned)(len / 1024));
  else
    snprintf(msg, sizeof(msg), "保存失败：%s", name);
  toast(msg);
  Serial.printf("[Img] save %s -> %s (%u B)\\n", written ? "OK" : "FAIL", name,
                (unsigned)len);
}"""
new = """static void downloadImage(LayoutNode* node) {
  if (!node || !node->img_src) { toast("没有图片数据"); return; }
  /* 原图已经在本地（抓取时就落盘了）—— 另存只是把它导出成能看的名字。 */
  char h[12];
  imgHashOf(h, sizeof(h), node->img_src);
  saveImageByHash(h);
}"""
s = rep(s, old, new, 'downloadImage')

# ── 4) viewSaveCb 支持相册（没有 node 时按 hash 存）──
old = """static void viewSaveCb(lv_event_t* e) {
  (void)e;
  if (g_viewNode) downloadImage(g_viewNode);
}"""
new = """static void viewSaveCb(lv_event_t* e) {
  (void)e;
  if (g_viewNode) downloadImage(g_viewNode);
  else if (g_viewHash[0]) saveImageByHash(g_viewHash);   /* 缓存相册进来的 */
}"""
s = rep(s, old, new, 'viewSaveCb')

# ── 5) g_viewHash 状态 ──
old = """static void* g_viewDsc = nullptr;
static LayoutNode* g_viewNode = nullptr;"""
new = """static void* g_viewDsc = nullptr;
static LayoutNode* g_viewNode = nullptr;   /* 网页里的图：非 NULL */
static char g_viewHash[12] = {0};          /* 相册里的图：node 为 NULL 时用它 */"""
s = rep(s, old, new, 'viewHash')

# ── 6) 拆 openImageViewer ──
old = """  g_viewDsc = dsc;
  g_viewNode = node;

  lv_obj_t* root = lv_obj_create(lv_layer_top());"""
new = """  g_viewNode = node;
  openImageViewWithDsc(dsc, w, h);
}

/* 缓存相册：没有 LayoutNode，只有本地 hash（t<hash>.thm / i<hash>.bin）。 */
static void openImageViewerHash(const char* hash) {
  char nm[24];
  snprintf(nm, sizeof(nm), "i%s.bin", hash);
  uint8_t* blob = nullptr;
  size_t blen = 0;
  const uint8_t* payload = nullptr;
  size_t len = 0;
  int iw = 0, ih = 0;
  if (!storeLoad(nm, &blob, &blen) ||
      !blobUnpack(blob, blen, "GTI1", &iw, &ih, &payload, &len)) {
    if (blob) heap_caps_free(blob);
    toast("本地没有这张图");
    return;
  }
  uint8_t* data = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!data) { heap_caps_free(blob); toast("内存不足"); return; }
  memcpy(data, payload, len);
  heap_caps_free(blob);

  void* full = tb_image_dsc_create(data, len, iw, ih, IMG_MAX_W, IMG_MAX_PIXELS,
                                   nullptr);
  if (!full) { toast("这张图解不出来"); return; }
  int w = 0, h = 0;
  void* dsc = tb_image_resample(full, 448, 384, &w, &h);
  tb_image_dsc_free(full);
  if (!dsc) { toast("这张图解不出来"); return; }
  snprintf(g_viewHash, sizeof(g_viewHash), "%s", hash);
  g_viewNode = nullptr;
  openImageViewWithDsc(dsc, w, h);
}

/* 建覆盖层 UI。dsc 已经是"解到 448x384 后"的像素，w/h 是它的实际尺寸。 */
static void openImageViewWithDsc(void* dsc, int w, int h) {
  closeImageViewer();
  g_viewDsc = dsc;

  lv_obj_t* root = lv_obj_create(lv_layer_top());"""
s = rep(s, old, new, 'split-viewer')

# ── 7) 原函数头：去掉重复的 closeImageViewer / 改判定 ──
old = """static void openImageViewer(LayoutNode* node) {
  if (!node || !node->img_src) return;
  closeImageViewer();

  /* 原图平时不在内存里 —— 点开才从磁盘读回来解码。 */
  void* full = loadFullImage(node);
  if (!full) {
    toast("这张图加载不出来");
    return;
  }
  /* 大图按屏幕再重采样一次：这时候才值得解到 448px，缩略图那点分辨率放大
     只会更糊。box 留出上下按钮的位置。 */
  int w = 0, h = 0;
  void* dsc = tb_image_resample(full, 448, 384, &w, &h);
  tb_image_dsc_free(full);   /* 像素已经在 dsc 里，原图字节可以还回去了 */
  if (!dsc) {
    toast("这张图解不出来");
    return;
  }"""
new = """static void openImageViewer(LayoutNode* node) {
  if (!node || !node->img_src) return;
  /* 原图平时不在内存里 —— 点开才从磁盘读回来解码。 */
  void* full = loadFullImage(node);
  if (!full) {
    toast("这张图加载不出来");
    return;
  }
  /* 大图按屏幕再重采样一次：这时候才值得解到 448px，缩略图那点分辨率放大
     只会更糊。box 留出上下按钮的位置。 */
  int w = 0, h = 0;
  void* dsc = tb_image_resample(full, 448, 384, &w, &h);
  tb_image_dsc_free(full);   /* 像素已经在 dsc 里，原图字节可以还回去了 */
  if (!dsc) {
    toast("这张图解不出来");
    return;
  }
  imgHashOf(g_viewHash, sizeof(g_viewHash), node->img_src);"""
s = rep(s, old, new, 'viewer-head')

io.open(B, 'w', encoding='utf-8', newline='').write(s)
print('ok')
