# -*- coding: utf-8 -*-
"""批量补丁：
   1) LVGL: emoji 不画豆腐块（缺字形占位框）
   2) browser: g_content 关滚动条（默认 AUTO 会画一条白条）
   3) touch: 两点校准 + 裸坐标 dump + GT911 配置探测
   4) serial_console: traw / tcal / tprobe 三个命令 + help
"""
import io, os

GT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
LVGL = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\4.0inch_ESP32-4848S040\1-Demo\Demo_Arduino\Libraries\Lvgl"


def load(p, crlf):
    t = io.open(p, encoding="utf-8", newline=None).read()   # 统一成 \n
    return t, ("\r\n" if crlf else "\n")


def save(p, t, nl):
    if nl == "\r\n":
        t = t.replace("\n", "\r\n")
    io.open(p, "w", encoding="utf-8", newline="").write(t)


def rep(t, old, new, tag, cnt=1):
    assert t.count(old) == cnt, "%s: expected %d occurrence(s), got %d" % (tag, cnt, t.count(old))
    return t.replace(old, new)


# ══════════════════════════════════════════════════════════════════════
# 1) LVGL —— emoji 不画豆腐块
# ══════════════════════════════════════════════════════════════════════
p = os.path.join(LVGL, "src", "draw", "sw", "lv_draw_sw_letter.c")
t, nl = load(p, False)

OLD = """void lv_draw_sw_letter(lv_draw_ctx_t * draw_ctx, const lv_draw_label_dsc_t * dsc,  const lv_point_t * pos_p,
                       uint32_t letter)
{
    lv_font_glyph_dsc_t g;
    bool g_ret = lv_font_get_glyph_dsc(dsc->font, &g, letter, '\\0');
    if(g_ret == false) {"""
NEW = """/* geek-terminal 补丁 2026-09-24：emoji / 符号类码位**不画豆腐块**。
   本机只有一套中文点阵（font_zh_16，3927 字），没有 emoji 字形 ——
   LVGL 缺字形时会画一个 1px 边框的方框（下面的 placeholder），
   一个 emoji 就是一格豆腐，整行看着稀烂。
   这里对 emoji 区码位直接跳过（什么都不画）；
   中文缺字**仍然保留方框** —— 那是"字库里真没有"的信号，必须看得见。 */
static bool gt_is_emoji_codepoint(uint32_t c)
{
    if(c == 0xFE0F || c == 0xFE0E) return true;      /* 变体选择符 VS16/VS15 */
    if(c == 0x200D) return true;                     /* ZWJ（拼 emoji 用） */
    if(c >= 0x1F000 && c <= 0x1FAFF) return true;    /* 主 emoji 区 */
    if(c >= 0x1F1E6 && c <= 0x1F1FF) return true;    /* 区域指示符（国旗） */
    if(c >= 0x2600 && c <= 0x27BF) return true;      /* 杂项符号 / 装饰符号 */
    if(c >= 0x2B00 && c <= 0x2BFF) return true;      /* 杂项符号与箭头 */
    if(c >= 0x2190 && c <= 0x21FF) return true;      /* 箭头 */
    if(c >= 0x2700 && c <= 0x27BF) return true;
    return false;
}

void lv_draw_sw_letter(lv_draw_ctx_t * draw_ctx, const lv_draw_label_dsc_t * dsc,  const lv_point_t * pos_p,
                       uint32_t letter)
{
    lv_font_glyph_dsc_t g;
    bool g_ret = lv_font_get_glyph_dsc(dsc->font, &g, letter, '\\0');
    if(g_ret == false) {
        if(gt_is_emoji_codepoint(letter)) return;    /* emoji：静默跳过，不画豆腐 */
        {"""
t = rep(t, OLD, NEW, "lvgl-1")

# 给 placeholder 那段补一个右花括号（上面多开了一层 block）
OLD = """            draw_ctx->draw_rect(draw_ctx, &glyph_dsc, &glyph_coords);
        }
        return;
    }"""
NEW = """            draw_ctx->draw_rect(draw_ctx, &glyph_dsc, &glyph_coords);
        }
        }
        return;
    }"""
t = rep(t, OLD, NEW, "lvgl-2")
save(p, t, nl)
print("1) lv_draw_sw_letter.c patched")

# ══════════════════════════════════════════════════════════════════════
# 2) browser g_content 关滚动条
# ══════════════════════════════════════════════════════════════════════
p = os.path.join(GT, "src", "app", "browser_screen.cpp")
t, nl = load(p, False)
OLD = """  lv_obj_set_scroll_dir(g_content, LV_DIR_VER);"""
NEW = """  lv_obj_set_scroll_dir(g_content, LV_DIR_VER);
  /* ⚠️ 必须显式关滚动条：LVGL 默认 LV_SCROLLBAR_MODE_AUTO，会在可滚动时
     沿内容区边沿画一条滚动条。本工程没启用自定义 scrollbar 样式（暗色 UI），
     它用的是默认样式的浅色 —— 表现就是「滚动时边上一条白线」
     （docs/09 B3 记的那条）。desktop_screen.cpp 早已这么处理，浏览器这里漏了。 */
  lv_obj_set_scrollbar_mode(g_content, LV_SCROLLBAR_MODE_OFF);"""
