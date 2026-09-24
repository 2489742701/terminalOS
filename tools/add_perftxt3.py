"""perftxt 加「纯色矩形填充」对照组。

已经排除的假说（都实测过，不是猜）：
  -O2        -> 零收益  ⟹ 不是指令数
  同字对照   -> 零差异  ⟹ 不是字形数据的 flash cache miss
  IRAM_ATTR  -> 零收益  ⟹ 不是取指 cache miss

剩下：钱确实花在「每像素」上（CN 1.26us/px ≈ 300 周期/px，理论只要 ~15）。
现在要分清是 (a) LVGL 的 fill 基础设施本身就慢，还是 (b) 字形 mask 混合额外慢。
做法：画一个 320x128 = 40960 px 的纯色矩形 —— 像素数跟 160 个汉字完全相同，
但它走的是无 mask 的纯填充路径。两者一比就知道钱花在哪。
"""
import io, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
P = os.path.join(ROOT, 'src', 'hal', 'serial_console.cpp')

ANCHOR_BENCH = '''  uint32_t t5 = txtBench(4);
  for (int i = 0; i < rows; i++) lv_obj_del(labs[i]);'''

ADD_BENCH = '''  /* --- 纯色矩形 320x128 = 40960 px ---
     像素数与 rows*20 个汉字完全相同（16x16x160），但走的是**无 mask 的纯填充**路径。
     拿它跟中文那一项比：若两者接近 ⟹ 慢在 fill 基础设施；若它快很多 ⟹ 慢在 mask 混合。 */
  {
    int w = 320, h = 128;
    while ((uint32_t)(w * h) < (uint32_t)(rows * 20 * 16 * 16) && h < 460) h++;
    lv_obj_t* rect = lv_obj_create(ov);
    lv_obj_clear_flag(rect, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(rect, w, h);
    lv_obj_set_pos(rect, 0, 200);
    lv_obj_set_style_bg_color(rect, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rect, 0, 0);
    lv_obj_set_style_pad_all(rect, 0, 0);
    lv_obj_set_style_radius(rect, 0, 0);
    uint32_t t6 = txtBench(4);
    uint32_t px = (uint32_t)w * h;
    Serial.printf("[PerfTxt] %-22s %6u  (+%u)  %ux%u=%u px\\n", "+solid rect",
                  (unsigned)t6, (unsigned)(t6 - t1), w, h, (unsigned)px);
    Serial.printf("[PerfTxt] per-px    solid=%u ns  CN-glyph=1260 ns  ratio=%.1fx\\n",
                  (unsigned)(((t6 - t1) * 1000) / px),
                  1260.0 / (double)(((t6 - t1) * 1000) / px));
    lv_obj_del(rect);
  }'''

ANCHOR_OUT = '''  Serial.printf("[PerfTxt] per-char  CN=%u us  CN_SAME=%u us  EN=%u us   label-fixed=%u us\\n",'''


def rep(s, old, new, tag):
    for o in (old.replace('\n', '\r\n'), old):
        if o in s:
            return s.replace(o, new.replace('\n', '\r\n') if o == old.replace('\n', '\r\n') else new, 1)
    raise SystemExit('MISS: ' + tag)


def main():
    s = io.open(P, 'r', encoding='utf-8', newline='').read()
    if '+solid rect' in s:
        print('ALREADY_PRESENT')
        return
    s = rep(s, ANCHOR_BENCH, ANCHOR_BENCH + '\n' + ADD_BENCH, 'rect bench')
    io.open(P, 'w', encoding='utf-8', newline='').write(s)
    print('INJECTED len=%d' % len(s))


if __name__ == '__main__':
    main()
