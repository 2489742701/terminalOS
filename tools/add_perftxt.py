"""注入 perftxt：文字渲染微观基准。

perfx 的差分只告诉我们"文字占 draw 的 59%"，但不知道钱花在
  (a) 每个字符的光栅化混合  (b) UTF-8 解码+字形查找  (c) label 自身固定开销
哪一头上。perftxt 用同一块 overlay 逐步加料测 delta，把三项拆开。
"""
import io, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
P = os.path.join(ROOT, 'src', 'hal', 'serial_console.cpp')

FUNC = r'''
/* ------------------------------------------------------------------ *
 * perftxt —— 文字渲染微观基准
 *
 * perfx 的差分只证明了"文字占 draw 的 59%"，但那 25ms 可能花在三处之一：
 *   (a) 每字符的光栅化 + alpha 混合（4bpp 抗锯齿）
 *   (b) UTF-8 解码 + 字形查找（3785 字的 sparse 字体）
 *   (c) label 自身的固定开销（布局 / 长文本换行计算）
 * 做法：在当前屏上盖一层不透明 overlay，逐步往里加料测 delta，
 * 原屏的成本是常量，会被减掉。
 * ------------------------------------------------------------------ */
static uint32_t txtBench(int n) {
  lv_disp_t* disp = lv_disp_get_default();
  uint32_t best = 0xFFFFFFFFu;
  for (int i = 0; i < n; i++) {
    lv_obj_update_layout(lv_scr_act());   /* 刚创建的 widget 坐标是陈旧的 */
    lv_obj_invalidate(lv_scr_act());
    uint32_t t0 = micros();
    lv_refr_now(disp);
    uint32_t d = micros() - t0;
    if (d < best) best = d;
  }
  return best;
}

static void cmdPerfTxt(const char* arg) {
  int rows = atoi(arg);
  if (rows < 1) rows = 8;
  lv_obj_t* scr = lv_scr_act();

  /* 20 个汉字 / 30 个 ASCII */
  const char* CN = "\xe6\x9e\x81\xe5\xae\xa2\xe7\xbb\x88\xe7\xab\xaf"      /* 极客终端 */
                   "\xe6\x96\x87\xe5\xad\x97\xe6\xb8\xb2\xe6\x9f\x93"      /* 文字渲染 */
                   "\xe5\x9f\xba\xe5\x87\x86\xe6\xb5\x8b\xe8\xaf\x95"      /* 基准测试 */
                   "\xe6\x96\x87\xe6\x9c\xac\xe4\xb8\x80\xe4\xba\x8c"      /* 文本一二 */
                   "\xe4\xb8\x89\xe5\x9b\x9b\xe4\xba\x94\xe5\x85\xad";     /* 三四五六 */
  const char* EN = "PerfTxtBenchMarkText0123456789Ab";
  const int CN_N = 20, EN_N = 30;

  Serial.printf("[PerfTxt] rows=%d  scr=%p\n", rows, scr);
  uint32_t t0 = txtBench(4);

  lv_obj_t* ov = lv_obj_create(scr);
  lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(ov, 480, 480);
  lv_obj_set_pos(ov, 0, 0);
  lv_obj_set_style_bg_color(ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ov, 0, 0);
  lv_obj_set_style_pad_all(ov, 0, 0);
  lv_obj_set_style_radius(ov, 0, 0);
  uint32_t t1 = txtBench(4);

  /* --- 中文 font_zh_16 --- */
  lv_obj_t** labs = (lv_obj_t**)malloc(sizeof(lv_obj_t*) * rows);
  if (!labs) { lv_obj_del(ov); Serial.println("[PerfTxt] OOM"); return; }
  for (int i = 0; i < rows; i++) {
    labs[i] = lv_label_create(ov);
    lv_obj_set_pos(labs[i], 6, 6 + i * 22);
    lv_obj_set_style_text_font(labs[i], &font_zh_16, 0);
    lv_obj_set_style_text_color(labs[i], lv_color_white(), 0);
    lv_label_set_text(labs[i], CN);
  }
  uint32_t t2 = txtBench(4);
  for (int i = 0; i < rows; i++) lv_obj_del(labs[i]);

  /* --- 英文 montserrat_16（同样 30 字符，4bpp 抗锯齿） --- */
  for (int i = 0; i < rows; i++) {
    labs[i] = lv_label_create(ov);
    lv_obj_set_pos(labs[i], 6, 6 + i * 22);
    lv_obj_set_style_text_font(labs[i], &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(labs[i], lv_color_white(), 0);
    lv_label_set_text(labs[i], EN);
  }
  uint32_t t3 = txtBench(4);
  for (int i = 0; i < rows; i++) lv_obj_del(labs[i]);

  /* --- 空文本：隔离 label 自身固定开销 --- */
  for (int i = 0; i < rows; i++) {
    labs[i] = lv_label_create(ov);
    lv_obj_set_pos(labs[i], 6, 6 + i * 22);
    lv_obj_set_style_text_font(labs[i], &font_zh_16, 0);
    lv_label_set_text(labs[i], "");
  }
  uint32_t t4 = txtBench(4);
  for (int i = 0; i < rows; i++) lv_obj_del(labs[i]);
  free(labs);
  lv_obj_del(ov);

  uint32_t cnCost = t2 - t1;
  uint32_t enCost = t3 - t1;
  uint32_t fixCost = t4 - t1;
  Serial.println("[PerfTxt] === us/frame (min of 4) ===");
  Serial.printf("[PerfTxt] %-22s %6u\n", "orig screen", (unsigned)t0);
  Serial.printf("[PerfTxt] %-22s %6u  (overlay cost %u)\n", "+empty overlay",
                (unsigned)t1, (unsigned)(t1 - t0));
  Serial.printf("[PerfTxt] %-22s %6u  (+%u)\n", "+CN 20x%d font_zh_16",
                (unsigned)t2, (unsigned)cnCost, rows);
  Serial.printf("[PerfTxt] %-22s %6u  (+%u)\n", "+EN 30x%d montserrat16",
                (unsigned)t3, (unsigned)enCost, rows);
  Serial.printf("[PerfTxt] %-22s %6u  (+%u)\n", "+empty label",
                (unsigned)t4, (unsigned)fixCost);
  Serial.printf("[PerfTxt] per-char  CN=%u us  EN=%u us   label-fixed=%u us\n",
                (unsigned)((cnCost - fixCost) / (rows * CN_N)),
                (unsigned)((enCost - fixCost) / (rows * EN_N)),
                (unsigned)(fixCost / rows));
}
'''

