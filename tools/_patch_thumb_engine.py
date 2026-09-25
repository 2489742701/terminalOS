# -*- coding: utf-8 -*-
"""缩略图 v3 —— 引擎侧：区域平均重采样 + 缩略图瓦片 + 点击回调。

v2 的问题（v2 = "原尺寸塞进去，超宽的丢掉"）：
  · 网页图片动辄 1000+ 像素宽，全被 464 的门槛挡掉 → 页面上还是没图；
  · 侥幸塞下的那些按原尺寸铺，480 屏上一张就占掉半屏，版面全乱。
v3 改成：一律**重采样成小缩略图**（盒子滤波，缩到 96px 也能认出画的是啥），
  点一下开全屏大图，另存到卡。

为什么不用 lv_img_set_zoom：
  · JPEG 在 LVGL 里是 RAW + 逐行 read_line（img_data 为 NULL），LVGL 缩放时把
    **每一行**当整张图单独变换，画出来是错位的；
  · 而且 zoom 是最近邻，缩到 1/5 就是马赛克 —— 我们要的是"糊但看得清"。
  所以自己把像素抠出来做区域平均。
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


# ══ common_types.h：Iface 加 register_image_handler ══════════════════
p, t = load('src/browser_engine/include/common_types.h')
t = sub(p, t,
"""  /* 图片：img_dsc 是平台侧的图片描述符（LVGL 里是 lv_img_dsc_t*）。
     由后台任务下载并校验通过后才填进布局节点，渲染时变成一块瓦片。
     max_w = 内容区宽度；超过的图由平台侧决定缩还是丢。 */
  void *(*create_image)(Renderer *renderer, void *img_dsc, int max_w);
""",
"""  /* 图片缩略图：img_dsc 是平台侧的图片描述符（LVGL 里是 lv_img_dsc_t*），
     内容是**已经重采样好的小图**。渲染时变成一块瓦片。 */
  void *(*create_image)(Renderer *renderer, void *img_dsc, int max_w);
  /* 缩略图被点了 → 回调里带着布局节点指针回去（app 层拿它开全屏大图 /
     另存文件）。参数是 void* 而不是具体类型：引擎不该知道 app 怎么处理。 */
  void (*register_image_handler)(Renderer *renderer, void *widget, void *node);
""")
save(p, t)

# ══ layout_engine.h：img_thumb 字段 + 收集函数 ═══════════════════════
p, t = load('src/browser_engine/include/layout_engine.h')
t = sub(p, t,
"""  void *img_dsc;  /* 平台侧图片描述符（LVGL: lv_img_dsc_t*）；NULL = 不显示 */
""",
"""  void *img_dsc;    /* 下载到的**原始**字节（lv_img_dsc_t 包着 jpg/png 原文） */
  void *img_thumb;  /* 重采样后的小图 dsc，渲染用的就是它；NULL = 这块不占瓦片 */
""")
t = sub(p, t,
"""int layout_collect_images(LayoutNode *root, LayoutNode **out, int max);
""",
"""int layout_collect_images(LayoutNode *root, LayoutNode **out, int max);

/* 收集"已经拿到原始字节"的图片节点（img_dsc 非空），最多 max 个。
   渲染前用它批量做缩略图 —— 重采样要碰 LVGL 解码器，只能在 UI 线程做。 */
int layout_collect_ready_images(LayoutNode *root, LayoutNode **out, int max);
""")
save(p, t)

# ══ layout_engine.cpp ════════════════════════════════════════════════
p, t = load('src/browser_engine/src/layout_engine.cpp')
t = sub(p, t,
"""  free(node->img_src);
  if (node->img_dsc) tb_image_dsc_free(node->img_dsc);
  free(node);""",
"""  free(node->img_src);
  if (node->img_dsc) tb_image_dsc_free(node->img_dsc);
  if (node->img_thumb) tb_image_dsc_free(node->img_thumb);
  free(node);""")

t = sub(p, t,
"""int layout_collect_images(LayoutNode *root, LayoutNode **out, int max) {
  int n = 0;
  if (!root || !out || max <= 0) return 0;
  collect_images_rec(root, out, max, &n, 0);
  return n;
}""",
"""int layout_collect_images(LayoutNode *root, LayoutNode **out, int max) {
  int n = 0;
  if (!root || !out || max <= 0) return 0;
  collect_images_rec(root, out, max, &n, 0);
  return n;
}

