"""
add_perfx.py - 加 `perfx` 命令：按控件类型隐藏，用差值反推 draw 花在哪

用法：
  perfx hide label|img|canvas|btn|cont    隐藏该类控件
  perfx show                              全部恢复
  然后跑 perf 30，跟隐藏前的数字比，差值就是那类控件的绘制成本。

背景：砍掉 flush 的 25ms 之后，draw 成了整帧 98% 的开销（桌面 43ms）。
纯写缓冲实测只要 1.2ms/屏（内部 DRAM 188 MB/s），说明 draw 贵在
"每个像素上的计算"而不是"往缓冲里写"。到底是圆角遮罩、文字、还是图标，
必须用差分测出来，不能靠猜。
"""
import io, os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
P = os.path.join(ROOT, 'src', 'hal', 'serial_console.cpp')

raw = io.open(P, 'r', encoding='utf-8', newline='').read()
crlf = '\r\n' in raw
s = raw.replace('\r\n', '\n')

assert 'cmdPerfx' not in s, 'already injected'

FN = r'''/* ---- perfx：按控件类型隐藏，用差分反推 draw 花在哪 ----
   砍掉 flush 的 25ms 之后 draw 占了整帧 98%（桌面 43ms），而纯写缓冲实测
   只要 1.2ms/屏 —— 说明 draw 贵在每个像素上的计算（圆角遮罩 / 文字 /
   图标 blit），不是贵在写内存。到底是哪一类，必须用差分测，不能猜。 */
static int walkSetHidden(lv_obj_t* o, const lv_obj_class_t* cls, bool hide) {
  int n = 0;
  uint32_t cnt = lv_obj_get_child_cnt(o);
  for (uint32_t i = 0; i < cnt; i++) {
    lv_obj_t* c = lv_obj_get_child(o, i);
    if (!c) continue;
    if (lv_obj_check_type(c, cls)) {
      if (hide) lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_clear_flag(c, LV_OBJ_FLAG_HIDDEN);
      n++;
    }
    n += walkSetHidden(c, cls, hide);
  }
  return n;
}

static void cmdPerfx(const char* arg) {
  lv_obj_t* scr = lv_scr_act();
  if (!scr) { Serial.println("[Perfx] no screen"); return; }

  const char* a = arg ? arg : "";
  if (strncmp(a, "show", 4) == 0) {
    int n = 0;
    n += walkSetHidden(scr, &lv_label_class, false);
    n += walkSetHidden(scr, &lv_img_class, false);
#if LV_USE_CANVAS
    n += walkSetHidden(scr, &lv_canvas_class, false);
#endif
    Serial.printf("[Perfx] restored %d objects\n", n);
    return;
  }

  const char* kind = a;
  if (strncmp(a, "hide ", 5) == 0) kind = a + 5;
  else if (strncmp(a, "hide", 4) == 0) kind = a + 4;

  const lv_obj_class_t* cls = nullptr;
  if (strcmp(kind, "label") == 0) cls = &lv_label_class;
  else if (strcmp(kind, "img") == 0) cls = &lv_img_class;
#if LV_USE_CANVAS
  else if (strcmp(kind, "canvas") == 0) cls = &lv_canvas_class;
#endif
  if (!cls) {
    Serial.println("[Perfx] usage: perfx hide label|img|canvas | perfx show");
    return;
  }
  int n = walkSetHidden(scr, cls, true);
  Serial.printf("[Perfx] hid %d '%s' objects -> now run 'perf' and diff\n", n, kind);
}

'''

anchor = 'static void cmdPerf(const char* arg) {'
assert anchor in s, 'cmdPerf anchor'
s = s.replace(anchor, FN + anchor, 1)

d_old = '  } else if (strcmp(cmd, "perfbw") == 0) {\n    cmdPerfBW(arg);'
d_new = d_old + '\n  } else if (strcmp(cmd, "perfx") == 0) {\n    cmdPerfx(arg);'
assert d_old in s, 'dispatch anchor'
s = s.replace(d_old, d_new, 1)

h_old = '  Serial.println("perfbw            - bandwidth probe: 定位 flush 那 26ms 到底是谁吃的");'
assert h_old in s, 'help anchor'
s = s.replace(h_old,
              h_old + '\n  Serial.println("perfx hide <label|img|canvas> - 隐藏该类控件，配合 perf 做差分定位 draw 开销");',
              1)

if crlf:
    s = s.replace('\n', '\r\n')
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('WROTE ' + P)
