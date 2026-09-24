"""两个诊断增强：

1) perf 支持第二参数 region 高度：perf 30 120 = 只 invalidate 顶部 120 行。
   之前 perf 永远全屏 invalidate，测出来的是"最坏情况"。真实交互（点一个 tile、
   滚一小段）只脏一小块，帧时间天差地别 —— 必须能量到。

2) perftxt 加"同字重复"对照组：rows 行全部放同一个汉字。
   若同字明显快于 20 个不同字 ⟹ 瓶颈是字形数据的 flash XIP cache miss
   （font_zh_16 有 3785 个字形 ≈ 484KB，远大于 S3 的 flash cache）。
   若两者一样快 ⟹ 瓶颈在每像素混合，跟 cache 无关。
"""
import io, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
P = os.path.join(ROOT, 'src', 'hal', 'serial_console.cpp')

OLD_RUN = '''static uint32_t perf_run(lv_disp_t* disp, lv_obj_t* scr, int rounds) {
  lv_obj_invalidate(scr);
  lv_refr_now(disp);            /* 丢掉第一帧（冷启动），避免拉偏平均值 */
  uint32_t t0 = micros();
  for (int i = 0; i < rounds; i++) {
    lv_obj_invalidate(scr);
    lv_refr_now(disp);
  }
  return micros() - t0;
}'''

NEW_RUN = '''static uint32_t perf_run(lv_disp_t* disp, lv_obj_t* scr, int rounds, int regionH) {
  /* regionH<=0 或 >=480 表示整屏；否则只脏顶部 regionH 行。
     真实交互（点一个 tile、滚一小段）只脏一小块，跟全屏差一个数量级。 */
  bool partial = (regionH > 0 && regionH < 480);
  lv_area_t a;
  a.x1 = 0; a.y1 = 0; a.x2 = 479; a.y2 = partial ? (lv_coord_t)(regionH - 1) : 479;

  if (partial) lv_obj_invalidate_area(scr, &a); else lv_obj_invalidate(scr);
  lv_refr_now(disp);            /* 丢掉第一帧（冷启动），避免拉偏平均值 */
  uint32_t t0 = micros();
  for (int i = 0; i < rounds; i++) {
    if (partial) lv_obj_invalidate_area(scr, &a); else lv_obj_invalidate(scr);
    lv_refr_now(disp);
  }
  return micros() - t0;
}'''

OLD_ARGS = '''static void cmdPerf(const char* arg) {
  int rounds = (arg && *arg) ? atoi(arg) : 0;
  if (rounds <= 0) rounds = 20;
  if (rounds > 200) rounds = 200;

  lv_disp_t* disp = lv_disp_get_default();
  lv_obj_t* scr = lv_scr_act();
  if (!disp || !scr || !disp->driver) { Serial.println("[Perf] no display"); return; }

  uint32_t tAll = perf_run(disp, scr, rounds);

  /* LVGL 8.3 没有导出 lv_disp_flush_cb_t 这个 typedef，用 auto 接住原始
     函数指针，别再自己猜类型名了。 */
  auto realFlush = disp->driver->flush_cb;
  disp->driver->flush_cb = dummy_flush_cb;
  uint32_t tDraw = perf_run(disp, scr, rounds);
  disp->driver->flush_cb = realFlush;'''

NEW_ARGS = '''static void cmdPerf(const char* arg) {
  int rounds = (arg && *arg) ? atoi(arg) : 0;
  if (rounds <= 0) rounds = 20;
  if (rounds > 200) rounds = 200;
  /* 第二参数：只脏顶部 N 行（局部刷新）。不传 = 整屏。 */
  int regionH = 0;
  if (arg) {
    const char* sp = strchr(arg, ' ');
    if (sp) regionH = atoi(sp + 1);
  }

  lv_disp_t* disp = lv_disp_get_default();
  lv_obj_t* scr = lv_scr_act();
  if (!disp || !scr || !disp->driver) { Serial.println("[Perf] no display"); return; }

  uint32_t tAll = perf_run(disp, scr, rounds, regionH);

  /* LVGL 8.3 没有导出 lv_disp_flush_cb_t 这个 typedef，用 auto 接住原始
     函数指针，别再自己猜类型名了。 */
  auto realFlush = disp->driver->flush_cb;
  disp->driver->flush_cb = dummy_flush_cb;
  uint32_t tDraw = perf_run(disp, scr, rounds, regionH);
  disp->driver->flush_cb = realFlush;'''

OLD_HDR = '''  Serial.printf("[Perf] x%d  total=%u us/frame (%.1f fps max)\\n", rounds,
                (unsigned)perAll, perAll ? (1000000.0 / (double)perAll) : 0.0);'''
NEW_HDR = '''  const char* rg = (regionH > 0 && regionH < 480) ? "partial" : "full";
  Serial.printf("[Perf] x%d %s(%d rows)  total=%u us/frame (%.1f fps max)\\n", rounds, rg,
                (regionH > 0 && regionH < 480) ? regionH : 480,
                (unsigned)perAll, perAll ? (1000000.0 / (double)perAll) : 0.0);'''