/* 已经拿到原始字节的图片（等做缩略图）。判定只看 img_dsc。 */
static void collect_ready_rec(LayoutNode *node, LayoutNode **out, int max,
                              int *n, int depth) {
  if (!node || depth > MAX_LAYOUT_DEPTH) return;
  for (LayoutNode *c = node; c && *n < max; c = c->next_sibling) {
    if (c->type == ELEMENT_IMAGE && c->img_dsc) out[(*n)++] = c;
    if (c->first_child)
      collect_ready_rec(c->first_child, out, max, n, depth + 1);
  }
}

int layout_collect_ready_images(LayoutNode *root, LayoutNode **out, int max) {
  int n = 0;
  if (!root || !out || max <= 0) return 0;
  collect_ready_rec(root, out, max, &n, 0);
  return n;
}""")

t = sub(p, t,
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
  } else if""",
"""  if (node->type == ELEMENT_IMAGE && node->img_thumb && seg_take_tile(node) &&
      iface->create_image) {
    /* 缩略图瓦片。判定条件是 img_thumb（渲染用的小图）不是 img_dsc（原始字节）：
       没做出缩略图的图片节点**一块瓦片都不占**，干跑和实跑看到的状态一致，
       所以"图下不下来"既不会把版面撑变形，也不会吃掉 MAX_WIDGETS 配额。
       小图排进 row-wrap 行（跟胶囊同一套机制），一屏能并排好几个。 */
    if (!s_rowContainer) {
      void *saved = renderer->platform_data;
      renderer->platform_data = parent;
      s_rowContainer = iface->create_row_wrap(renderer, render_ctx->max_width);
      renderer->platform_data = saved;
      if (s_rowContainer) s_widgetCount++;
    }
    if (s_rowContainer) {
      void *saved = renderer->platform_data;
      renderer->platform_data = s_rowContainer;
      node->widget = iface->create_image(renderer, node->img_thumb,
                                         render_ctx->max_width);
      renderer->platform_data = saved;
      widget = node->widget;
      if (widget) {
        s_widgetCount++;
        if (iface->register_image_handler)
          iface->register_image_handler(renderer, widget, node);
      }
    }
  } else if""")
save(p, t)

# ══ lvgl_renderer.h ══════════════════════════════════════════════════
p, t = load('src/browser_engine/include/lvgl_renderer.h')
t = sub(p, t,
"""void lvgl_renderer_set_link_callback(LvglLinkCallback cb);
""",
"""void lvgl_renderer_set_link_callback(LvglLinkCallback cb);

/* 缩略图点击回传：参数就是那个布局节点（void* 是刻意的 —— 引擎不该知道
   app 层拿它去开全屏还是另存）。回调里只记指针，真开图放 tick 里做。 */
typedef void (*LvglImageCallback)(void *node);
void lvgl_renderer_set_image_callback(LvglImageCallback cb);
""")
t = sub(p, t,
"""/* 释放 dsc **和它持有的 data**。必须在 UI 线程调（内部碰 LVGL 图片缓存）。 */
void tb_image_dsc_free(void *dsc);
""",
"""/* 释放 dsc **和它持有的 data**。必须在 UI 线程调（内部碰 LVGL 图片缓存）。 */
void tb_image_dsc_free(void *dsc);

/* 把一张"原始字节 dsc"重采样成不超过 box_w × box_h 的小图（区域平均），
   返回新的 RGB565(A) dsc —— 缩略图和大图都是它做出来的，只是 box 不同。
   返回 NULL = 解不出来（渐进式 JPEG、超大图、格式不认识…）。
   ⚠️ 只能在 UI 线程调：里面要用 LVGL 的图片解码器。
   ⚠️ 内部会临时把 dsc.header.reserved 当成 JPEG 降采样档位（见
      tools/patch_lvgl_jpeg_scale.py），**不会**改动传进来的 dsc。 */
void *tb_image_resample(void *src_dsc, int box_w, int box_h,
                        int *out_w, int *out_h);
""")
save(p, t)

