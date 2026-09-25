# -*- coding: utf-8 -*-
"""缩略图 v4：图片下载提前到**解析之前**。

v3 的致命时序：
    下载 HTML → 解析 + 建布局树（内部 DRAM 被吃到只剩几百字节）
    → 按布局树里的 URL 去下图片 → DNS 直接失败：
      `[E][WiFiGeneric.cpp:1583] hostByName(): DNS Failed for static.ws.126.net`
      每张图白等 7 秒、rc=-1，一张都下不来，还把页面加载拖到预算上限。

v4 改成：
    下载 HTML → **扫原始 HTML 抓图**（此时 DRAM 还有几十 KB）
    → 解析 + 建布局树 → 按 URL 把提前抓好的图认领回节点。
    解析完之后就不需要再联网了。
"""
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
# 1) 引擎：layout_assign_images（把提前抓好的图按 URL 认领回节点）
# ══════════════════════════════════════════════════════════════════════
p, t = load('src/browser_engine/include/layout_engine.h')
t = sub(p, t,
"""/* 把树里所有 ELEMENT_IMAGE 节点打到串口（串口 `imgscan`）。诊断用。 */
void layout_dump_images(LayoutNode *root);""",
"""/* 把树里所有 ELEMENT_IMAGE 节点打到串口（串口 `imgscan`）。诊断用。 */
void layout_dump_images(LayoutNode *root);

/* 认领钩子：给定节点的图片地址，返回要挂上去的 dsc（NULL = 不要）。
   为什么要绕这一道：图片必须在**解析之前**就抓下来（解析会把内部 DRAM 吃光，
   之后 DNS 直接失败），那时候还没有布局树、也就没有"节点"可以挂。
   所以先扫 HTML 抓图，树建好后再按 URL 认领回来。 */
typedef void *(*LayoutImageMatcher)(const char *src, void *ctx);
int layout_assign_images(LayoutNode *root, LayoutImageMatcher matcher,
                         void *ctx);""")
save(p, t)

p, t = load('src/browser_engine/src/layout_engine.cpp')
t = sub(p, t,
"""void layout_dump_images(LayoutNode *root) {""",
"""static void assign_images_rec(LayoutNode *node, LayoutImageMatcher m, void *ctx,
                              int *n, int depth) {
  if (!node || depth > MAX_LAYOUT_DEPTH) return;
  for (LayoutNode *c = node; c; c = c->next_sibling) {
    if (c->type == ELEMENT_IMAGE && c->img_src && !c->img_dsc) {
      void *d = m(c->img_src, ctx);
      if (d) {
        c->img_dsc = d;
        (*n)++;
      }
    }
    if (c->first_child)
      assign_images_rec(c->first_child, m, ctx, n, depth + 1);
  }
}

int layout_assign_images(LayoutNode *root, LayoutImageMatcher m, void *ctx) {
  int n = 0;
  if (!root || !m) return 0;
  assign_images_rec(root, m, ctx, &n, 0);
  return n;
}

void layout_dump_images(LayoutNode *root) {""")
save(p, t)

# ══════════════════════════════════════════════════════════════════════
# 2) lvgl_renderer：tb_image_dsc_discard（后台任务里安全释放没画过的 dsc）
# ══════════════════════════════════════════════════════════════════════
p, t = load('src/browser_engine/include/lvgl_renderer.h')
t = sub(p, t,
"""/* 释放 dsc **和它持有的 data**。必须在 UI 线程调（内部碰 LVGL 图片缓存）。 */
void tb_image_dsc_free(void *dsc);""",
"""/* 释放 dsc **和它持有的 data**。必须在 UI 线程调（内部碰 LVGL 图片缓存）。 */
void tb_image_dsc_free(void *dsc);

/* 释放一个**从来没画过**的 dsc：不碰 LVGL 图片缓存，所以后台任务里也能调。
   给"提前抓下来但最后没被任何节点认领"的图收尸用。 */
void tb_image_dsc_discard(void *dsc);""")
save(p, t)

p, t = load('src/browser_engine/src/lvgl_renderer.cpp')
t = sub(p, t,
"""void tb_image_dsc_free(void *dsc) {""",
"""void tb_image_dsc_discard(void *dsc) {
  if (!dsc) return;
  lv_img_dsc_t *d = (lv_img_dsc_t *)dsc;
  if (d->data) heap_caps_free((void *)d->data);
  heap_caps_free(d);
}

void tb_image_dsc_free(void *dsc) {""")
save(p, t)

