# -*- coding: utf-8 -*-
import io

# ── screensaver.h：公开 setter / getter ────────────────────────────────────
p = r"src/app/screensaver.h"
t = io.open(p, encoding="utf-8", newline="").read()
old = """  static void sleepNow();
"""
new = """  static void sleepNow();

  /* 自动息屏：无操作多久进 DIM。**0 = 永不息屏**。
     原先是编译期常量（300000），设置页够不着 —— 改成运行时变量才有「自动息屏」这一项。 */
  static void setIdleTimeout(unsigned long ms);
  static unsigned long idleTimeout();
"""
assert t.count(old) == 1, t.count(old)
t = t.replace(old, new)
io.open(p, "w", encoding="utf-8", newline="").write(t)
print("screensaver.h patched")

# ── screensaver.cpp：常量 -> 变量 + 0 表示永不 + 定义两个函数 ──────────────
p = r"src/app/screensaver.cpp"
t = io.open(p, encoding="utf-8", newline="").read()

old = "static const unsigned long ACTIVE_TIMEOUT_MS   = 300000;  // 5min 无操作 -> DIM（测试用，原 30s）"
new = "static unsigned long       ACTIVE_TIMEOUT_MS   = 300000;  // 无操作 -> DIM；**0 = 永不息屏**（设置页可改）"
assert t.count(old) == 1, t.count(old)
t = t.replace(old, new)

old = "    if (now - lastActivityMs > ACTIVE_TIMEOUT_MS) enterDim(true);"
new = "    if (ACTIVE_TIMEOUT_MS > 0 && now - lastActivityMs > ACTIVE_TIMEOUT_MS) enterDim(true);"
assert t.count(old) == 1, t.count(old)
t = t.replace(old, new)

old = "void ScreenSaver::sleepNow() {"
new = """void ScreenSaver::setIdleTimeout(unsigned long ms) {
  ACTIVE_TIMEOUT_MS = ms;
}

unsigned long ScreenSaver::idleTimeout() {
  return ACTIVE_TIMEOUT_MS;
}

void ScreenSaver::sleepNow() {"""
assert t.count(old) == 1, t.count(old)
t = t.replace(old, new)

io.open(p, "w", encoding="utf-8", newline="").write(t)
print("screensaver.cpp patched")
