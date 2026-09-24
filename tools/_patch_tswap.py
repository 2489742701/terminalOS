# -*- coding: utf-8 -*-
"""给触摸加 X/Y 交换开关（tswap）。

tprobe 实测 X/Y_OUTPUT_MAX 都是 480，与屏幕一致 —— 端点范围不是根因。
剩下的可能只有三种：轴向翻转 / X-Y 交换 / 常量偏移。前两种都能用
tcal 的端点顺序 + tswap 现场试出来，**不用重新烧录**。
"""
import io
GT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"

# ── touch.h ────────────────────────────────────────────────────────────
p = GT + r"\src\hal\touch.h"
t = io.open(p, encoding="utf-8", newline=None).read()
old = "  static void setCal(int x0, int x1, int y0, int y1);"
new = ("  static void setCal(int x0, int x1, int y0, int y1);\n\n"
       "  /* X/Y 交换开关（面板贴反时裸 X 其实是屏幕 Y）。`tswap` 现场切，\n"
       "     配合 tcal 的端点顺序可以把翻转/交换/偏移三种情况全试一遍。 */\n"
       "  static void setSwap(bool on);")
assert t.count(old) == 1
io.open(p, "w", encoding="utf-8", newline="").write(t.replace(old, new))

# ── touch.cpp ──────────────────────────────────────────────────────────
p = GT + r"\src\hal\touch.cpp"
t = io.open(p, encoding="utf-8", newline=None).read()

old = """bool Touch::touched(int& x, int& y) {
  if (!ts) return false;
  ts->read();
  int rx, ry;
  if (!rawXY(rx, ry)) return false;
  if (s_rawX1 != s_rawX0) x = map(rx, s_rawX0, s_rawX1, 0, SCREEN_WIDTH - 1);
  else                    x = 0;
  if (s_rawY1 != s_rawY0) y = map(ry, s_rawY0, s_rawY1, 0, SCREEN_HEIGHT - 1);
  else                    y = 0;
  return true;
}"""
new = """static bool s_swapXY = false;

bool Touch::touched(int& x, int& y) {
  if (!ts) return false;
  ts->read();
  int rx, ry;
  if (!rawXY(rx, ry)) return false;
  int sx = (s_rawX1 != s_rawX0)
               ? map(rx, s_rawX0, s_rawX1, 0, SCREEN_WIDTH - 1) : 0;
  int sy = (s_rawY1 != s_rawY0)
               ? map(ry, s_rawY0, s_rawY1, 0, SCREEN_HEIGHT - 1) : 0;
  if (s_swapXY) { x = sy; y = sx; }
  else          { x = sx; y = sy; }
  return true;
}

void Touch::setSwap(bool on) {
  s_swapXY = on;
  Serial.printf("[Touch] swapXY = %d\\n", (int)on);
}"""
assert t.count(old) == 1
t = t.replace(old, new)

# dump 里也要跟着交换，否则打出来的屏幕坐标跟实际手感对不上
old = """      int sx = (s_rawX1 != s_rawX0)
                   ? map(rx, s_rawX0, s_rawX1, 0, SCREEN_WIDTH - 1) : 0;
      int sy = (s_rawY1 != s_rawY0)
                   ? map(ry, s_rawY0, s_rawY1, 0, SCREEN_HEIGHT - 1) : 0;
      Serial.printf("[Touch] raw=%5d,%5d -> screen=%4d,%4d\\n", rx, ry, sx, sy);"""
new = """      int sx = (s_rawX1 != s_rawX0)
                   ? map(rx, s_rawX0, s_rawX1, 0, SCREEN_WIDTH - 1) : 0;
      int sy = (s_rawY1 != s_rawY0)
                   ? map(ry, s_rawY0, s_rawY1, 0, SCREEN_HEIGHT - 1) : 0;
      int dx = s_swapXY ? sy : sx, dy = s_swapXY ? sx : sy;
      Serial.printf("[Touch] raw=%5d,%5d -> screen=%4d,%4d%s\\n",
                    rx, ry, dx, dy, s_swapXY ? " (swap)" : "");"""
assert t.count(old) == 1
t = t.replace(old, new)
io.open(p, "w", encoding="utf-8", newline="").write(t)

# ── serial_console ─────────────────────────────────────────────────────
p = GT + r"\src\hal\serial_console.cpp"
t = io.open(p, encoding="utf-8", newline=None).read()
old = """} else if (strcmp(cmd, "tprobe") == 0) {"""
new = """} else if (strcmp(cmd, "tswap") == 0) {
    /* X/Y 交换：tswap（切换）/ tswap on / tswap off */
    bool on = true;
    if (arg && *arg) on = !(strcmp(arg, "off") == 0 || strcmp(arg, "0") == 0);
    else             on = !s_touchSwap;
    s_touchSwap = on;
    Touch::setSwap(on);
} else if (strcmp(cmd, "tprobe") == 0) {"""
assert t.count(old) == 1
t = t.replace(old, new)

# s_touchSwap 必须放在文件作用域：插在 if-else 链中间会断链（语法错）
anchor = "/* 解析并执行一行命令 */\nstatic void executeLine(char* line) {"
assert t.count(anchor) == 1, "executeLine anchor not found"
t = t.replace(anchor,
              "/* 触摸 X/Y 交换开关的当前状态（`tswap` 不带参数时取反） */\n"
              "static bool s_touchSwap = false;\n\n" + anchor)

old = '  Serial.println("tprobe            - 读 GT911 配置的 X/Y_OUTPUT_MAX");'
new = ('  Serial.println("tprobe            - 读 GT911 配置的 X/Y_OUTPUT_MAX");\n'
       '  Serial.println("tswap [on|off]    - 触摸 X/Y 交换(面板贴反时试这个)");')
assert t.count(old) == 1
t = t.replace(old, new)
io.open(p, "w", encoding="utf-8", newline="").write(t)
print("tswap patched")