# ══════════════════════════════════════════════════════════════════════
# 3) browser_screen：扫描 + 提前下载 + 认领
# ══════════════════════════════════════════════════════════════════════
p, t = load('src/app/browser_screen.cpp')

t = sub(p, t,
""" * 现在的链路：
 *   1) dom_renderer 把 src / data-src / data-original 解析成绝对地址写进 img_src
 *      （DOM 阶段**绝不联网**，否则一个页面几十张图 = 几十次 TLS 握手）；
 *   2) 后台 fetch 任务解析完布局树之后，挑最多 IMG_MAX 张小图下载，
 *      每张有字节上限；
 *   3) 下载完先用文件头看宽高再挡一道 —— tjpgd 在 decoder_open 里一次性分配
 *      w*h*3 的 RGB888 缓冲，一张大图就能把 PSRAM 吃穿；
 *   4) 过了闸的才包成 lv_img_dsc_t 填进 img_dsc，渲染时变成一块瓦片（lv_img）。
 *
 * 门槛全部集中在下面四个宏，改一个数就能调松紧。
 * 没有 img_dsc 的图片节点**一块瓦片都不占**，所以"图下不下来"不会撑变形，
 * 也不会吃掉 MAX_WIDGETS 的配额。 */""",
""" * 现在的链路（⚠️ 顺序是踩出来的，别调）：
 *   1) 后台任务下完 HTML、**还没解析**时，先在原始 HTML 里扫出 <img> 地址；
 *   2) **趁 DRAM 还富余**把图抓下来（最多 IMG_MAX 张，单张有字节上限）；
 *      —— 解析 + 建布局树会把内部 DRAM 吃到只剩几百字节，那时候 DNS 直接
 *         失败（`hostByName(): DNS Failed`），图片一张都下不来；
 *   3) 解析 + 建布局树（dom_renderer 只记 img_src，DOM 阶段绝不联网）；
 *   4) 树建好后按 URL 把提前抓好的图认领回节点；
 *   5) 渲染前在 UI 线程把原图重采样成小缩略图（img_thumb），点开再看大图。
 *
 * 门槛集中在下面几个宏。没有 img_thumb 的图片节点**一块瓦片都不占**，
 * 所以"图下不下来"不会撑变形，也不会吃掉 MAX_WIDGETS 的配额。 */""")

