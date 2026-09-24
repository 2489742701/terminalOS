"""给 LVGL 文字混合宏加"透明像素短路"。

FILL_NORMAL_MASK_PX 是文字（字形 mask 填充）走的宏。它只有 mask==255 的快路径，
**mask==0 时仍然调用 lv_color_mix(color, dest, 0)** —— 这个函数调用 + 计算的
结果恒等于 dest，是纯浪费。汉字 16x16 里大片是透明像素（笔画只占 ~30%），
所以这一半以上像素在白跑。

对比 MAP_NORMAL_MASK_PX（图片路径）早就写了 `if(*mask)` 短路，文字路径漏了。

语义完全等价：lv_color_mix(fg, bg, 0) == bg。
"""
import io
import os
import sys

LVGL = (r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040"
        r"\4.0inch_ESP32-4848S040\1-Demo\Demo_Arduino\Libraries\Lvgl")
P = os.path.join(LVGL, "src", "draw", "sw", "lv_draw_sw_blend.c")

MARK = "/* FILL_NORMAL_MASK_PX: skip fully-transparent px (geek-terminal perf) */"

s = io.open(P, "r", encoding="utf-8", newline="").read()
if MARK in s:
    print("ALREADY PATCHED")
    sys.exit(0)

# 两个分支（LV_COLOR_SCREEN_TRANSP 0 / 非 0）都要改
OLD_A = """#define FILL_NORMAL_MASK_PX(color)                                                          \\
    if(*mask == LV_OPA_COVER) *dest_buf = color;                                 \\
    else *dest_buf = lv_color_mix(color, *dest_buf, *mask);            \\
    mask++;                                                         \\
    dest_buf++;"""

NEW_A = """%s
#define FILL_NORMAL_MASK_PX(color)                                                          \\
    if(*mask) {                                                                 \\
        if(*mask == LV_OPA_COVER) *dest_buf = color;                             \\
        else *dest_buf = lv_color_mix(color, *dest_buf, *mask);                  \\
    }                                                                           \\
    mask++;                                                                     \\
    dest_buf++;""" % MARK

OLD_B = """#define FILL_NORMAL_MASK_PX(color)                                               \\
    if(*mask == LV_OPA_COVER) *dest_buf = color;                                 \\
    else if(disp->driver->screen_transp) lv_color_mix_with_alpha(*dest_buf, dest_buf->ch.alpha, color, *mask, dest_buf, &dest_buf->ch.alpha);           \\
    else *dest_buf = lv_color_mix(color, *dest_buf, *mask);            \\
    mask++;                                                         \\
    dest_buf++;"""

NEW_B = """%s
#define FILL_NORMAL_MASK_PX(color)                                               \\
    if(*mask) {                                                                 \\
        if(*mask == LV_OPA_COVER) *dest_buf = color;                             \\
        else if(disp->driver->screen_transp) lv_color_mix_with_alpha(*dest_buf, dest_buf->ch.alpha, color, *mask, dest_buf, &dest_buf->ch.alpha);           \\
        else *dest_buf = lv_color_mix(color, *dest_buf, *mask);                  \\
    }                                                                           \\
    mask++;                                                                     \\
    dest_buf++;""" % MARK


def rep(old, new, tag):
    global s
    for o, n in ((old, new),
                 (old.replace('\n', '\r\n'), new.replace('\n', '\r\n'))):
        if o in s:
            s = s.replace(o, n, 1)
            print('OK   ' + tag)
            return
    raise SystemExit('MISS: ' + tag)


rep(OLD_A, NEW_A, 'FILL_NORMAL_MASK_PX (screen_transp=0)')
rep(OLD_B, NEW_B, 'FILL_NORMAL_MASK_PX (screen_transp!=0)')

io.open(P, "w", encoding="utf-8", newline="").write(s)
print("WROTE", P)