# ══ lvgl_renderer.cpp ════════════════════════════════════════════════
p, t = load('src/browser_engine/src/lvgl_renderer.cpp')

RESAMPLE = r'''
/* ═══════════════════════════════════════════════════════════════════════════
 * 缩略图 / 全屏大图：把原始 JPEG/PNG 字节重采样成指定尺寸的 RGB565 缓冲
 *
 * 为什么不用 lv_img_set_zoom（两条都踩过）：
 *   1) JPEG 在 LVGL 里走的是 RAW + 逐行 read_line（decoder_open 故意把
 *      img_data 留成 NULL）。LVGL 缩放时会拿**每一行**当整张图单独变换，
 *      画出来是上下错位的；
 *   2) zoom 本身是最近邻，缩到 1/5 就是马赛克 —— 而我们要的是"糊但看得清"。
 * 所以这里自己把像素抠出来做**区域平均**（box filter）：每个目标像素取它覆盖
 * 的那些源像素求平均，缩略图上还能认出这是什么。
 *
 * 取像素的两条路：
 *   · JPEG（img_data 为空）→ lv_img_decoder_read_line 逐行取（已经是 RGB565）；
 *   · PNG（解码器一次性给整块）→ 直接从 img_data 取（RGB565+alpha，3 字节/像素）。
 *
 * ⚠️ 只能在 UI 线程调（碰 LVGL 解码器）。
 * ═══════════════════════════════════════════════════════════════════════ */

#define TB_DECODE_PIXEL_CAP 400000   /* 解码中间缓冲的像素上限：w*h*3 ≤ 1.2MB */

static void tb_unpack565(uint16_t c, int *r, int *g, int *b) {
  *r = (c >> 11) & 0x1F;
  *g = (c >> 5) & 0x3F;
  *b = c & 0x1F;
}

void *tb_image_resample(void *src_dsc, int box_w, int box_h,
                        int *out_w, int *out_h) {
  if (out_w) *out_w = 0;
  if (out_h) *out_h = 0;
  if (!src_dsc || box_w <= 0 || box_h <= 0) return NULL;

  lv_img_dsc_t *sd = (lv_img_dsc_t *)src_dsc;
  if (!sd->data || sd->header.w <= 0 || sd->header.h <= 0) return NULL;

  const uint8_t *raw = sd->data;
  bool is_png = (sd->data_size > 8 && raw[0] == 0x89 && raw[1] == 0x50);

  /* JPEG 挑降采样档位：最小的那一档，让解码出来的图既塞得进 box*2
     （留点余量，区域平均才有东西可平均），又不超过解码预算。 */
  int scale = is_png ? 0 : 3;
  if (!is_png) {
    int pw = sd->header.w, ph = sd->header.h;
    for (int s = 0; s <= 3; s++) {
      long dw = pw >> s, dh = ph >> s;
      if (dw <= (long)box_w * 2 && dh <= (long)box_h * 2 &&
          dw * dh <= TB_DECODE_PIXEL_CAP) {
        scale = s;
        break;
      }
    }
  }

  lv_img_dsc_t tmp;                 /* 一份副本：档位只影响这次的解码 */
  lv_img_decoder_dsc_t dec;
  uint8_t *dst = NULL;
  int32_t *acc = NULL;
  uint8_t *row = NULL;
  int sw = 0, sh = 0, dw = 0, dh = 0, bpp = 2, has_alpha = 0;
  int ret = 0;

  tmp = *sd;
  tmp.header.reserved = (uint32_t)scale;

  if (lv_img_decoder_open(&dec, is_png ? (const void *)sd : (const void *)&tmp,
                          lv_color_white(), 0) != LV_RES_OK) {
    Serial.println("[Img] resample: decoder open failed");
    return NULL;
  }
  sw = (int)dec.header.w;
  sh = (int)dec.header.h;
  if (sw <= 0 || sh <= 0) ret = 1;
  has_alpha = lv_img_cf_has_alpha(dec.header.cf) ? 1 : 0;
  bpp = has_alpha ? 3 : 2;

  /* 目标尺寸：等比缩到能塞进 box（只缩不放） */
  if (!ret) {
    dw = sw;
    dh = sh;
    if (dw > box_w) {
      dh = (int)((long)dh * box_w / dw);
      dw = box_w;
    }
    if (dh > box_h) {
      dw = (int)((long)dw * box_h / dh);
      dh = box_h;
    }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    dst = (uint8_t *)tb_alloc((size_t)dw * dh * bpp);
    acc = (int32_t *)tb_alloc(sizeof(int32_t) * (size_t)dw * 4);
    if (!dst || !acc) ret = 2;
  }

  const uint8_t *mem = (const uint8_t *)dec.img_data;
  if (!ret && !mem) {
    row = (uint8_t *)tb_alloc((size_t)sw * 2);
    if (!row) ret = 3;
  }

  for (int dy = 0; !ret && dy < dh; dy++) {
    int y0 = (int)((long)dy * sh / dh);
    int y1 = (int)((long)(dy + 1) * sh / dh);
    if (y1 <= y0) y1 = y0 + 1;
    if (y1 > sh) y1 = sh;

    memset(acc, 0, sizeof(int32_t) * (size_t)dw * 4);
    for (int y = y0; y < y1; y++) {
      if (!mem) {
        /* JPEG 逐行取；read_line 已经是 RGB565，只认 y 递增，不能回头 */
        if (lv_img_decoder_read_line(&dec, 0, y, sw, row) != LV_RES_OK) {
          ret = 4;
          break;
        }
      }
      const uint8_t *srcp = mem ? (mem + (size_t)y * sw * bpp) : row;
      for (int dx = 0; dx < dw; dx++) {
        int x0 = (int)((long)dx * sw / dw);
        int x1 = (int)((long)(dx + 1) * sw / dw);
        if (x1 <= x0) x1 = x0 + 1;
        if (x1 > sw) x1 = sw;
        int r = 0, g = 0, b = 0, a = 0;
        for (int x = x0; x < x1; x++) {
          const uint8_t *q = srcp + (size_t)x * bpp;
          int rr, gg, bb;
          tb_unpack565((uint16_t)(q[0] | (q[1] << 8)), &rr, &gg, &bb);
          r += rr;
          g += gg;
          b += bb;
          if (has_alpha) a += q[2];
        }
        int32_t *o = acc + dx * 4;
        int cnt = x1 - x0;
        o[0] += r;
        o[1] += g;
        o[2] += b;
        o[3] += has_alpha ? a : (cnt * 255);
      }
    }
    if (ret) break;

    int rows = y1 - y0;
    uint8_t *dp = dst + (size_t)dy * dw * bpp;
    for (int dx = 0; dx < dw; dx++) {
      int x0 = (int)((long)dx * sw / dw);
      int x1 = (int)((long)(dx + 1) * sw / dw);
      if (x1 <= x0) x1 = x0 + 1;
      if (x1 > sw) x1 = sw;
      int cnt = rows * (x1 - x0);
      const int32_t *o = acc + dx * 4;
      int r = (int)(o[0] / cnt) & 0x1F;
      int g = (int)(o[1] / cnt) & 0x3F;
      int b = (int)(o[2] / cnt) & 0x1F;
      uint16_t c = (uint16_t)((r << 11) | (g << 5) | b);
      dp[0] = (uint8_t)(c & 0xFF);
      dp[1] = (uint8_t)(c >> 8);
      if (has_alpha) dp[2] = (uint8_t)(o[3] / cnt);
      dp += bpp;
    }
  }

  lv_img_decoder_close(&dec);
  if (row) heap_caps_free(row);
  if (acc) heap_caps_free(acc);

  if (ret) {
    if (dst) heap_caps_free(dst);
    Serial.printf("[Img] resample failed: ret=%d %dx%d scale=%d\n", ret, sw, sh,
                  scale);
    return NULL;
  }

  lv_img_dsc_t *out = (lv_img_dsc_t *)tb_alloc(sizeof(lv_img_dsc_t));
  if (!out) {
    heap_caps_free(dst);
    return NULL;
  }
  memset(out, 0, sizeof(*out));
  out->header.always_zero = 0;
  out->header.w = (uint32_t)dw;
  out->header.h = (uint32_t)dh;
  out->header.cf = has_alpha ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_TRUE_COLOR;
  out->data = dst;
  out->data_size = (uint32_t)((size_t)dw * dh * bpp);
  if (out_w) *out_w = dw;
  if (out_h) *out_h = dh;
  Serial.printf("[Img] resample %dx%d(scale %d) -> %dx%d %s\n", sw, sh, scale,
                dw, dh, has_alpha ? "RGB565A" : "RGB565");
  return out;
}

'''
t = sub(p, t,
"""static bool lvgl_renderer_init(Renderer *renderer) {""",
RESAMPLE.lstrip('\n') + """static bool lvgl_renderer_init(Renderer *renderer) {""")