t = rep(t, OLD, NEW, "browser-scrollbar")
save(p, t, nl)
print("2) browser_screen.cpp patched")

# ══════════════════════════════════════════════════════════════════════
# 3) touch —— 校准 / 裸坐标 / 探测
# ══════════════════════════════════════════════════════════════════════
p = os.path.join(GT, "src", "hal", "touch.h")
t, nl = load(p, True)
OLD = """  // 是否释放
  static bool released();

 private:"""
NEW = """  // 是否释放
  static bool released();

  /* 两点校准：把面板裸坐标 [x0,x1] / [y0,y1] 线性映射到屏幕 0..479。
     用串口 `traw` 量四角裸值，再用 `tcal x0 x1 y0 y1` 填。 */
  static void setCal(int x0, int x1, int y0, int y1);

  /* 当前按下的**面板裸坐标**（未做屏幕映射）。ts->read() 之后调用。 */
  static bool rawXY(int& rx, int& ry);

  /* 连续打印裸坐标 + 映射后屏幕坐标，用于量四角。ms=持续毫秒。 */
  static void dump(uint32_t ms);

  /* 读 GT911 配置寄存器里的 X/Y_OUTPUT_MAX（库里 readByteData 是 private，
     只能自己走 Wire）。若它不是 480，说明控制器输出范围与屏幕不符 ——
     那就是"触摸偏移"的根因。 */
  static void probe();

 private:"""
t = rep(t, OLD, NEW, "touch.h")
save(p, t, nl)

p = os.path.join(GT, "src", "hal", "touch.cpp")
t, nl = load(p, True)

OLD = """bool Touch::hasSignal() { return true; }

bool Touch::touched(int& x, int& y) {
  if (!ts) return false;
  ts->read();
  if (!ts->isTouched) return false;
  // GT911 坐标映射：480x480 -> 0..SCREEN
  x = map(ts->points[0].x, 480, 0, 0, SCREEN_WIDTH - 1);
  y = map(ts->points[0].y, 480, 0, 0, SCREEN_HEIGHT - 1);
  return true;
}

bool Touch::released() { return true; }"""
NEW = """bool Touch::hasSignal() { return true; }

/* ── 两点校准端点 ──────────────────────────────────────────────────────
 * 旧代码是写死的 map(points.x, 480, 0, 0, 479)：
 *   ⚠️ 库里 setRotation(ROTATION_NORMAL) 已经做过一次 x = width - x，
 *      这里又 map(480, 0, ...) 翻了一次 —— 两次对消，等于裸值 ×479/480。
 *   ⚠️ 更糟的是端点写死 480：若 GT911 配置里的 X/Y_OUTPUT_MAX 不是 480，
 *      裸值就根本不落在 0..480，于是整屏偏移/缩放都不对。
 * 现在改成可校准的两点线性映射。默认 0..480 是为了**逐字节复刻旧行为**
 * （= 裸值 ×479/480），不是实测值；真值用 `traw` 量四角后 `tcal` 填。
 * ⚠️ x1 != x0 / y1 != y0：map() 除零返回 INT32_MIN，坐标直接飞出屏幕。 */
static int s_rawX0 = 0, s_rawX1 = 480;
static int s_rawY0 = 0, s_rawY1 = 480;

bool Touch::rawXY(int& rx, int& ry) {
  if (!ts || !ts->isTouched) return false;
  /* 库里 ROTATION_NORMAL 已做 width-x / height-y，先反回裸值 */
  rx = (int)SCREEN_WIDTH  - (int)ts->points[0].x;
  ry = (int)SCREEN_HEIGHT - (int)ts->points[0].y;
  return true;
}

bool Touch::touched(int& x, int& y) {
  if (!ts) return false;
  ts->read();
  int rx, ry;
  if (!rawXY(rx, ry)) return false;
  if (s_rawX1 != s_rawX0) x = map(rx, s_rawX0, s_rawX1, 0, SCREEN_WIDTH - 1);
  else                    x = 0;
  if (s_rawY1 != s_rawY0) y = map(ry, s_rawY0, s_rawY1, 0, SCREEN_HEIGHT - 1);
  else                    y = 0;
  return true;
}

void Touch::setCal(int x0, int x1, int y0, int y1) {
  s_rawX0 = x0; s_rawX1 = x1;
  s_rawY0 = y0; s_rawY1 = y1;
  Serial.printf("[Touch] cal: rawX %d..%d -> 0..%d, rawY %d..%d -> 0..%d\\n",
                x0, x1, SCREEN_WIDTH - 1, y0, y1, SCREEN_HEIGHT - 1);
}

void Touch::dump(uint32_t ms) {
  if (!initialized && !begin()) return;
  Serial.printf("[Touch] dump %u ms —— 请依次点：左上 / 右上 / 右下 / 左下\\n",
                (unsigned)ms);
  uint32_t t0 = millis();
  int lastRx = -1, lastRy = -1;
  while (millis() - t0 < ms) {
    ts->read();
    int rx, ry;
    if (rawXY(rx, ry) && (rx != lastRx || ry != lastRy)) {
      int sx = (s_rawX1 != s_rawX0)
                   ? map(rx, s_rawX0, s_rawX1, 0, SCREEN_WIDTH - 1) : 0;
      int sy = (s_rawY1 != s_rawY0)
                   ? map(ry, s_rawY0, s_rawY1, 0, SCREEN_HEIGHT - 1) : 0;
      Serial.printf("[Touch] raw=%5d,%5d -> screen=%4d,%4d\\n", rx, ry, sx, sy);
      lastRx = rx; lastRy = ry;
    }
    delay(30);
  }
  Serial.println("[Touch] dump done");
}

void Touch::probe() {
  /* GT911 配置寄存器：0x8047=版本号, 0x8048/49=X_OUTPUT_MAX, 0x804A/4B=Y_OUTPUT_MAX。
     库里 readByteData 是 private，只能自己走一遍 Wire。addr 用库默认的 0x5D。 */
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  auto rd = [](uint16_t reg) -> int {
    Wire.beginTransmission((uint8_t)0x5D);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)0x5D, (uint8_t)1);
    return (int)Wire.read();
  };
  int ver = rd(0x8047);
  int xmax = rd(0x8048) + (rd(0x8049) << 8);
  int ymax = rd(0x804A) + (rd(0x804B) << 8);
  Serial.printf("[Touch] GT911 cfg: ver=0x%02X X_OUTPUT_MAX=%d Y_OUTPUT_MAX=%d"
                " (屏幕 %dx%d；两者不一致 => 裸值不落在 0..%d，就是偏移根因)\\n",
                ver, xmax, ymax, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH);
}

bool Touch::released() { return true; }"""
t = rep(t, OLD, NEW, "touch.cpp")
save(p, t, nl)
print("3) touch patched")

