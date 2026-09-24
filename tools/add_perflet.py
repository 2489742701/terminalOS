"""注入 perflet：把「画一个汉字」拆成 API 级逐项计时。

到这里已经排除了三个假说（都实测过）：
  -O2 真作用于 LVGL 后 -> draw 38640->37241，只省 3.6%  ⟹ 不是指令数
  同字对照              -> 311 vs 303 us                 ⟹ 不是字形数据 cache miss
  IRAM_ATTR             -> 无变化                        ⟹ 不是取指

而纯色矩形 40960 px 只要 451us（11 ns/px），同样像素数的汉字要 50776us
（1260 ns/px）—— 慢 114 倍，且 fill 基础设施被证明极快。
所以钱一定花在「画一个字」这条路径上除 fill 之外的某一步：
  A 字形查找 lv_font_get_glyph_dsc
  B 位图指针 lv_font_get_glyph_bitmap
  C 临时缓冲 lv_mem_buf_get/release
  D 4bpp 解包到 mask_buf
perflet 对每一项单独打表。
"""
import io, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
P = os.path.join(ROOT, 'src', 'hal', 'serial_console.cpp')

FUNC = r'''
/* ------------------------------------------------------------------ *
 * perflet —— 把一个汉字的绘制拆成 API 级逐项计时
 * ------------------------------------------------------------------ */
static void cmdPerfLet(const char* arg) {
  int n = atoi(arg);
  if (n < 1) n = 100;
  if (n > 5000) n = 5000;

  /* 极客终端文字渲染基准测试文本一二三四五六 */
  static const uint32_t CN[20] = {
    0x6781, 0x5BA2, 0x7EC8, 0x7AEF, 0x6587, 0x5B57, 0x6E32, 0x67D3,
    0x57FA, 0x51C6, 0x6D4B, 0x8BD5, 0x6587, 0x672C, 0x4E00, 0x4E8C,
    0x4E09, 0x56DB, 0x4E94, 0x516D
  };
  static const uint8_t bpp4[16] = {
    0, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255
  };
  const int M = 20;
  uint32_t total = (uint32_t)n * (uint32_t)M;
  Serial.printf("[PerfLet] n=%d x %d chars = %u calls per item\n", n, M, (unsigned)total);

  lv_font_glyph_dsc_t g;

  /* A: 字形查找 */
  uint32_t t = micros();
  for (int i = 0; i < n; i++)
    for (int j = 0; j < M; j++) lv_font_get_glyph_dsc(&font_zh_16, &g, CN[j], 0);
  uint32_t tA = micros() - t;

  /* B: 位图指针 */
  t = micros();
  for (int i = 0; i < n; i++)
    for (int j = 0; j < M; j++) {
      const uint8_t* b = lv_font_get_glyph_bitmap(&font_zh_16, CN[j]);
      (void)b;
    }
  uint32_t tB = micros() - t;

  /* C: 临时缓冲（draw_letter_normal 每个字都 get/release 一次） */
  t = micros();
  for (int i = 0; i < n; i++)
    for (int j = 0; j < M; j++) {
      void* p = lv_mem_buf_get(256);
      lv_mem_buf_release(p);
    }
  uint32_t tC = micros() - t;

  /* D: 4bpp 解包到 mask_buf（B 已含在内，差值是纯解包） */
  uint8_t* mask = (uint8_t*)lv_mem_buf_get(256);
  if (!mask) { Serial.println("[PerfLet] buf OOM"); return; }
  t = micros();
  for (int i = 0; i < n; i++)
    for (int j = 0; j < M; j++) {
      const uint8_t* map_p = lv_font_get_glyph_bitmap(&font_zh_16, CN[j]);
      if (!map_p) continue;
      int m = 0;
      for (int row = 0; row < 16; row++) {
        uint32_t bitmask = 0xF0;
        uint32_t col_bit = 0;
        for (int col = 0; col < 16; col++) {
          uint8_t px = (*map_p & bitmask) >> (4 - col_bit);
          mask[m] = bpp4[px];
          if (col_bit < 4) { col_bit += 4; bitmask = bitmask >> 4; }
          else { col_bit = 0; bitmask = 0xF0; map_p++; }
          m++;
        }
      }
    }
  uint32_t tD = micros() - t;
  lv_mem_buf_release(mask);

#define ROW(tag, us)                                                          \
  Serial.printf("[PerfLet] %-26s %8u us  %6u ns/char\n", tag,                 \
                (unsigned)(us), (unsigned)(((uint64_t)(us) * 1000) / total))
  ROW("A glyph_dsc (查找)", tA);
  ROW("B glyph_bitmap (指针)", tB);
  ROW("C mem_buf get+release", tC);
  ROW("D 解包4bpp(B含在内)", tD);
  Serial.printf("[PerfLet] D-B = 纯解包              %8u us  %6u ns/char\n",
                (unsigned)(tD - tB),
                (unsigned)(((uint64_t)(tD - tB) * 1000) / total));
  Serial.printf("[PerfLet] 实测整字成本 ~303000 ns/char，上面四项之和 = %u ns\n",
                (unsigned)(((uint64_t)(tA + tD + tC) * 1000) / total));
}
'''

ANCHOR_FUNC = 'static void cmdPerf(const char* arg) {'
ANCHOR_DISP = '  } else if (strcmp(cmd, "perftxt") == 0) {\r\n    cmdPerfTxt(arg);'
ANCHOR_HELP = '  Serial.println("perftxt [rows]      - 文字渲染微观基准：每字符 us（含同字对照判 cache miss）");'


def rep(s, old, new, tag):
    base = old.replace('\r\n', '\n')
    for o in (base.replace('\n', '\r\n'), base):
        if o in s:
            return s.replace(o, new.replace('\r\n', '\n').replace('\n', '\r\n'), 1)
    raise SystemExit('MISS: ' + tag)


def main():
    s = io.open(P, 'r', encoding='utf-8', newline='').read()
    if 'cmdPerfLet' in s:
        print('ALREADY_PRESENT')
        return
    s = rep(s, ANCHOR_FUNC, FUNC.strip('\r\n') + '\n\n' + ANCHOR_FUNC, 'func')
    s = rep(s, ANCHOR_DISP,
            ANCHOR_DISP + '\n  } else if (strcmp(cmd, "perflet") == 0) {\n    cmdPerfLet(arg);', 'disp')
    s = rep(s, ANCHOR_HELP,
            ANCHOR_HELP + '\n  Serial.println("perflet [n]         - 把画一个汉字拆成查找/位图/缓冲/解包逐项计时");', 'help')
    io.open(P, 'w', encoding='utf-8', newline='').write(s)
    print('INJECTED len=%d' % len(s))


if __name__ == '__main__':
    main()
