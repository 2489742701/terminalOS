# -*- coding: utf-8 -*-
"""缩略图链路 —— 引擎侧补丁（2026-09-25）

统一用脚本改源码，不用 Edit：Edit 在这批 CRLF 文件上假成功过 4 次。
所有替换都按文件自身的换行风格归一化后再做。
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


def sub(p, text, old, new, once=True):
    nl = '\r\n' if '\r\n' in text else '\n'
    o = old.replace('\n', nl)
    n = new.replace('\n', nl)
    cnt = text.count(o)
    if cnt != 1:
        fail.append('%s: pattern count=%d :: %s' % (p, cnt, old.split('\n')[0][:60]))
        return text
    return text.replace(o, n, 1)


# ────────────────────────────────────────────────────────────────────
# 1) common_types.h —— RenderInterface 加一个 create_image
# ────────────────────────────────────────────────────────────────────
p, t = load('src/browser_engine/include/common_types.h')
t = sub(p, t,
"""  void *(*create_chip)(Renderer *renderer, const char *text, int max_width,
                       uint32_t color);
""",
"""  void *(*create_chip)(Renderer *renderer, const char *text, int max_width,
                       uint32_t color);
  /* 图片：img_dsc 是平台侧的图片描述符（LVGL 里是 lv_img_dsc_t*）。
     由后台任务下载并校验通过后才填进布局节点，渲染时变成一块瓦片。
     max_w = 内容区宽度；超过的图由平台侧决定缩还是丢。 */
  void *(*create_image)(Renderer *renderer, void *img_dsc, int max_w);
""")
save(p, t)

# ────────────────────────────────────────────────────────────────────
# 2) layout_engine.h —— LayoutNode 加图片字段 + 收集函数
# ────────────────────────────────────────────────────────────────────
p, t = load('src/browser_engine/include/layout_engine.h')
t = sub(p, t,
"""  char *form_value;  // For inputs/textarea
  char *placeholder; // For inputs
""",
"""  char *form_value;  // For inputs/textarea
  char *placeholder; // For inputs

  /* ── 图片（缩略图）2026-09-25 ── */
  char *img_src;  /* <img src> 的绝对地址（下载前就填好，下载后保留便于诊断） */
  int   img_w;    /* HTML width 属性，0=没写 */
  int   img_h;    /* HTML height 属性，0=没写 */
  void *img_dsc;  /* 平台侧图片描述符（LVGL: lv_img_dsc_t*）；NULL = 不显示 */
""")
t = sub(p, t,
"""void layout_forget_prepare(void);  /* 布局树释放时调用 */
""",
"""void layout_forget_prepare(void);  /* 布局树释放时调用 */

/* 收集整棵树里"值得下载"的图片节点，写进 out，最多 max 个，返回个数。
   跳过：没有 img_src、已经下载过（img_dsc 非空）、HTML 里写明是 1x1~4x4
   的那种追踪像素。
   ⚠️ 只用来**挑**，不联网 —— 联网在后台 fetch 任务里做。 */
int layout_collect_images(LayoutNode *root, LayoutNode **out, int max);
""")
save(p, t)

# ────────────────────────────────────────────────────────────────────
# 3) lvgl_renderer.h —— 声明
# ────────────────────────────────────────────────────────────────────
p, t = load('src/browser_engine/include/lvgl_renderer.h')
t = sub(p, t,
"""RenderResult arduino_download_html(const char *url, MemoryBuffer *buffer);
""",
"""RenderResult arduino_download_html(const char *url, MemoryBuffer *buffer);

/* ── 图片（缩略图）2026-09-25 ────────────────────────────────────────────
   <img> 以前只被映射成 ELEMENT_IMAGE，渲染阶段没人认识 —— 页面上一张图都没有。
   链路：dom_renderer 记绝对地址 → 后台任务挑小图下载 → 过尺寸闸 → 填 img_dsc
   → 渲染成 lv_img。下面四个函数就是这条链上的工具。 */

/* 二进制 GET。maxBytes 是硬上限，超过直接放弃（缩略图不需要原图）。
   referer 可空 —— 不少 CDN 防盗链，没 Referer 直接 403。
   返回 0=成功（*out 由调用方 heap_caps_free），-1=网络，-2=内存，-3=太大/非 200。 */
int arduino_download_binary(const char *url, uint8_t **out, size_t *outLen,
                            size_t maxBytes, const char *referer);

