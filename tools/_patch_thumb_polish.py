# -*- coding: utf-8 -*-
"""缩略图收尾：加边框容器 + 全屏小图适度放大。"""
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


# ── 1) create_image：外面套一个有边框的容器 ──
p, t = load('src/browser_engine/src/lvgl_renderer.cpp')
t = sub(p, t,
"""  lv_obj_t *img = lv_img_create(parent);
  lv_img_set_src(img, (const lv_img_dsc_t *)img_dsc);

  /* 缩略图早就重采样到 <= THUMB 了，这里不需要再缩放。
     ⚠️ 别想着用 lv_img_set_zoom 收尾：LVGL 对未解码完的图按行变换，
     一 zoom 就错位（详见 tb_image_resample 上面的注释）。 */
  (void)max_w;
  lv_obj_set_style_border_color(img, lv_color_hex(0x444444), 0);
  lv_obj_set_style_border_width(img, 1, 0);
  lv_obj_set_style_pad_all(img, 1, 0);
  return img;""",
"""  /* 外面套一层带边框的容器再放图。
     为什么不直接给 lv_img 加边框：lv_img 是拿**对象整体坐标**画图的
     （不是内容区），边框会被图盖住 —— 等于白设。 */
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(box, 2, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0x888888), 0);
  lv_obj_set_style_radius(box, 4, 0);

  lv_obj_t *img = lv_img_create(box);
  lv_img_set_src(img, (const lv_img_dsc_t *)img_dsc);
  lv_obj_center(img);

  /* 缩略图早就重采样到 <= THUMB 了，这里不需要再缩放。
     ⚠️ 别想着用 lv_img_set_zoom 收尾：LVGL 对未解码完的图按行变换，
     一 zoom 就错位（详见 tb_image_resample 上面的注释）。 */
  (void)max_w;
  return box;""")
save(p, t)

# ── 2) 查看器：小图适度放大（最多 2x） ──
p, t = load('src/app/browser_screen.cpp')
t = sub(p, t,
"""  lv_obj_t* img = lv_img_create(root);
  lv_img_set_src(img, (const lv_img_dsc_t*)dsc);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, -16);
  lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);""",
"""  lv_obj_t* img = lv_img_create(root);
  lv_img_set_src(img, (const lv_img_dsc_t*)dsc);
  lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);

  /* 小图放大展示：480 屏上 300px 的图原样摆着还是小。上限 2 倍 ——
     再往上就是最近邻糊成块，反而看不清。
     大图不动：它已经是"能解到的最高分辨率"，再放只会更糊。 */
  int zw = w, zh = h;
  if (w < 420 && h < 340 && w > 0 && h > 0) {
    int z = 448 * 256 / w;
    int z2 = 384 * 256 / h;
    if (z2 < z) z = z2;
    if (z > 512) z = 512;
    if (z > 256) {
      /* pivot 必须挪到左上角：默认是中心，缩放会从中间往外撑，位置全歪 */
      lv_img_set_pivot(img, 0, 0);
      lv_img_set_zoom(img, (uint16_t)z);
      zw = w * z / 256;
      zh = h * z / 256;
      lv_obj_set_size(img, zw, zh);
    }
  }
  lv_obj_align(img, LV_ALIGN_CENTER, 0, -16);""")

t = sub(p, t,
"""  Serial.printf("[Img] viewer %dx%d from %.64s\\n", w, h,
                node->img_src ? node->img_src : "?");""",
"""  Serial.printf("[Img] viewer %dx%d -> %dx%d from %.56s\\n", w, h, zw, zh,
                node->img_src ? node->img_src : "?");""")
save(p, t)

print('FAILURES: %d' % len(fail))
for f in fail:
    print(' -', f)
sys.exit(1 if fail else 0)