t = sub(p, t,
"""/* 跑在后台 fetch 任务里（Core 0）：只下载、只填 img_dsc，**不碰 LVGL** */
static void fetchPageImages(const String& pageUrl) {
  if (!g_layoutRoot) return;
  LayoutNode* imgs[IMG_MAX];
  int n = layout_collect_images(g_layoutRoot, imgs, IMG_MAX);
  if (n <= 0) return;
  Serial.printf("[Img] %d candidate(s)\\n", n);

  uint32_t tAll = millis();
  int ok = 0;
  for (int i = 0; i < n; i++) {
    if (g_stopRequested) break;      /* 用户点了停止 → 立刻收手 */
    uint32_t spent = millis() - tAll;
    if (spent > IMG_TOTAL_MS) {
      Serial.printf("[Img] budget out (%ums), %d left\\n", (unsigned)spent,
                    n - i);
      break;
    }
    uint8_t* data = nullptr;
    size_t len = 0;
    uint32_t t0 = millis();
    Serial.printf("[Img] get %d/%d %.72s\\n", i + 1, n, imgs[i]->img_src);
    int rc = arduino_download_binary(imgs[i]->img_src, &data, &len,
                                     IMG_MAX_BYTES, pageUrl.c_str());
    Serial.printf("[Img] got rc=%d %u B %ums\\n", rc, (unsigned)len,
                  (unsigned)(millis() - t0));
    if (rc != 0 || !data || len == 0) {
      Serial.printf("[Img] miss rc=%d %.64s\\n", rc, imgs[i]->img_src);
      continue;
    }
    int w = 0, h = 0;
    if (!tb_image_peek_size(data, len, &w, &h)) {
      Serial.printf("[Img] drop unknown fmt %u B %.48s\\n",
                    (unsigned)len, imgs[i]->img_src);
      heap_caps_free(data);
      continue;
    }
    if (w < 16 || h < 16) {
      Serial.printf("[Img] drop tiny %dx%d\\n", w, h);
      heap_caps_free(data);
      continue;
    }
    int scale = 0;
    void* dsc = tb_image_dsc_create(data, len, w, h, IMG_MAX_W,
                                    IMG_MAX_PIXELS, &scale);
    if (!dsc) {
      Serial.printf("[Img] drop too big %dx%d (%u B)\\n", w, h, (unsigned)len);
      heap_caps_free(data);
      continue;
    }
    imgs[i]->img_dsc = dsc;
    ok++;
    Serial.printf("[Img] ok %dx%d -> %dx%d (1/%d) %u B  %.48s\\n", w, h,
                  w >> scale, h >> scale, 1 << scale, (unsigned)len,
                  imgs[i]->img_src);
  }
  Serial.printf("[Img] ready %d/%d in %ums, PSRAM free=%u\\n", ok, n,
                (unsigned)(millis() - tAll),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}""",
"""/* ── 提前抓下来的图：URL → dsc。解析完后按 URL 认领回节点 ── */
static String g_preUrl[IMG_MAX];
static void* g_preDsc[IMG_MAX];
static bool g_preUsed[IMG_MAX];
static int g_preN = 0;

static void clearPreload() {
  for (int i = 0; i < IMG_MAX; i++) {
    g_preUrl[i] = "";
    g_preDsc[i] = nullptr;
    g_preUsed[i] = false;
  }
  g_preN = 0;
}

/* 没人认领的图收尸（扫到的 URL 在布局树里可能压根没有对应节点）。 */
static void freeUnusedPreload() {
  int freed = 0;
  for (int i = 0; i < IMG_MAX; i++) {
    if (g_preDsc[i] && !g_preUsed[i]) {
      tb_image_dsc_discard(g_preDsc[i]);
      g_preDsc[i] = nullptr;
      freed++;
    }
  }
  if (freed) Serial.printf("[Img] discarded %d unmatched\\n", freed);
}

static bool attrDelim(char c) {
  return c == ' ' || c == '\\t' || c == '\\n' || c == '\\r' || c == '/' ||
         c == '"' || c == '\\'';
}

/* 读 p 处形如 attr = "值" 的值，写进 out/outLen。
   ⚠️ 调用方必须保证 p 前面是分隔符，否则 `data-src` 里的 `src` 也会被当成 src。 */
static bool attrValueAt(const char* p, size_t remain, const char* attr,
                        const char** out, size_t* outLen) {
  size_t alen = strlen(attr);
  if (remain < alen + 3) return false;
  if (memcmp(p, attr, alen) != 0) return false;
  size_t i = alen;
  while (i < remain && (p[i] == ' ' || p[i] == '\\t')) i++;
  if (i >= remain || p[i] != '=') return false;
  i++;
  while (i < remain && (p[i] == ' ' || p[i] == '\\t')) i++;
  if (i >= remain) return false;
  char q = p[i];
  if (q != '"' && q != '\\'') return false;
  i++;
  size_t s = i;
  while (i < remain && p[i] != q && (i - s) < 400) i++;
  if (i >= remain || p[i] != q) return false;
  *out = p + s;
  *outLen = i - s;
  return true;
}

/* 在原始 HTML 里扫 <img> 的地址（绝对化后存进 g_preUrl），返回个数。
   属性优先级跟 dom_renderer 保持一致：src > data-src > data-original，
   否则同一张图两边取到不同 URL，认领时对不上。 */
static int scanImageUrls(const char* html, size_t len, const String& base) {
  static const char* KEYS[3] = {"src", "data-src", "data-original"};
  int n = 0;
  for (size_t i = 0; i + 6 < len && n < IMG_MAX; i++) {
    if (html[i] != '<') continue;
    if (!(html[i + 1] == 'i' && html[i + 2] == 'm' && html[i + 3] == 'g'))
      continue;
    char c4 = html[i + 4];
    if (!(c4 == ' ' || c4 == '\\t' || c4 == '\\n' || c4 == '\\r' || c4 == '/' ||
          c4 == '>'))
      continue;

    size_t j = i + 4;
    size_t end = j;
    while (end < len && html[end] != '>') end++;
    if (end - j > 3000) {          /* 不像正常标签（半个 script 之类），跳过 */
      i = end;
      continue;
    }

    const char* v = nullptr;
    size_t vlen = 0;
    bool got = false;
    for (int t = 0; t < 3 && !got; t++) {
      for (size_t k = j; k < end && !got; k++) {
        if (k > j && !attrDelim(html[k - 1])) continue;
        got = attrValueAt(html + k, end - k, KEYS[t], &v, &vlen);
      }
    }
    i = end;
    if (!got || vlen < 12) continue;
    if (vlen >= 5 && memcmp(v, "data:", 5) == 0) continue;

    char* raw = (char*)malloc(vlen + 1);
    if (!raw) continue;
    memcpy(raw, v, vlen);
    raw[vlen] = '\\0';
    char* abs = tactilebrowser_resolve_url(base.c_str(), raw);
    free(raw);
    if (!abs) continue;
    bool dup = false;
    for (int d = 0; d < n; d++) {
      if (g_preUrl[d] == abs) { dup = true; break; }
    }
    if (!dup) {
      g_preUrl[n] = abs;
      n++;
    }
    free(abs);
  }
  return n;
}

/* 跑在后台 fetch 任务里（Core 0）：只下载、只填 g_pre*，**不碰 LVGL**。
   ⚠️⚠️ 必须在解析之前调（见文件头注释）。 */
static void fetchPageImages(const char* html, size_t htmlLen, const String& base) {
  clearPreload();
  if (!html || htmlLen < 64) return;

  int n = scanImageUrls(html, htmlLen, base);
  if (n <= 0) {
    Serial.println("[Img] no <img> found in raw HTML");
    return;
  }
  g_preN = n;
  Serial.printf("[Img] %d candidate(s) from raw HTML, DRAM free=%u\\n", n,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  uint32_t tAll = millis();
  int ok = 0;
  for (int i = 0; i < n; i++) {
    if (g_stopRequested) break;      /* 用户点了停止 → 立刻收手 */
    uint32_t spent = millis() - tAll;
    if (spent > IMG_TOTAL_MS) {
      Serial.printf("[Img] budget out (%ums), %d left\\n", (unsigned)spent,
                    n - i);
      break;
    }
    uint8_t* data = nullptr;
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
    int scale = 0;
    void* dsc = tb_image_dsc_create(data, len, w, h, IMG_MAX_W,
                                    IMG_MAX_PIXELS, &scale);
    if (!dsc) {
      Serial.printf("[Img] drop too big %dx%d (%u B)\\n", w, h, (unsigned)len);
      heap_caps_free(data);
      continue;
    }
    g_preDsc[i] = dsc;
    ok++;
    Serial.printf("[Img] ok %dx%d -> %dx%d (1/%d) %u B\\n", w, h, w >> scale,
                  h >> scale, 1 << scale, (unsigned)len);
  }
  Serial.printf("[Img] preloaded %d/%d in %ums, DRAM free=%u\\n", ok, n,
                (unsigned)(millis() - tAll),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

/* 解析完之后，把提前抓好的图按 URL 认领回布局节点。 */
static void* matchPreloaded(const char* src, void* ctx) {
  (void)ctx;
  if (!src) return nullptr;
  for (int i = 0; i < g_preN; i++) {
    if (!g_preDsc[i] || g_preUsed[i]) continue;
    if (g_preUrl[i] == src) {
      g_preUsed[i] = true;
      return g_preDsc[i];
    }
  }
  return nullptr;
}""")

