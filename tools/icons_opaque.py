"""
icons_opaque.py - 图标画布从 TRUE_COLOR_ALPHA(32bpp) 改成 TRUE_COLOR(16bpp 不透明)

perf 差分实测（桌面屏，draw 42409 us）：
  藏掉 11 个 canvas 图标 -> draw 29348（这些图标吃掉 13061 us = 31%）
  11 个图标 13ms，平均每个 1.2ms 只画 22x22=484 个像素 —— 2.5 us/px，
  比纯写内存慢几百倍。原因就是 alpha：TRUE_COLOR_ALPHA 每像素都要
  读背景 + 按 alpha 混合 + 写回，还占 4B/px 双倍内存带宽。

改成不透明 RGB565 后：blit 退化成纯拷贝，缓冲也直接减半。
代价：图标周围不再透明，而是不透明的黑色 —— 目前图标都画在黑色背景上
（桌面磁贴 transparent -> 底下是 scr 的黑色；顶栏同理），视觉应当无差。
若某些屏的底色不是黑，会露出黑方块，需要 master 看图确认后再针对性处理。
"""
import io, os

P = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                  '..', 'src', 'app', 'icons.cpp'))

raw = io.open(P, 'r', encoding='utf-8', newline='').read()
crlf = '\r\n' in raw
s = raw.replace('\r\n', '\n')

# 1) 缓冲大小：4B/px -> 2B/px（两处）
n = s.count('uint32_t bufBytes = (uint32_t)size * size * 4;')
assert n == 2, 'bufBytes anchor count=%d' % n
s = s.replace('uint32_t bufBytes = (uint32_t)size * size * 4;',
              'uint32_t bufBytes = (uint32_t)size * size * 2;')

# 2) 画布格式：带 alpha -> 不透明（两处）
n = s.count('LV_IMG_CF_TRUE_COLOR_ALPHA);')
assert n == 2, 'CF anchor count=%d' % n
s = s.replace('LV_IMG_CF_TRUE_COLOR_ALPHA);', 'LV_IMG_CF_TRUE_COLOR);')

# 3) 背景填充：透明 -> 不透明
#    四处：icon_create / icon_create_wifi / icon_wifi_set_level / icon_set_type
old_bg = 'lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);'
n = s.count(old_bg)
assert n == 4, 'fill_bg anchor count=%d' % n
s = s.replace(old_bg, 'lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);')

# 4) 注释更正
s = s.replace('// 从 PSRAM 分配 canvas 缓冲，不占用 LVGL 128KB 内存池',
              '// 从 PSRAM 分配 canvas 缓冲，不占用 LVGL 128KB 内存池\n'
              '  // 注意：格式是不透明 RGB565（2B/px），不是带 alpha 的 32bpp')

if crlf:
    s = s.replace('\n', '\r\n')
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('WROTE ' + P)