ANCHOR_FUNC = 'static void cmdPerf(const char* arg) {'
ANCHOR_DISP = '  } else if (strcmp(cmd, "perfx") == 0) {\r\n    cmdPerfx(arg);'
ANCHOR_HELP = '  Serial.println("perfx hide <label|img|canvas> - 隐藏该类控件，配合 perf 做差分定位 draw 开销");'


def main():
    s = io.open(P, 'r', encoding='utf-8', newline='').read()
    if 'cmdPerfTxt' in s:
        print('ALREADY_PRESENT')
        return
    for a in (ANCHOR_FUNC, ANCHOR_DISP, ANCHOR_HELP):
        if a not in s:
            raise SystemExit('anchor missing: %r' % a[:50])

    s = s.replace(ANCHOR_FUNC, FUNC.strip('\r\n') + '\r\n\r\n' + ANCHOR_FUNC, 1)
    s = s.replace(ANCHOR_DISP,
                  ANCHOR_DISP + '\r\n  } else if (strcmp(cmd, "perftxt") == 0) {\r\n    cmdPerfTxt(arg);',
                  1)
    s = s.replace(ANCHOR_HELP,
                  ANCHOR_HELP + '\r\n  Serial.println("perftxt [rows]      - 文字渲染微观基准：每字符 us，拆开光栅化/查找/固定开销");',
                  1)
    # 字体外部声明
    if 'extern lv_font_t font_zh_16' not in s:
        s = s.replace('#include "src/core/lv_refr.h"',
                      '#include "src/core/lv_refr.h"\r\n#include "app/font_zh.h"', 1)
    io.open(P, 'w', encoding='utf-8', newline='').write(s)
    print('INJECTED  len=%d' % len(s))


if __name__ == '__main__':
    main()
