# -*- coding: utf-8 -*-
"""浏览器资源"下载到本地"改造（2026-09-25，master 定的方向）。

目标：磁盘（SD 卡 → LittleFS）是网页资源的家，内存只留"正在显示"的东西。
  · 原图抓完立刻落盘，生成完缩略图就把 PSRAM 里的原始字节还回去
  · 二次打开同一页：原图命中本地 → 不联网
  · 点开大图 / 另存：从盘读（盘上没有才现下）

⚠️ 记忆：Edit 工具在 CRLF 文件上会假成功 —— 一律脚本改，改完 grep 核对。
"""
import io, sys

BASE = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal'
B = BASE + r'\src\app\browser_screen.cpp'
L = BASE + r'\src\browser_engine\src\layout_engine.cpp'


def w(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


def rep(s, old, new, tag, cnt=1):
    n = s.count(old)
    assert n == cnt, ('anchor %s: found %d' % (tag, n))
    return s.replace(old, new, 1)


# ══════════════════ 1) 对象存储抽象 ══════════════════
s = io.open(B, encoding='utf-8').read()

anchor = """static bool g_imgEnabled = true;  /* 串口 `img on|off`；慢页面可以临时关掉 */
static int  g_imgThumbPx = 96;    /* 缩略图长边上限（串口 `thumb <px>`） */
"""
store = anchor + """
/* ═══ 本地对象存储（2026-09-25）════════════════════════════════════════════
 * master 定的方向：**下载到本地为准**，内存只是"正在显示"的工作区。
 *   · SD 卡优先（容量大）；没插卡退回片内 LittleFS。
 *   · ⛔ SD 未挂载时绝不能碰 SD 接口 —— 会去动与 LCD 共用的那条 SPI 总线。
 *
 * 两种 blob 都带 12 字节头 magic(4) + w(2) + h(2) + len(4) + payload：
 *   i<hash>.bin  GTI1  原图压缩字节（jpeg/png 原文）
 *   t<hash>.thm  GTT1  缩略图 RGB565(或 RGB565A) 像素
 * 带头的意义：回读时不用再 peek 尺寸，也不怕哪天格式判断改了。
 */
#define BLOB_HDR 12

static uint8_t* blobPack(const char* magic, int w, int h,
                         const uint8_t* payload, size_t len, size_t* outLen) {
  if (!payload || len == 0 || !outLen) return nullptr;
  uint8_t* b = (uint8_t*)heap_caps_malloc(BLOB_HDR + len,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!b) return nullptr;
  memcpy(b, magic, 4);
  b[4] = (uint8_t)(w & 0xFF);       b[5] = (uint8_t)((w >> 8) & 0xFF);
  b[6] = (uint8_t)(h & 0xFF);       b[7] = (uint8_t)((h >> 8) & 0xFF);
  b[8] = (uint8_t)(len & 0xFF);
  b[9] = (uint8_t)((len >> 8) & 0xFF);
  b[10] = (uint8_t)((len >> 16) & 0xFF);
  b[11] = (uint8_t)((len >> 24) & 0xFF);
  memcpy(b + BLOB_HDR, payload, len);
  *outLen = BLOB_HDR + len;
  return b;
}

static bool blobUnpack(const uint8_t* b, size_t n, const char* magic,
                       int* w, int* h, const uint8_t** payload, size_t* plen) {
  if (!b || n < BLOB_HDR + 1 || memcmp(b, magic, 4) != 0) return false;
  int iw = b[4] | (b[5] << 8);
  int ih = b[6] | (b[7] << 8);
  uint32_t l = (uint32_t)b[8] | ((uint32_t)b[9] << 8) |
               ((uint32_t)b[10] << 16) | ((uint32_t)b[11] << 24);
  if (iw <= 0 || ih <= 0 || l == 0 || (size_t)l != n - BLOB_HDR) return false;
  if (w) *w = iw;
  if (h) *h = ih;
  if (payload) *payload = b + BLOB_HDR;
  if (plen) *plen = (size_t)l;
  return true;
}

/* 路径：SD 上统一放 /gt/ 下（SD.mkdir 不递归，只能平铺）。 */
static void storePath(char* out, size_t cap, const char* name, bool sd) {
  if (sd) snprintf(out, cap, "/gt/%s", name);
  else    snprintf(out, cap, "/%s", name);
}

static bool storeSave(const char* name, const uint8_t* data, size_t len) {
  if (!name || !data || !len) return false;
  char p[64];
  if (SDCard::mounted()) {
    storePath(p, sizeof(p), name, true);
    return SDCard::writeFileBin(p, data, len);
  }
  if (!LittleFS.begin(false)) return false;
  storePath(p, sizeof(p), name, false);
  File f = LittleFS.open(p, FILE_WRITE);
  if (!f) { LittleFS.end(); return false; }
  size_t got = f.write(data, len);
  f.close();
  LittleFS.end();
  return got == len;
}

/* 回读。*out 从 PSRAM 分配，调用方 heap_caps_free。 */
static bool storeLoad(const char* name, uint8_t** out, size_t* outLen) {
  if (out) *out = nullptr;
  if (outLen) *outLen = 0;
  if (!name || !out || !outLen) return false;
  char p[64];

  if (SDCard::mounted()) {
    storePath(p, sizeof(p), name, true);
    return SDCard::readFileBin(p, out, outLen);
  }
  if (!LittleFS.begin(false)) return false;
  storePath(p, sizeof(p), name, false);
  File f = LittleFS.open(p, FILE_READ);
  if (!f) { LittleFS.end(); return false; }
  size_t n = (size_t)f.size();
  if (n == 0) { f.close(); LittleFS.end(); return false; }
  uint8_t* buf = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) { f.close(); LittleFS.end(); return false; }
  size_t got = f.read(buf, n);
  f.close();
  LittleFS.end();
  if (got != n) { heap_caps_free(buf); return false; }
  *out = buf;
  *outLen = n;
  return true;
}

/* 原图 / 缩略图的本地文件名（hash 来自绝对 URL）。 */
static void imgBlobName(char* out, size_t cap, const char* url, bool thumb) {
  uint32_t hh = url_hash(String(url ? url : ""));
  snprintf(out, cap, thumb ? "t%08lx.thm" : "i%08lx.bin", (unsigned long)hh);
}
"""
s = rep(s, anchor, store, 'store-anchor')
print('1) store ok')

# ══════════════════ 2) 抓取：缓存命中 + 落盘 ══════════════════
old = """    uint8_t* data = nullptr;
    size_t len = 0;
    uint32_t t0 = millis();
    Serial.printf("[Img] get %d/%d %.72s\\n", i + 1, n, g_preUrl[i].c_str());
    int rc = arduino_download_binary(g_preUrl[i].c_str(), &data, &len,
                                     IMG_MAX_BYTES, base.c_str());
    Serial.printf("[Img] got rc=%d %u B %ums\\n", rc, (unsigned)len,
                  (unsigned)(millis() - t0));
    if (rc != 0 || !data || len == 0) continue;

    int w = 0, h = 0;
    if (!tb_image_peek_size(data, len, &w, &h) || w < 16 || h < 16) {
      Serial.printf("[Img] drop fmt/size %dx%d\\n", w, h);
      heap_caps_free(data);
      continue;
    }
    int scale = 0;"""
new = """    uint8_t* data = nullptr;
    size_t len = 0;
    int w = 0, h = 0;
    uint32_t t0 = millis();
    char nm[32];
    imgBlobName(nm, sizeof(nm), g_preUrl[i].c_str(), false);

    /* ① 本地已有 → 回读，不联网。这是"下载到本地"该有的样子：
          第二次打开同一页不再产生流量，也不受网络抖动影响。 */
    uint8_t* cblob = nullptr;
    size_t cblen = 0;
    const uint8_t* payload = nullptr;
    if (storeLoad(nm, &cblob, &cblen) &&
        blobUnpack(cblob, cblen, "GTI1", &w, &h, &payload, &len)) {
      data = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!data) { heap_caps_free(cblob); continue; }
      memcpy(data, payload, len);
      heap_caps_free(cblob);
      Serial.printf("[Img] cache hit %s %dx%d %u B\\n", nm, w, h, (unsigned)len);
    } else {
      if (cblob) heap_caps_free(cblob);
      Serial.printf("[Img] get %d/%d %.72s\\n", i + 1, n, g_preUrl[i].c_str());
      int rc = arduino_download_binary(g_preUrl[i].c_str(), &data, &len,
                                       IMG_MAX_BYTES, base.c_str());
      Serial.printf("[Img] got rc=%d %u B %ums\\n", rc, (unsigned)len,
                    (unsigned)(millis() - t0));
      if (rc != 0 || !data || len == 0) continue;

      if (!tb_image_peek_size(data, len, &w, &h) || w < 16 || h < 16) {
        Serial.printf("[Img] drop fmt/size %dx%d\\n", w, h);
        heap_caps_free(data);
        continue;
      }
      /* ② 落盘：原图的家是磁盘，内存那份只是过客（缩略图做好就还回去）。 */
      size_t blen = 0;
      uint8_t* blob = blobPack("GTI1", w, h, data, len, &blen);
      if (blob) {
        if (storeSave(nm, blob, blen))
          Serial.printf("[Img] saved %s (%u B)\\n", nm, (unsigned)blen);
        heap_caps_free(blob);
      }
    }

    int scale = 0;"""
s = rep(s, old, new, 'fetch')
print('2) fetch ok')

# ══════════════════ 3) 缩略图落盘 + 释放原图 ══════════════════
old = """  for (int i = 0; i < n; i++) {
    int w = 0, h = 0;
    void* th = tb_image_resample(imgs[i]->img_dsc, g_imgThumbPx, g_imgThumbPx,
                                 &w, &h);
    if (!th) continue;
    imgs[i]->img_thumb = th;
    made++;
  }"""
new = """  for (int i = 0; i < n; i++) {
    int w = 0, h = 0;
    void* th = tb_image_resample(imgs[i]->img_dsc, g_imgThumbPx, g_imgThumbPx,
                                 &w, &h);
    if (!th) continue;
    imgs[i]->img_thumb = th;
    made++;

    /* 缩略图也落盘：将来再打开这一页可以直接读回像素，连解码都省了。 */
    if (imgs[i]->img_src) {
      char tnm[32];
      imgBlobName(tnm, sizeof(tnm), imgs[i]->img_src, true);
      lv_img_dsc_t* td = (lv_img_dsc_t*)th;
      size_t blen = 0;
      uint8_t* blob = blobPack("GTT1", w, h, (const uint8_t*)td->data,
                               (size_t)td->data_size, &blen);
      if (blob) {
        storeSave(tnm, blob, blen);
        heap_caps_free(blob);
      }
    }

    /* ⛔ 原图的"家"是磁盘：缩略图一做好就把 PSRAM 里的原始字节还回去。
       内存里只剩缩略图（96×96×2 ≈ 18KB），原图一张最多 64KB —— 六张就是
       384KB，那是过去 DRAM 被吃穿的主要来源。
       点开大图 / 另存时再从盘里读回来（loadFullImage）。 */
    if (imgs[i]->img_dsc) {
      tb_image_dsc_free(imgs[i]->img_dsc);
      imgs[i]->img_dsc = nullptr;
    }
  }"""
s = rep(s, old, new, 'thumb')
print('3) thumb ok')

# ══════════════════ 4) 按需加载原图 ══════════════════
old = """static void openImageViewer(LayoutNode* node) {
  if (!node || !node->img_dsc) return;
  closeImageViewer();

  /* 大图按屏幕再重采样一次：这时候才值得解到 448px，缩略图那点分辨率放大
     只会更糊。box 留出上下按钮的位置。 */
  int w = 0, h = 0;
  void* dsc = tb_image_resample(node->img_dsc, 448, 384, &w, &h);
  if (!dsc) {
    toast("这张图解不出来");
    return;
  }"""
new = """/* 按需加载原图：优先从本地读，盘上没有才联网。
   返回的是"拿原图字节包好的 dsc"，用完调用方必须 tb_image_dsc_free。
   ⚠️ 内存里平时**没有**原图（见 buildThumbnails）—— 这就是"点开才加载"。 */
static void* loadFullImage(LayoutNode* node) {
  if (!node || !node->img_src) return nullptr;
  char nm[32];
  imgBlobName(nm, sizeof(nm), node->img_src, false);

  uint8_t* blob = nullptr;
  size_t blen = 0;
  const uint8_t* payload = nullptr;
  size_t len = 0;
  int w = 0, h = 0;
  uint8_t* data = nullptr;

  if (storeLoad(nm, &blob, &blen) &&
      blobUnpack(blob, blen, "GTI1", &w, &h, &payload, &len)) {
    data = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data) memcpy(data, payload, len);
    heap_caps_free(blob);
    if (data)
      Serial.printf("[Img] viewer: from disk %s %dx%d\\n", nm, w, h);
  } else {
    if (blob) heap_caps_free(blob);
    Serial.printf("[Img] viewer: not cached, refetch %.72s\\n", node->img_src);
    int rc = arduino_download_binary(node->img_src, &data, &len,
                                     IMG_MAX_BYTES, nullptr);
    if (rc != 0 || !data || len == 0) {
      if (data) heap_caps_free(data);
      return nullptr;
    }
    if (!tb_image_peek_size(data, len, &w, &h) || w < 16 || h < 16) {
      heap_caps_free(data);
      return nullptr;
    }
    size_t bl2 = 0;
    uint8_t* b2 = blobPack("GTI1", w, h, data, len, &bl2);
    if (b2) { storeSave(nm, b2, bl2); heap_caps_free(b2); }
  }
  if (!data) return nullptr;

  void* d = tb_image_dsc_create(data, len, w, h, IMG_MAX_W, IMG_MAX_PIXELS,
                                nullptr);
  if (!d) heap_caps_free(data);
  return d;
}

static void openImageViewer(LayoutNode* node) {
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
s = rep(s, old, new, 'viewer')
print('4) viewer ok')

# ══════════════════ 5) 另存：从盘读 ══════════════════
old = """static void downloadImage(LayoutNode* node) {
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

  char msg[96];"""
new = """static void downloadImage(LayoutNode* node) {
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

  char msg[96];"""
s = rep(s, old, new, 'download')
print('5) download ok')

w(B, s)

# ══════════════════ 6) 引擎：别把"已释放原图"当成"待下载" ══════════════════
s = io.open(L, encoding='utf-8').read()
old = """    if (c->type == ELEMENT_IMAGE && c->img_src && !c->img_dsc) {
      bool tracking_pixel = (c->img_w > 0 && c->img_w <= 4 &&"""
new = """    /* ⚠️ img_dsc 为空**不等于**没下载过：原图落盘后内存那份会被释放
       （见 browser_screen.cpp 的 buildThumbnails），此时 img_thumb 还在。
       只看 img_dsc 会把这批节点重新判成"待下载"，翻段时又去抓一遍。 */
    if (c->type == ELEMENT_IMAGE && c->img_src && !c->img_dsc && !c->img_thumb) {
      bool tracking_pixel = (c->img_w > 0 && c->img_w <= 4 &&"""
s = rep(s, old, new, 'engine-collect')

old = """    if (c->type == ELEMENT_IMAGE && c->img_src && !c->img_dsc) {
      void *d = m(c->img_src, ctx);"""
new = """    if (c->type == ELEMENT_IMAGE && c->img_src && !c->img_dsc && !c->img_thumb) {
      void *d = m(c->img_src, ctx);"""
s = rep(s, old, new, 'engine-assign')
w(L, s)
print('6) engine ok')
print('ALL PATCHED')