# ---- perftxt：同字对照 ----
OLD_CN = '''  uint32_t t2 = txtBench(4);
  for (int i = 0; i < rows; i++) lv_obj_del(labs[i]);'''
NEW_CN = '''  uint32_t t2 = txtBench(4);
  for (int i = 0; i < rows; i++) lv_obj_del(labs[i]);

  /* --- 中文「同一个字」重复 20 遍 ---
     对照实验：若明显快于 20 个不同字 ⟹ 瓶颈是字形数据的 flash XIP cache miss
     （font_zh_16 有 3785 个字形 ~484KB，远大于 S3 的 flash cache，随机命中率极低）。
     若两者一样快 ⟹ 瓶颈在每像素混合，跟 cache 无关，减 bpp 才是正解。 */
  for (int i = 0; i < rows; i++) {
    labs[i] = lv_label_create(ov);
    lv_obj_set_pos(labs[i], 6, 6 + i * 22);
    lv_obj_set_style_text_font(labs[i], &font_zh_16, 0);
    lv_obj_set_style_text_color(labs[i], lv_color_white(), 0);
    lv_label_set_text(labs[i], CN_SAME);
  }
  uint32_t t5 = txtBench(4);
  for (int i = 0; i < rows; i++) lv_obj_del(labs[i]);'''

OLD_DECL = '''  const int CN_N = 20, EN_N = 30;'''
NEW_DECL = '''  /* 同一个汉字「极」重复 20 遍 —— 与 CN 只差"是否命中 cache" */
  const char* CN_SAME =
      "\\xe6\\x9e\\x81\\xe6\\x9e\\x81\\xe6\\x9e\\x81\\xe6\\x9e\\x81\\xe6\\x9e\\x81"
      "\\xe6\\x9e\\x81\\xe6\\x9e\\x81\\xe6\\x9e\\x81\\xe6\\x9e\\x81\\xe6\\x9e\\x81"
      "\\xe6\\x9e\\x81\\xe6\\x9e\\x81\\xe6\\x9e\\x81\\xe6\\x9e\\x81\\xe6\\x9e\\x81"
      "\\xe6\\x9e\\x81\\xe6\\x9e\\x81\\xe6\\x9e\\x81\\xe6\\x9e\\x81\\xe6\\x9e\\x81";
  const int CN_N = 20, EN_N = 30;'''

OLD_OUT = '''  Serial.printf("[PerfTxt] per-char  CN=%u us  EN=%u us   label-fixed=%u us\\n",'''
NEW_OUT = '''  Serial.printf("[PerfTxt] %-22s %6u  (+%u)   <-- 同字对照\\n", "+CN_SAME 20x%d",
                (unsigned)t5, (unsigned)(t5 - t1), rows);
  Serial.printf("[PerfTxt] per-char  CN=%u us  CN_SAME=%u us  EN=%u us   label-fixed=%u us\\n",
                (unsigned)((cnCost - fixCost) / (rows * CN_N)),
                (unsigned)(((t5 - t1) - fixCost) / (rows * CN_N)),'''
OLD_OUT2 = '''                (unsigned)((enCost - fixCost) / (rows * EN_N)),
                (unsigned)(fixCost / rows));'''
NEW_OUT2 = '''                (unsigned)((enCost - fixCost) / (rows * EN_N)),
                (unsigned)(fixCost / rows));'''

OLD_HELP2 = '  Serial.println("perftxt [rows]      - 文字渲染微观基准：每字符 us，拆开光栅化/查找/固定开销");'
NEW_HELP2 = ('  Serial.println("perftxt [rows]      - 文字渲染微观基准：每字符 us（含同字对照判 cache miss）");\n'
             '  Serial.println("perf [n] [h]        - h=只脏顶部 h 行（局部刷新），默认 480=整屏");')


def crlf(t):
    return t.replace('\r\n', '\n').replace('\n', '\r\n')


def rep(s, old, new, tag):
    """同一个 .cpp 里 CRLF(原有代码) 和 LF(后来脚本注入的代码) 混着，
    两种都试一遍再报错。"""
    base = old.replace('\r\n', '\n')
    for o in (crlf(base), base):
        if o in s:
            return s.replace(o, crlf(new.replace('\r\n', '\n')), 1)
    raise SystemExit('MISS: ' + tag)


def main():
    s = io.open(P, 'r', encoding='utf-8', newline='').read()
    if 'perf_run(lv_disp_t* disp, lv_obj_t* scr, int rounds, int regionH)' in s:
        print('ALREADY_PRESENT')
        return
    s = rep(s, OLD_RUN, NEW_RUN, 'perf_run')
    s = rep(s, OLD_ARGS, NEW_ARGS, 'cmdPerf args')
    s = rep(s, OLD_HDR, NEW_HDR, 'perf header')
    s = rep(s, OLD_DECL, NEW_DECL, 'CN_SAME decl')
    s = rep(s, OLD_CN, NEW_CN, 'CN_SAME bench')
    s = rep(s, OLD_OUT, NEW_OUT, 'perftxt out')
    s = rep(s, OLD_HELP2, NEW_HELP2, 'help')
    io.open(P, 'w', encoding='utf-8', newline='').write(s)
    print('INJECTED len=%d' % len(s))


if __name__ == '__main__':
    main()