t = sub(p, t,
"""static int s_linkCount = 0;

void lvgl_renderer_set_link_callback(LvglLinkCallback cb) { s_linkCb = cb; }""",
"""static int s_linkCount = 0;

void lvgl_renderer_set_link_callback(LvglLinkCallback cb) { s_linkCb = cb; }

/* ── 缩略图点击 ──
   user_data 直接存**布局节点指针**。为什么敢存：节点和 widget 同生共死
   （换页时 freeLayoutTree + contentReset 一起做），而且这里只处理 CLICKED，
   销毁阶段不会再派发给它。 */
static LvglImageCallback s_imageCb = NULL;
void lvgl_renderer_set_image_callback(LvglImageCallback cb) { s_imageCb = cb; }

static void image_clicked_cb(lv_event_t *e) {
  if (!s_imageCb) return;
  void *node = lv_event_get_user_data(e);
  if (node) s_imageCb(node);
}

static void lvgl_renderer_register_image_handler(Renderer *renderer,
                                                 void *widget, void *node) {
  (void)renderer;
  if (!widget || !node) return;
  lv_obj_add_flag((lv_obj_t *)widget, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb((lv_obj_t *)widget, image_clicked_cb, LV_EVENT_CLICKED,
                      node);
}""")

t = sub(p, t,
"""  renderer->base.create_image = lvgl_renderer_create_image;""",
"""  renderer->base.create_image = lvgl_renderer_create_image;
  renderer->base.register_image_handler = lvgl_renderer_register_image_handler;""")

