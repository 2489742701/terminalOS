# -*- coding: utf-8 -*-
"""Touch 增加：flipX / flipY / swapXY / resetCal / getCal。

触摸测试屏要用它们做"点了立即生效"的开关 —— 不重烧就能把
翻转 / X-Y 交换 / 重置 三种情况全试一遍。
"""
import io
GT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"


def load(rel):
    p = GT + "\\" + rel
    return io.open(p, encoding="utf-8", newline=None).read(), p


def save(p, t, crlf):
    if crlf:
        t = t.replace("\n", "\r\n")
    io.open(p, "w", encoding="utf-8", newline="").write(t)


# ── touch.h ────────────────────────────────────────────────────────────
t, p = load(r"src\hal\touch.h")
old = """  /* X/Y 交换开关（面板贴反时裸 X 其实是屏幕 Y）。`tswap` 现场切，
     配合 tcal 的端点顺序可以把翻转/交换/偏移三种情况全试一遍。 */
  static void setSwap(bool on);"""
new = """  /* X/Y 交换开关（面板贴反时裸 X 其实是屏幕 Y）。`tswap` 现场切，
     配合 tcal 的端点顺序可以把翻转/交换/偏移三种情况全试一遍。 */
  static void setSwap(bool on);

  /* 触摸测试屏用的现场开关 —— 点了立即生效，不落盘、不用重烧。
     flipX/flipY 就是交换该轴的两个端点（等价于把轴翻转过来）。 */
  static void flipX();
  static void flipY();
  static void swapXY();
  static void resetCal();

  /* 读回当前校准参数，给 UI 显示用 */
  static void getCal(int& x0, int& x1, int& y0, int& y1, bool& swap);"""
assert t.count(old) == 1
save(p, t.replace(old, new), True)
print("patched touch.h")

# ── touch.cpp ──────────────────────────────────────────────────────────
t, p = load(r"src\hal\touch.cpp")
old = """void Touch::setSwap(bool on) {
  s_swapXY = on;
  Serial.printf("[Touch] swapXY = %d\\n", (int)on);
}"""
new = """void Touch::setSwap(bool on) {
  s_swapXY = on;
  Serial.printf("[Touch] swapXY = %d\\n", (int)on);
}

void Touch::flipX() {
  int t0 = s_rawX0;
  s_rawX0 = s_rawX1;
  s_rawX1 = t0;
  Serial.printf("[Touch] flipX -> X %d..%d\\n", s_rawX0, s_rawX1);
}

void Touch::flipY() {
  int t0 = s_rawY0;
  s_rawY0 = s_rawY1;
  s_rawY1 = t0;
  Serial.printf("[Touch] flipY -> Y %d..%d\\n", s_rawY0, s_rawY1);
}

void Touch::swapXY() {
  s_swapXY = !s_swapXY;
  Serial.printf("[Touch] swapXY -> %d\\n", (int)s_swapXY);
}

void Touch::resetCal() {
  s_rawX0 = 0;   s_rawX1 = 480;
  s_rawY0 = 0;   s_rawY1 = 480;
  s_swapXY = false;
  Serial.println("[Touch] cal reset to default 0..480 / 0..480");
}

void Touch::getCal(int& x0, int& x1, int& y0, int& y1, bool& swap) {
  x0 = s_rawX0; x1 = s_rawX1;
  y0 = s_rawY0; y1 = s_rawY1;
  swap = s_swapXY;
}"""
assert t.count(old) == 1
save(p, t.replace(old, new), True)
print("patched touch.cpp")
print("ALL OK")