# ── fetch_task：改成"先下 HTML → 抓图 → 再解析 → 认领" ──
t = sub(p, t,
"""    /* 用宽视口排版（max_height 在布局阶段未使用，传 0 即可） */
    int ci = pageCacheFind(url);
    if (ci < 0) {
      /* PSRAM 里没有 → 退到 SD 卡那份（跨会话/重启仍然有效）。
         读回来塞进 PSRAM 缓存，下面走的就是同一条"缓存命中"路径。 */
      String html;
      if (sdPageLoad(url, html)) {
        pageCachePut(url, (const uint8_t*)html.c_str(), html.length());
        ci = pageCacheFind(url);
        if (ci >= 0)
          Serial.printf("[Browser] SD cache hit: %s (%u B)\\n", url.c_str(),
                        (unsigned)g_pageCache[ci].len);
      }
    }
    if (ci >= 0) {
      Serial.printf("[Browser] cache hit: %s (%u B, age %us)\\n",
                    url.c_str(), (unsigned)g_pageCache[ci].len,
                    (unsigned)((millis() - g_pageCache[ci].ts) / 1000));
      g_taskResult = tactilebrowser_parse_html_buffer(
          url.c_str(), (const char*)g_pageCache[ci].data, g_pageCache[ci].len,
          g_browserViewportW, 0, &g_stopRequested, &g_layoutRoot);
    } else {
      g_taskResult = tactilebrowser_download_and_parse(
          url.c_str(), g_browserViewportW, 0, &g_stopRequested, &g_layoutRoot);
    }

    /* 缩略图：布局树有了才谈得上挑图。放在这里而不是 DOM 阶段，是为了让
       "页面文字先出来"和"图片慢慢下"分开 —— 图下不到也只是没有图，不影响正文。 */
    if (g_taskResult == RENDER_SUCCESS && g_layoutRoot && !g_stopRequested &&
        g_imgEnabled) {
      fetchPageImages(url);
    }
""",
"""    /* 用宽视口排版（max_height 在布局阶段未使用，传 0 即可） */
    const char* htmlData = nullptr;
    size_t htmlLen = 0;
    MemoryBuffer fresh;
    fresh.data = nullptr;
    fresh.size = 0;

    int ci = pageCacheFind(url);
    if (ci < 0) {
      /* PSRAM 里没有 → 退到 SD 卡那份（跨会话/重启仍然有效）。
         读回来塞进 PSRAM 缓存，下面走的就是同一条"缓存命中"路径。 */
      String html;
      if (sdPageLoad(url, html)) {
        pageCachePut(url, (const uint8_t*)html.c_str(), html.length());
        ci = pageCacheFind(url);
        if (ci >= 0)
          Serial.printf("[Browser] SD cache hit: %s (%u B)\\n", url.c_str(),
                        (unsigned)g_pageCache[ci].len);
      }
    }
    if (ci >= 0) {
      Serial.printf("[Browser] cache hit: %s (%u B, age %us)\\n",
                    url.c_str(), (unsigned)g_pageCache[ci].len,
                    (unsigned)((millis() - g_pageCache[ci].ts) / 1000));
      htmlData = (const char*)g_pageCache[ci].data;
      htmlLen = g_pageCache[ci].len;
    } else {
      /* 自己下、自己解析（不再走 tactilebrowser_download_and_parse）——
         为的就是拿到**原始 HTML**，好在解析之前先把图抓下来。 */
      g_taskResult = cache_download_html(url.c_str(), &fresh);
      Serial.printf("[Browser] download rc=%d size=%u DRAM free=%u\\n",
                    (int)g_taskResult, (unsigned)fresh.size,
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
      if (g_taskResult == RENDER_SUCCESS && fresh.data) {
        htmlData = fresh.data;
        htmlLen = fresh.size;
      }
    }

    /* ⚠️⚠️ 顺序不能换：图片必须在**解析之前**抓完。
       解析 + 建布局树会把内部 DRAM 吃到只剩几百字节，之后 DNS 一律失败
       （实测 `hostByName(): DNS Failed`），每张图白等 7 秒、rc=-1。 */
    if (htmlData && g_imgEnabled && !g_stopRequested) {
      fetchPageImages(htmlData, htmlLen, url);
    }

    if (htmlData) {
      g_taskResult = tactilebrowser_parse_html_buffer(
          url.c_str(), htmlData, htmlLen, g_browserViewportW, 0,
          &g_stopRequested, &g_layoutRoot);
    }
    if (fresh.data) {
      free(fresh.data);
      fresh.data = nullptr;
    }

    if (g_taskResult == RENDER_SUCCESS && g_layoutRoot) {
      int got = layout_assign_images(g_layoutRoot, matchPreloaded, nullptr);
      Serial.printf("[Img] attached %d/%d to layout\\n", got, g_preN);
    }
    freeUnusedPreload();
""")
save(p, t)

print('FAILURES: %d' % len(fail))
for f in fail:
    print(' -', f)
sys.exit(1 if fail else 0)
