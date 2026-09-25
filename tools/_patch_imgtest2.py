# -*- coding: utf-8 -*-
"""imgtest 也走一遍重采样 —— 不点屏就能验"这张图能不能变成缩略图"。"""
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


p, t = load('src/app/browser_screen.cpp')
t = sub(p, t,
"""  Serial.printf("[Img] test decoder: res=%d cf=%u %ux%u\\n", (int)r,
                (unsigned)hdr.cf, (unsigned)hdr.w, (unsigned)hdr.h);
  Serial.printf("[Img] test PSRAM free=%u\\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  tb_image_dsc_free(dsc);""",
"""  Serial.printf("[Img] test decoder: res=%d cf=%u %ux%u\\n", (int)r,
                (unsigned)hdr.cf, (unsigned)hdr.w, (unsigned)hdr.h);

  /* 真正要用的是这一条：重采样成缩略图（再解到大图）。
     串口看到 [Img] resample ... 才算这张图"能显示"。 */
  int tw = 0, th2 = 0;
  void* thumb = tb_image_resample(dsc, g_imgThumbPx, g_imgThumbPx, &tw, &th2);
  Serial.printf("[Img] test thumb=%s %dx%d (box=%d)\\n", thumb ? "OK" : "FAIL",
                tw, th2, g_imgThumbPx);
  if (thumb) tb_image_dsc_free(thumb);
  int vw = 0, vh = 0;
  void* big = tb_image_resample(dsc, 448, 384, &vw, &vh);
  Serial.printf("[Img] test view=%s %dx%d\\n", big ? "OK" : "FAIL", vw, vh);
  if (big) tb_image_dsc_free(big);

  Serial.printf("[Img] test PSRAM free=%u\\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  tb_image_dsc_free(dsc);""")
save(p, t)

print('FAILURES: %d' % len(fail))
for f in fail:
    print(' -', f)
sys.exit(1 if fail else 0)
