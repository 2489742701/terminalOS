"""新增 Icon::Download（向下箭头 + 底线：把网页存下来）。"""
import io

H = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\icons.h"
C = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\icons.cpp"

log = []

# ---- header ----
h = io.open(H, encoding="utf-8", newline="").read()
if "Download," in h:
    log.append("hdr: ALREADY")
else:
    A = "  More,       // 三个点（更多）"
    if A not in h:
        log.append("hdr: *** ANCHOR NOT FOUND ***")
    else:
        h = h.replace(A, A + "\n  Download,   // 向下箭头 + 底线（把当前页存下来）", 1)
        io.open(H, "w", encoding="utf-8", newline="").write(h)
        log.append("hdr: patched")
log.append("hdr verify: %s" % ("Download," in io.open(H, encoding="utf-8").read()))

# ---- cpp ----
c = io.open(C, encoding="utf-8", newline="").read()
if "case Icon::Download" in c:
    log.append("cpp: ALREADY")
else:
    A = "    case Icon::More: {"
    NEW = """    case Icon::Download: {
      /* 下载：向下箭头 + 底线。竖杆从上到下，末端两只箭头翼，底部一条横线（托盘）。 */
      const int top = (int)(S * 0.26f);
      const int bot = (int)(S * 0.68f);
      lv_point_t stem[2] = {{cx, top}, {cx, bot}};
      lv_canvas_draw_line(canvas, stem, 2, &ld);
      const int head = (int)(S * 0.22f);
      lv_point_t wing[3] = {{(lv_coord_t)(cx - head), (lv_coord_t)(bot - head)},
                            {(lv_coord_t)cx, (lv_coord_t)bot},
                            {(lv_coord_t)(cx + head), (lv_coord_t)(bot - head)}};
      lv_canvas_draw_line(canvas, wing, 3, &ld);
      const int y0 = (int)(S * 0.78f);
      lv_point_t base[2] = {{(lv_coord_t)(S * 0.22f), (lv_coord_t)y0},
                            {(lv_coord_t)(S * 0.78f), (lv_coord_t)y0}};
      lv_canvas_draw_line(canvas, base, 2, &ld);
      break;
    }
"""
    if A not in c:
        log.append("cpp: *** ANCHOR NOT FOUND ***")
    else:
        c = c.replace(A, NEW + A, 1)
        io.open(C, "w", encoding="utf-8", newline="").write(c)
        log.append("cpp: patched")
log.append("cpp verify: %s" % ("case Icon::Download" in io.open(C, encoding="utf-8").read()))

print("\n".join(log))