t = sub(p, t,
"""  lv_obj_t *img = lv_img_create(parent);
  lv_img_set_src(img, (const lv_img_dsc_t *)img_dsc);

  /* 比内容区还宽的图直接不画：LVGL 对 RAW 图是按行 read_line 画的，
     lv_img_set_zoom 在 RAW 上会把每一行各自缩放（画出来是错位的），
     所以这里不做缩放，超宽的图在上一道尺寸闸就被丢掉了。 */
  (void)max_w;
  return img;""",
"""  lv_obj_t *img = lv_img_create(parent);
  lv_img_set_src(img, (const lv_img_dsc_t *)img_dsc);

  /* 缩略图早就重采样到 <= THUMB 了，这里不需要再缩放。
     ⚠️ 别想着用 lv_img_set_zoom 收尾：LVGL 对未解码完的图按行变换，
     一 zoom 就错位（详见 tb_image_resample 上面的注释）。 */
  (void)max_w;
  lv_obj_set_style_border_color(img, lv_color_hex(0x444444), 0);
  lv_obj_set_style_border_width(img, 1, 0);
  lv_obj_set_style_pad_all(img, 1, 0);
  return img;""")
save(p, t)

print('FAILURES: %d' % len(fail))
for f in fail:
    print(' -', f)
sys.exit(1 if fail else 0)
