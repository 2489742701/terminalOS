# -*- coding: utf-8 -*-
"""两个硬伤一起修：

① LVGL 内存池太小 → PNG 全废、还会出乱码
   lodepng 的分配器是 `lodepng_malloc() { return lv_mem_alloc(size); }`
   （lvgl/src/extra/libs/png/lodepng.c:74），也就是走 **LVGL 自己的池**。
   而池大小是 `LV_MEM_SIZE = 128KB`：
     · 一张 300x200 的 PNG，光 lodepng_decode32 的输出就要 300*200*4 = 240KB
       → lv_mem_alloc 必然失败 → PNG 解码器 open_cb 返回 INV；
     · 然后流程落到**内建解码器**接管，把压缩的 PNG 原文当像素交出来
       （dec.img_data == dsc->data），按 300x200x3 去读一个 8KB 缓冲区 ——
       屏幕上一坨彩色乱码，而且不报错。
   所以这张图在设备上就是"看着有尺寸、内容全是噪声"。
   修：LV_MEM_SIZE 128KB → 768KB（池本来就在 PSRAM，多占 640KB，PSRAM 还有 6MB+）。

② 缩略图瓦片落在第 1 段之外，页面上根本看不到
   163 首页的导航条摊平出 ~58 个胶囊，把 60 块瓦片的前 58 块吃光了，
   图片节点排在 60 名开外 → 第 1 段一块图都铺不出来（日志里
   `[Img] tile #N created` 一条都没有）。
   修：PAGE_SEG_TILES 60 → 80。
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


# ── ① LV_MEM_SIZE ──
s = io.open(CONF, encoding='utf-8', errors='replace').read()
if '#define LV_MEM_SIZE (768U*1024U)' in s:
    print('lv_conf.h: LV_MEM_SIZE already 768K')
elif '#define LV_MEM_SIZE (128U*1024U)' in s:
    s = s.replace('#define LV_MEM_SIZE (128U*1024U)',
                  '#define LV_MEM_SIZE (768U*1024U)', 1)
    io.open(CONF, 'w', encoding='utf-8', newline='').write(s)
    print('lv_conf.h: LV_MEM_SIZE 128K -> 768K（lodepng 走 lv_mem_alloc，'
          '池太小 PNG 一律解不出来）')
else:
    fail.append('lv_conf.h: LV_MEM_SIZE 128U*1024U not found')

# ── ② PAGE_SEG_TILES ──
p, t = load('src/app/browser_screen.cpp')
t = sub(p, t,
"""static const int PAGE_SEG_TILES = 60;   /* 一段铺多少块瓦片 */""",
"""/* 一段铺多少块瓦片。
   ⚠️ 2026-09-25 从 60 提到 80：163 首页的导航条摊平出 ~58 个胶囊，60 块的前
   58 块全被吃光，图片节点排在 60 名开外 —— 第 1 段一块缩略图都铺不出来，
   现象就是"页面上根本找不到图"。80 之后图片能被包进第 1 段。 */
static const int PAGE_SEG_TILES = 80;""")
save(p, t)

# 强制 LVGL 重编（LV_MEM_SIZE 在 lv_conf.h 里）
lib = os.path.join(ROOT, ".wb_build", "esp32s3", "lib51c")
bak = lib + ".stale"
if os.path.isdir(bak):
    n = 1
    while os.path.exists("%s.old%d" % (bak, n)):
        n += 1
    os.rename(bak, "%s.old%d" % (bak, n))
if os.path.isdir(lib):
    os.rename(lib, bak)
    print("renamed lib51c -> lib51c.stale (force LVGL rebuild)")

print('FAILURES: %d' % len(fail))
for f in fail:
    print(' -', f)
sys.exit(1 if fail else 0)
