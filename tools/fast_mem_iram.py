"""把 LVGL 的绘制热函数搬进 IRAM。

判据链（每一步都实测过，不是猜的）：
  perfx 差分 -> 文字占 draw 的 59%
  perftxt    -> 中文 305us/字 vs 英文 78us/字，比值跟字形像素数成正比
                ⟹ 钱花在「每像素」上：305us/256px = 1.19us/px ≈ 286 个 CPU 周期
  -O2        -> 整帧 58685 vs 58720 us，零收益 ⟹ 不是指令数问题
  同字对照   -> CN_SAME 315 vs CN 305，无差异 ⟹ 不是 flash 数据的 cache miss

剩下唯一解释：取指。LV_ATTRIBUTE_FAST_MEM 一直是空的，draw_letter_normal /
fill_normal / lv_color_mix 这些逐像素循环全在 flash 上跑 XIP，S3 的 I-cache
只有 16KB，而 LVGL 绘制路径代码量远超它 -> 循环里不停 I-cache miss。

用法: python tools/fast_mem_iram.py on|off
⚠️ 若链接报 iram0_0_seg overflowed，说明 IRAM 装不下，需要收缩（只留最热几个）。
"""
import io, os, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
P = os.path.join(ROOT, 'src', 'config', 'lv_conf.h')

# 按行定位：所有 .h 的换行符不统一，整块替换会踩坑
ANCHOR = '#define LV_ATTRIBUTE_FAST_MEM'
HEAD = [
    '/* PERF: 空的 FAST_MEM 是文字渲染慢 4 倍的元凶 —— 逐像素循环在 flash 上跑 XIP，',
    '   S3 的 I-cache 只有 16KB，而 LVGL 绘制路径远大于它，循环里不停 miss。',
    '   实测 305us/汉字 = 1.19us/px = 286 周期/px，纯取指瓶颈（-O2 零收益可佐证）。',
    '   搬进 IRAM 后观感完全不变。 */',
    '#include <esp_attr.h>',
    '#define LV_ATTRIBUTE_FAST_MEM IRAM_ATTR',
]


def main():
    lv = sys.argv[1] if len(sys.argv) > 1 else 'on'
    lines = io.open(P, 'r', encoding='utf-8', newline='').read().split('\n')
    out = []
    found = False
    for ln in lines:
        if ln.strip() == ANCHOR or ln.strip().startswith(ANCHOR + ' '):
            found = True
            # off 时必须还原成「空宏」那一行 —— 直接 append(ln) 会把 IRAM_ATTR 原样留下
            out.extend(HEAD) if lv == 'on' else out.append(ANCHOR)
            continue
        # 去掉上一次注入的注释块和 include
        if lv == 'off' and (ln.startswith('/* PERF: 空的 FAST_MEM')
                            or ln.strip() == '#include <esp_attr.h>'
                            or ln.startswith('   S3 的 I-cache')
                            or ln.startswith('   实测 305us/汉字')
                            or ln.startswith('   搬进 IRAM 后观感完全不变。 */')):
            continue
        out.append(ln)
    if not found:
        raise SystemExit('anchor not found: %s' % ANCHOR)
    io.open(P, 'w', encoding='utf-8', newline='').write('\n'.join(out))
    on = any('LV_ATTRIBUTE_FAST_MEM IRAM_ATTR' in l for l in out)
    print('FAST_MEM=%s' % ('IRAM_ATTR' if on else 'empty'))


if __name__ == '__main__':
    main()