# ══════════════════════════════════════════════════════════════════════
# 4) serial_console —— traw / tcal / tprobe
# ══════════════════════════════════════════════════════════════════════
p = os.path.join(GT, "src", "hal", "serial_console.cpp")
t, nl = load(p, False)

OLD = """} else if (strcmp(cmd, "flatdump") == 0) {"""
NEW = """} else if (strcmp(cmd, "traw") == 0) {
    /* 触摸裸坐标 dump：traw（默认 10 秒） / traw 20
       用途：量四角裸值 -> 填 tcal。诊断"触摸偏移"的唯一可信手段。 */
    uint32_t ms = (arg && *arg) ? (uint32_t)atoi(arg) : 10;
    if (ms == 0) ms = 10;
    Touch::dump(ms * 1000);
} else if (strcmp(cmd, "tcal") == 0) {
    /* 触摸两点校准：tcal <rawX0> <rawX1> <rawY0> <rawY1>
       例：四角裸值 X 20..460、Y 15..455 -> tcal 20 460 15 455
       不带参数 = 恢复默认 0 480 0 480。 */
    int a0 = 0, a1 = 480, b0 = 0, b1 = 480;
    if (arg && *arg) {
      if (sscanf(arg, "%d %d %d %d", &a0, &a1, &b0, &b1) != 4) {
        Serial.println("[Console] tcal 需要 4 个数：tcal x0 x1 y0 y1");
        return;
      }
    }
    Touch::setCal(a0, a1, b0, b1);
} else if (strcmp(cmd, "tprobe") == 0) {
    /* 读 GT911 配置里的 X/Y_OUTPUT_MAX —— 不是 480 就是偏移根因 */
    Touch::probe();
} else if (strcmp(cmd, "flatdump") == 0) {"""
t = rep(t, OLD, NEW, "console-cmds")

OLD = """  Serial.println("sdbench [KB]      - TF 卡读写速度实测, 默认 256KB");"""
NEW = """  Serial.println("sdbench [KB]      - TF 卡读写速度实测, 默认 256KB");
  Serial.println("traw [s]          - 触摸裸坐标 dump(量四角, 诊断偏移)");
  Serial.println("tcal x0 x1 y0 y1  - 触摸两点校准(默认 0 480 0 480)");
  Serial.println("tprobe            - 读 GT911 配置的 X/Y_OUTPUT_MAX");"""
t = rep(t, OLD, NEW, "console-help")
save(p, t, nl)
print("4) serial_console.cpp patched")
print("ALL OK")
