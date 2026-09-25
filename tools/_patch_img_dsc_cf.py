# -*- coding: utf-8 -*-
"""修：重采样拿到的可能不是"解码后的像素"。

根因分析：
  lv_img_decoder_open 会**依次**问三个解码器（SJPG → PNG → 内建）。
  我们给源 dsc 填的 cf 是 LV_IMG_CF_TRUE_COLOR_ALPHA(5)，它正好落在内建解码器
  的受理范围（CF_BUILT_IN_FIRST=TRUE_COLOR .. CF_BUILT_IN_LAST=ALPHA_8BIT）里。
  于是当 PNG 解码器的 open_cb 失败时（lodepng 解不了这张图），流程**不会报错**，
  而是落到内建解码器 —— 内建对 VARIABLE 源的处理是把 img_dsc->data
  （**压缩的 PNG 原始字节**）当像素交出去。
  结果：我们按 300x200x3 去读一个 8KB 的缓冲区，读到的全是别处的内存 →
  屏幕上一坨彩色乱码。而且 dec.header.w/h 还是我们自己填的 300x200，看着一切正常。

修法（两条一起上）：
  1) 源 dsc 的 cf 改成 LV_IMG_CF_RAW_ALPHA(2)。这既是 LVGL 对 png **文件**源的
     官方写法（lv_png.c 的 FILE 分支就填 RAW_ALPHA），又落在内建受理范围之外
     （2 < 4）—— 解码器只能由 PNG 认领，失败就是干净地失败，不会再有人递假像素。
  2) 重采样里加一条兜底：img_data 如果**就是** data 本身，说明是内建把原始文件
     原样递回来了，一律当"没解出来"处理。
  3) 顺手把 resample 的输入状态和"图片瓦片到底建没建"打进日志。
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


# ── 1) 源 dsc 的 cf：PNG 用 RAW_ALPHA，别用 TRUE_COLOR_ALPHA ──
p, t = load('src/browser_engine/src/lvgl_renderer.cpp')
t = sub(p, t,
"""  /* PNG：lodepng 解完再转成 RGB565+alpha（3 字节/像素）→ TRUE_COLOR_ALPHA。
     JPEG：交给 SJPG 解码器按行 read_line → RAW。
     ⚠️ 这两个 cf 不能互换：PNG 解码器不设 cf，直接抄 dsc 头里这个值。 */
  d->header.cf = is_png ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_RAW;""",
"""  /* ⚠️⚠️ 这里的 cf 是**给解码器看的输入**，不是"解码后的格式"，填错会出乱码：
       · PNG 必须填 RAW_ALPHA。填 TRUE_COLOR_ALPHA(5) 会踩一个大坑 ——
         内建解码器受理 4~11 这一段，PNG 解不出来时它会**接管**，而内建对
         VARIABLE 源的做法是把 data（压缩的 PNG 原文）当像素交出来。
         结果是：尺寸看着对、像素全是乱码，还不报错。
         RAW_ALPHA(2) 落在内建受理范围外，只有 PNG 解码器能认领，
         解不了就干净地失败。
       · JPEG 填 RAW，交给 SJPG 逐行 read_line。 */
  d->header.cf = is_png ? LV_IMG_CF_RAW_ALPHA : LV_IMG_CF_RAW;""")

# ── 2) 重采样：兜底 + 输入状态日志 ──
t = sub(p, t,
"""  sw = (int)dec.header.w;
  sh = (int)dec.header.h;
  if (sw <= 0 || sh <= 0) ret = 1;
  has_alpha = lv_img_cf_has_alpha(dec.header.cf) ? 1 : 0;
  bpp = has_alpha ? 3 : 2;""",
"""  sw = (int)dec.header.w;
  sh = (int)dec.header.h;
  if (sw <= 0 || sh <= 0) ret = 1;
  has_alpha = lv_img_cf_has_alpha(dec.header.cf) ? 1 : 0;
  bpp = has_alpha ? 3 : 2;

  /* ⚠️ 兜底：img_data 如果就是 data 本身，说明根本没有解码器解它，
     是内建解码器把**压缩原文**原样递回来了 —— 当像素读就是彩色乱码。 */
  if (dec.img_data && (const uint8_t *)dec.img_data == sd->data) {
    Serial.println("[Img] resample: built-in handed back raw bytes, refuse");
    lv_img_decoder_close(&dec);
    return NULL;
  }
  Serial.printf("[Img] resample in: cf=%u %dx%d img_data=%s src=%u B\\n",
                (unsigned)dec.header.cf, sw, sh,
                dec.img_data ? "decoded" : "line-by-line",
                (unsigned)sd->data_size);""")
save(p, t)

# ── 3) 渲染：图片瓦片建了没有，得看得见 ──
p, t = load('src/browser_engine/src/layout_engine.cpp')
t = sub(p, t,
"""      if (widget) {
        s_widgetCount++;
        if (iface->register_image_handler)
          iface->register_image_handler(renderer, widget, node);
      }""",
"""      if (widget) {
        s_widgetCount++;
        Serial.printf("[Img] tile #%d created\\n", s_widgetCount - 1);
        if (iface->register_image_handler)
          iface->register_image_handler(renderer, widget, node);
      }""")
save(p, t)

print('FAILURES: %d' % len(fail))
for f in fail:
    print(' -', f)
sys.exit(1 if fail else 0)
