# -*- coding: utf-8 -*-
"""缩略图 v2：dsc 头必须自带宽高 + JPEG 降采样档位。

v1 踩的坑：
  · LVGL 的 PNG 解码器（lv_png.c）对 VARIABLE 源**不解析 PNG 头**，
    直接把 img_dsc->header 里的 cf/w/h 抄进解码器 dsc —— 我们当初全填 0，
    于是 lv_img_decoder_get_info 报 OK 但 w=h=0，lv_img 尺寸为 0 → 什么都看不见。
  · lv_res_t 里 INV=0 / OK=1，别再被"res=1"吓一跳以为失败了。

另外把 lv_conf.h 的 LV_IMG_CACHE_DEF_SIZE 从 256 降到 16：
  每解一张图就常驻一份解码缓冲，256 条等于可以一直挂着 256 张图不释放。
"""
import io, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONF = (r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040"
        r"\4.0inch_ESP32-4848S040\1-Demo\Demo_Arduino\Libraries\Lvgl\lv_conf.h")
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


# ── lvgl_renderer.h：签名改带尺寸/预算 ──────────────────────────────
p, t = load('src/browser_engine/include/lvgl_renderer.h')
t = sub(p, t,
"""/* 接管 data 的所有权，包一个 lv_img_dsc_t。失败返回 NULL 且不接管 data。 */
void *tb_image_dsc_create(uint8_t *data, size_t len);""",
"""/* 接管 data 的所有权，包一个 lv_img_dsc_t。
   w/h 必须是 tb_image_peek_size 读出来的**原始**尺寸 —— LVGL 的 PNG 解码器
   不会自己去解析 PNG 头，它把 img_dsc->header 里的 w/h 原样抄走，填 0 的话
   图片尺寸就是 0×0，lv_img 什么都不显示（v1 就是栽在这里）。

   max_w / max_px 是解码后的预算：
     JPEG → 在 1/1、1/2、1/4、1/8 里挑最小能塞下的档位（tjpgd 解码时降采样）；
     PNG  → 没有降采样这一档，超预算直接返回 NULL。
   scale_out 可空，用来回传实际选中的档位（0~3）方便打日志。

   返回 NULL = 这张图不该显示（格式不认识，或 1/8 都塞不下预算）。
   ⚠️ 返回 NULL 时**没有**接管 data，调用方自己 heap_caps_free。 */
void *tb_image_dsc_create(uint8_t *data, size_t len, int w, int h,
                          int max_w, long max_px, int *scale_out);""")
save(p, t)