/* 只读文件头拿宽高，**不解码、不碰 LVGL**（后台任务里不能用 lv_img_decoder_*：
   LVGL 非线程安全）。支持的格式返回 true，不认识（webp/avif/svg…）返回 false。 */
bool tb_image_peek_size(const uint8_t *data, size_t len, int *w, int *h);

/* 接管 data 的所有权，包一个 lv_img_dsc_t。失败返回 NULL 且不接管 data。 */
void *tb_image_dsc_create(uint8_t *data, size_t len);
/* 释放 dsc **和它持有的 data**。必须在 UI 线程调（内部碰 LVGL 图片缓存）。 */
void tb_image_dsc_free(void *dsc);
""")
save(p, t)

# ────────────────────────────────────────────────────────────────────
# 4) lvgl_renderer.cpp —— 实现 + 注册
# ────────────────────────────────────────────────────────────────────
p, t = load('src/browser_engine/src/lvgl_renderer.cpp')
IMG_CODE = r'''
/* ═══════════════════════════════════════════════════════════════════════════
 * 图片（缩略图）2026-09-25
 *
 * 为什么要有字节/尺寸上限：LVGL 的 SJPG 解码器（lv_sjpg.c，底层是 tjpgd）在
 * decoder_open 里**一次性**分配 w*h*3 的 RGB888 中间缓冲，再按行转成 RGB565。
 * 也就是说一张 1920x1080 的 JPEG 会直接吃掉 6MB —— 不设闸，PSRAM 瞬间见底。
 * 所以：下载有字节上限，下载完先用 tb_image_peek_size 看宽高，超预算直接丢，
 * 通过的才包成 lv_img_dsc_t 交给渲染层。
 *
 * ⚠️ 这些函数跑在后台 fetch 任务（Core 0）里，**一律不许碰 LVGL**。
 *    只有 tb_image_dsc_free 例外（它要失效图片缓存），必须在 UI 线程调。
 * ═══════════════════════════════════════════════════════════════════════ */

/* 二进制下载（与 arduino_download_html 同构，但更短、有上限、带 Referer） */
static int download_binary_inner(const char *url, uint8_t **out, size_t *outLen,
                                 size_t maxBytes, const char *referer,
                                 int depth) {
  if (!url || !out || !outLen || maxBytes == 0) return -1;
  if (depth > 3) return -1;

  String urlStr(url);
  bool isHttps = urlStr.startsWith("https://");
  int protoEnd = urlStr.indexOf("://");
  if (protoEnd < 0) return -1;
  String rest = urlStr.substring(protoEnd + 3);
  int slashPos = rest.indexOf('/');
  String host = slashPos < 0 ? rest : rest.substring(0, slashPos);
  String path = slashPos < 0 ? "/" : rest.substring(slashPos);
  int port = isHttps ? 443 : 80;
  int colonPos = host.indexOf(':');
  if (colonPos >= 0) {
    port = host.substring(colonPos + 1).toInt();
    host = host.substring(0, colonPos);
  }

  WiFiClient *client = nullptr;
  WiFiClient tcpClient;
  WiFiClientSecure sslClient;
  if (isHttps) {
    init_tls_psram();
    sslClient.setInsecure();
    sslClient.setTimeout(8);
    if (!sslClient.connect(host.c_str(), port)) return -1;
    client = &sslClient;
  } else {
    tcpClient.setTimeout(8);
    if (!tcpClient.connect(host.c_str(), port)) return -1;
    client = &tcpClient;
  }

  String req = "GET " + path + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 "
               "Build/KOT49H) AppleWebKit/537.36 (KHTML, like Gecko) "
               "Chrome/30.0.0.0 Mobile Safari/537.36\r\n" +
               "Accept: image/jpeg,image/png,image/*,*/*;q=0.8\r\n" +
               "Accept-Language: zh-CN,zh;q=0.9\r\n" +
               (referer && referer[0] ? (String("Referer: ") + referer + "\r\n")
                                      : String("")) +
               "Connection: close\r\n\r\n";
  client->print(req);

  String line;
  int status = 0;
  int contentLen = 0;
  bool chunked = false;
  String redirectTo = "";
  uint32_t t0 = millis();
  while (client->connected() || client->available()) {
    if (millis() - t0 > 8000) { client->stop(); return -1; }
    line = client->readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;
    if (status == 0 && line.startsWith("HTTP/")) {
      status = line.substring(9, 12).toInt();
    } else if (line.startsWith("Content-Length:")) {
      contentLen = line.substring(15).toInt();
    } else if (line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
      chunked = true;
    } else if (line.length() > 9 &&
               strncasecmp(line.c_str(), "location:", 9) == 0) {
      redirectTo = line.substring(9);
      redirectTo.trim();
    }
  }

  if (status >= 300 && status <= 399 && redirectTo.length() > 0) {
    client->stop();
    String nextUrl = resolve_redirect(urlStr, redirectTo);
    if (nextUrl.length() == 0) return -1;
    return download_binary_inner(nextUrl.c_str(), out, outLen, maxBytes,
                                 referer, depth + 1);
  }
  if (status != 200) { client->stop(); return -3; }
  if (contentLen > (int)maxBytes) { client->stop(); return -3; }
  if (contentLen <= 0) contentLen = (int)maxBytes;

  uint8_t *buf = (uint8_t *)heap_caps_malloc(contentLen + 8,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) { client->stop(); return -2; }

  int total = 0;
  t0 = millis();
  if (chunked) {
    while (client->connected() || client->available()) {
      if (millis() - t0 > 10000) break;
      String sl = client->readStringUntil('\n');
      sl.trim();
      int cs = strtol(sl.c_str(), NULL, 16);
      if (cs <= 0) break;
      int room = contentLen - total;
      if (room <= 0) break;
      int want = cs > room ? room : cs;
      int got = 0;
      while (got < want && (client->connected() || client->available())) {
        int r = client->read(buf + total, want - got);
        if (r > 0) { got += r; total += r; }
        else delay(1);
      }
      client->readStringUntil('\n');
      if (got < cs) break;
      vTaskDelay(1);
    }
  } else {
    while (total < contentLen && (client->connected() || client->available())) {
      if (millis() - t0 > 10000) break;
      int r = client->read(buf + total, contentLen - total);
      if (r > 0) { total += r; if (total % 4096 < 64) vTaskDelay(1); }
      else delay(1);
    }
  }
  client->stop();

  if (total <= 0) { heap_caps_free(buf); return -1; }
  *out = buf;
  *outLen = (size_t)total;
  return 0;
}

int arduino_download_binary(const char *url, uint8_t **out, size_t *outLen,
                            size_t maxBytes, const char *referer) {
  return download_binary_inner(url, out, outLen, maxBytes, referer, 0);
}

bool tb_image_peek_size(const uint8_t *data, size_t len, int *w, int *h) {
  if (!data || !w || !h) return false;
  *w = 0;
  *h = 0;
  /* JPEG：从 SOI 开始顺着段链找 SOFn（FFC0~FFCF，排除 DHT/C4、JPG/C8、DAC/CC） */
  if (len > 24 && data[0] == 0xFF && data[1] == 0xD8) {
    size_t i = 2;
    while (i + 9 < len) {
      if (data[i] != 0xFF) { i++; continue; }
      uint8_t m = data[i + 1];
      if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7) || m == 0xFF) {
        i += 2;
        continue;
      }
      if (m == 0xC0 || m == 0xC1 || m == 0xC2 || m == 0xC3 || m == 0xC5 ||
          m == 0xC6 || m == 0xC7 || m == 0xC9 || m == 0xCA || m == 0xCB ||
          m == 0xCD || m == 0xCE || m == 0xCF) {
        *h = (data[i + 5] << 8) | data[i + 6];
        *w = (data[i + 7] << 8) | data[i + 8];
        return (*w > 0 && *h > 0);
      }
      size_t seg = ((size_t)data[i + 2] << 8) | data[i + 3];
      if (seg < 2) return false;
      i += 2 + seg;
    }
    return false;
  }
  /* PNG：IHDR 里第 16~23 字节是大端宽高 */
  if (len > 24 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E &&
      data[3] == 0x47) {
    *w = (int)(((uint32_t)data[16] << 24) | ((uint32_t)data[17] << 16) |
               ((uint32_t)data[18] << 8) | (uint32_t)data[19]);
    *h = (int)(((uint32_t)data[20] << 24) | ((uint32_t)data[21] << 16) |
               ((uint32_t)data[22] << 8) | (uint32_t)data[23]);
    return (*w > 0 && *h > 0);
  }
  /* GIF：逻辑屏幕描述符，小端 */
  if (len > 14 && data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46) {
    *w = (int)(data[6] | (data[7] << 8));
    *h = (int)(data[8] | (data[9] << 8));
    return (*w > 0 && *h > 0);
  }
  return false;
}

void *tb_image_dsc_create(uint8_t *data, size_t len) {
  lv_img_dsc_t *d = (lv_img_dsc_t *)tb_alloc(sizeof(lv_img_dsc_t));
  if (!d) return NULL;
  memset(d, 0, sizeof(*d));
  d->header.always_zero = 0;
  d->header.cf = LV_IMG_CF_RAW;  /* 真正的格式交给 SJPG/PNG 解码器自己认 */
  d->data = data;
  d->data_size = len;
  return d;
}

void tb_image_dsc_free(void *dsc) {
  if (!dsc) return;
  lv_img_dsc_t *d = (lv_img_dsc_t *)dsc;
  /* LVGL 的图片缓存按 src 指针索引。不让它失效的话，缓存里那条还指着我们
     马上要 free 的 dsc —— 下次命中就是野指针，而且是延后很久才炸的那种。 */
  lv_img_cache_invalidate_src(d);
  if (d->data) heap_caps_free((void *)d->data);
  heap_caps_free(d);
}

'''
t = sub(p, t,
"""static bool lvgl_renderer_init(Renderer *renderer) {""",
IMG_CODE.lstrip('\n') + """static bool lvgl_renderer_init(Renderer *renderer) {""")

t = sub(p, t,
"""static void *lvgl_renderer_create_label(Renderer *renderer, const char *text,
                                         int x, int y) {""",
"""static void *lvgl_renderer_create_image(Renderer *renderer, void *img_dsc,
                                         int max_w) {
  if (!renderer || !img_dsc) return NULL;
  lv_obj_t *parent = (lv_obj_t *)renderer->platform_data;
  if (!parent) return NULL;

  lv_obj_t *img = lv_img_create(parent);
  lv_img_set_src(img, (const lv_img_dsc_t *)img_dsc);

  /* 比内容区还宽的图直接不画：LVGL 对 RAW 图是按行 read_line 画的，
     lv_img_set_zoom 在 RAW 上会把每一行各自缩放（画出来是错位的），
     所以这里不做缩放，超宽的图在上一道尺寸闸就被丢掉了。 */
  (void)max_w;
  return img;
}

static void *lvgl_renderer_create_label(Renderer *renderer, const char *text,
                                         int x, int y) {""")

t = sub(p, t,
"""  renderer->base.create_chip = lvgl_renderer_create_chip;""",
"""  renderer->base.create_chip = lvgl_renderer_create_chip;
  renderer->base.create_image = lvgl_renderer_create_image;""")
save(p, t)

# ────────────────────────────────────────────────────────────────────
# 5) layout_engine.cpp —— 销毁字段 / 图片瓦片 / 收集函数
# ────────────────────────────────────────────────────────────────────
p, t = load('src/browser_engine/src/layout_engine.cpp')
t = sub(p, t,
"""  free(node->form_value);
  free(node->placeholder);
  free(node);""",
"""  free(node->form_value);
  free(node->placeholder);
  free(node->img_src);
  if (node->img_dsc) tb_image_dsc_free(node->img_dsc);
  free(node);""")

t = sub(p, t,
"""  if ((node->type == ELEMENT_INPUT_TEXT || node->type == ELEMENT_TEXTAREA) &&
      iface->create_text_input && seg_take_tile(node)) {""",
"""  if (node->type == ELEMENT_IMAGE && node->img_dsc && seg_take_tile(node) &&
      iface->create_image) {
    /* 缩略图：字节在后台任务里已经下载+过闸，这里只管建控件。
       没有 img_dsc 的图片节点**一块瓦片都不占**（干跑和实跑都一样），
       所以"图下不下来"不会把版面撑变形，也不会吃掉 MAX_WIDGETS 配额。 */
    node->widget = iface->create_image(renderer, node->img_dsc,
                                       render_ctx->max_width);
    widget = node->widget;
    if (widget) {
      s_widgetCount++;
      s_rowContainer = NULL;   /* 图片是区块，胶囊行到此为止 */
      Serial.printf("[Img] tile %d\\n", s_widgetCount - 1);
    }
  } else if ((node->type == ELEMENT_INPUT_TEXT || node->type == ELEMENT_TEXTAREA) &&
      iface->create_text_input && seg_take_tile(node)) {""")

t = sub(p, t,
"""void layout_forget_prepare(void) { s_preparedRoot = NULL; s_tileTotal = 0; }""",
"""void layout_forget_prepare(void) { s_preparedRoot = NULL; s_tileTotal = 0; }

/* ── 图片候选收集 ──────────────────────────────────────────────────────────
 * 铁律照旧：兄弟用迭代、父子才递归。
 * 只挑"值得下载"的：有 img_src、还没下载过、而且不是 1x1 那种追踪像素
 * （HTML 里写了 width/height 且都 <= 4 的基本都是埋点/占位图）。 */
static void collect_images_rec(LayoutNode *node, LayoutNode **out, int max,
                               int *n, int depth) {
  if (!node || depth > MAX_LAYOUT_DEPTH) return;
  for (LayoutNode *c = node; c && *n < max; c = c->next_sibling) {
    if (c->type == ELEMENT_IMAGE && c->img_src && !c->img_dsc) {
      bool tracking_pixel = (c->img_w > 0 && c->img_w <= 4 &&
                             c->img_h > 0 && c->img_h <= 4);
      if (!tracking_pixel) out[(*n)++] = c;
    }
    if (c->first_child)
      collect_images_rec(c->first_child, out, max, n, depth + 1);
  }
}

int layout_collect_images(LayoutNode *root, LayoutNode **out, int max) {
  int n = 0;
  if (!root || !out || max <= 0) return 0;
  collect_images_rec(root, out, max, &n, 0);
  return n;
}""")
save(p, t)

# ────────────────────────────────────────────────────────────────────
# 6) dom_renderer.cpp —— <img> 取 src / width / height
# ────────────────────────────────────────────────────────────────────
p, t = load('src/browser_engine/src/dom_renderer.cpp')
t = sub(p, t,
"""static LayoutNode *build_layout_tree_from_dom(lxb_dom_node_t *dom_node,""",
"""/* <img> 的 width/height 属性是**不带结尾 0** 的一段字符，且常常写成 "120px"。
   只吃开头的数字，吃不到就当没写。 */
static int attr_int(const char *s, size_t n) {
  int v = 0;
  bool any = false;
  for (size_t i = 0; i < n && i < 8; i++) {
    if (s[i] >= '0' && s[i] <= '9') {
      v = v * 10 + (s[i] - '0');
      any = true;
    } else {
      break;
    }
  }
  return any ? v : 0;
}

static LayoutNode *build_layout_tree_from_dom(lxb_dom_node_t *dom_node,""")

t = sub(p, t,
"""  // Apply tag-level selectors
  if (tag && tag_len > 0) {""",
"""  /* ── <img>（缩略图）2026-09-25 ────────────────────────────────────────
     这里**只**记绝对地址和尺寸属性，绝不联网 —— DOM 阶段联网会把一次页面
     加载拖成分钟级（一个页面几十张图，每张一次 TLS 握手）。
     真正的下载在后台 fetch 任务里、按"最多几张 + 各自字节上限"来做。
     内联 data: URI 直接跳过（那玩意儿是 base64，解析它纯属浪费）。 */
  if (elem_type == ELEMENT_IMAGE) {
    size_t src_len = 0;
    const char *src_attr = html_parser.get_element_attr(element, "src", &src_len);
    if (!src_attr || src_len == 0) {
      src_attr = html_parser.get_element_attr(element, "data-src", &src_len);
    }
    /* 懒加载站点（知乎/微博）常把真地址放在 data-original / data-actual 里 */
    if (!src_attr || src_len == 0) {
      src_attr = html_parser.get_element_attr(element, "data-original", &src_len);
    }
    if (src_attr && src_len > 0 &&
        !(src_len >= 5 && strncmp(src_attr, "data:", 5) == 0)) {
      char *raw = safe_strndup(src_attr, src_len);
      if (raw) {
        char *abs_url = tactilebrowser_resolve_url(context->document_url, raw);
        free(raw);
        if (abs_url) layout_node->img_src = abs_url;
      }
    }
    size_t w_len = 0;
    const char *w_attr =
        html_parser.get_element_attr(element, "width", &w_len);
    if (w_attr && w_len > 0) layout_node->img_w = attr_int(w_attr, w_len);
    size_t h_len = 0;
    const char *h_attr =
        html_parser.get_element_attr(element, "height", &h_len);
    if (h_attr && h_len > 0) layout_node->img_h = attr_int(h_attr, h_len);
  }

  // Apply tag-level selectors
  if (tag && tag_len > 0) {""")
save(p, t)

print('FAILURES: %d' % len(fail))
for f in fail:
    print(' -', f)
sys.exit(1 if fail else 0)