# ── lvgl_renderer.cpp：实现 ─────────────────────────────────────────
p, t = load('src/browser_engine/src/lvgl_renderer.cpp')
t = sub(p, t,
"""void *tb_image_dsc_create(uint8_t *data, size_t len) {
  lv_img_dsc_t *d = (lv_img_dsc_t *)tb_alloc(sizeof(lv_img_dsc_t));
  if (!d) return NULL;
  memset(d, 0, sizeof(*d));
  d->header.always_zero = 0;
  d->header.cf = LV_IMG_CF_RAW;  /* 真正的格式交给 SJPG/PNG 解码器自己认 */
  d->data = data;
  d->data_size = len;
  return d;
}""",
"""void *tb_image_dsc_create(uint8_t *data, size_t len, int w, int h,
                          int max_w, long max_px, int *scale_out) {
  if (scale_out) *scale_out = 0;
  if (!data || len < 16 || w <= 0 || h <= 0) return NULL;

  bool is_png = (len > 24 && data[0] == 0x89 && data[1] == 0x50 &&
                 data[2] == 0x4E && data[3] == 0x47);
  bool is_jpg = (data[0] == 0xFF && data[1] == 0xD8);
  if (!is_png && !is_jpg) return NULL;

  /* JPEG 能在**解码阶段**降采样（tjpgd 的 1/2 / 1/4 / 1/8，档位走
     lv_img_header_t.reserved，见 tools/patch_lvgl_jpeg_scale.py），
     所以再大的图也能当缩略图显示；PNG 走 lodepng，只能原尺寸解，
     超预算就放弃 —— 宁可不显示，也别把 PSRAM 吃穿。 */
  int scale = 0;
  if (is_jpg) {
    for (scale = 0; scale <= 3; scale++) {
      long sw = w >> scale;
      long sh = h >> scale;
      if (sw <= max_w && sw * sh <= max_px) break;
    }
    if (scale > 3) return NULL;   /* 1/8 都还塞不下 */
  } else if (w > max_w || (long)w * (long)h > max_px) {
    return NULL;
  }

  lv_img_dsc_t *d = (lv_img_dsc_t *)tb_alloc(sizeof(lv_img_dsc_t));
  if (!d) return NULL;
  memset(d, 0, sizeof(*d));
  d->header.always_zero = 0;
  d->header.w = w >> scale;
  d->header.h = h >> scale;
  /* PNG：lodepng 解完再转成 RGB565+alpha（3 字节/像素）→ TRUE_COLOR_ALPHA。
     JPEG：交给 SJPG 解码器按行 read_line → RAW。
     ⚠️ 这两个 cf 不能互换：PNG 解码器不设 cf，直接抄 dsc 头里这个值。 */
  d->header.cf = is_png ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_RAW;
  d->header.reserved = (uint32_t)scale;   /* JPEG 降采样档位 */
  d->data = data;
  d->data_size = len;
  if (scale_out) *scale_out = scale;
  return d;
}""")
save(p, t)

# ── browser_screen.cpp：调用点 ──────────────────────────────────────
p, t = load('src/app/browser_screen.cpp')
t = sub(p, t,
"""    if (w > IMG_MAX_W || (long)w * (long)h > IMG_MAX_PIXELS) {
      Serial.printf("[Img] drop too big %dx%d (%u B)\\n", w, h, (unsigned)len);
      heap_caps_free(data);
      continue;
    }
    void* dsc = tb_image_dsc_create(data, len);
    if (!dsc) { heap_caps_free(data); continue; }
    imgs[i]->img_dsc = dsc;
    ok++;
    Serial.printf("[Img] ok %dx%d %u B  %.52s\\n", w, h, (unsigned)len,
                  imgs[i]->img_src);""",
"""    int scale = 0;
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
                  imgs[i]->img_src);""")

t = sub(p, t,
"""  void* dsc = tb_image_dsc_create(data, len);
  if (!dsc) { heap_caps_free(data); Serial.println("[Img] test: dsc alloc failed"); return; }""",
"""  int scale = 0;
  void* dsc = tb_image_dsc_create(data, len, w, h, IMG_MAX_W, IMG_MAX_PIXELS,
                                  &scale);
  if (!dsc) {
    heap_caps_free(data);
    Serial.println("[Img] test: rejected (unknown fmt or over budget)");
    return;
  }
  Serial.printf("[Img] test scale=1/%d -> %dx%d\\n", 1 << scale, w >> scale,
                h >> scale);""")
save(p, t)

# ── lv_conf.h：图片缓存条数 256 → 16 ────────────────────────────────
if os.path.exists(CONF):
    s = io.open(CONF, encoding='utf-8', errors='replace').read()
    if 'LV_IMG_CACHE_DEF_SIZE 256' in s:
        s = s.replace('#define LV_IMG_CACHE_DEF_SIZE 256',
                      '#define LV_IMG_CACHE_DEF_SIZE 16', 1)
        io.open(CONF, 'w', encoding='utf-8', newline='').write(s)
        print('lv_conf.h: LV_IMG_CACHE_DEF_SIZE 256 -> 16')
    else:
        print('lv_conf.h: LV_IMG_CACHE_DEF_SIZE already changed')
else:
    fail.append('lv_conf.h not found')

print('FAILURES: %d' % len(fail))
for f in fail:
    print(' -', f)
sys.exit(1 if fail else 0)
