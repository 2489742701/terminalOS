# -*- coding: utf-8 -*-
"""分段渲染：串口诊断命令 seg（2026-09-25）"""
import io, sys

ROOT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
H = ROOT + r"\src\app\browser_screen.h"
C = ROOT + r"\src\app\browser_screen.cpp"
S = ROOT + r"\src\hal\serial_console.cpp"


def patch(path, old, new, tag, must=True):
    s = io.open(path, encoding="utf-8", newline="").read()
    if old not in s:
        print(("MISS: " if must else "SKIP: ") + tag)
        return None if must else s
    if s.count(old) != 1:
        print("DUP(%d): %s" % (s.count(old), tag)); sys.exit(1)
    print("OK: " + tag)
    return s.replace(old, new, 1)


# ── 头文件声明 ──
s = patch(H,
"""/* 页面缓存统计：PSRAM 缓存页数/字节 + LittleFS 已存页数/字节 */
""",
"""/* ── 分段渲染诊断（串口 `seg`）──
   seg        = 打印当前分段状态
   seg <n>    = 跳到第 n 段（0 起）
   seg next   = 下一段   seg prev = 上一段
   ⚠️ 真正翻段在 tick 里做（不能在串口/事件上下文里直接清内容区）。 */
void BrowserScreen_segGo(int start);
void BrowserScreen_segDump();
int  BrowserScreen_segStart();
int  BrowserScreen_segSize();

/* 页面缓存统计：PSRAM 缓存页数/字节 + LittleFS 已存页数/字节 */
""", "h decl")

if s is None:
    # 头文件里没找到锚点 → 换个锚点
    s = io.open(H, encoding="utf-8", newline="").read()
    s = s.rstrip() + """

/* ── 分段渲染诊断（串口 `seg`）──
   seg / seg <n> / seg next / seg prev */
void BrowserScreen_segGo(int start);
void BrowserScreen_segDump();
int  BrowserScreen_segStart();
int  BrowserScreen_segSize();
"""
    print("OK: h decl (appended)")

io.open(H, "w", encoding="utf-8", newline="").write(s)

# ── cpp 实现：追加到文件末尾 ──
c = io.open(C, encoding="utf-8", newline="").read()
c = c.rstrip() + """

/* ── 分段渲染：串口诊断入口 ──
   翻段只能在 tick 里真正执行（清内容区会删掉正在派发事件的对象），
   这里只是记下目标。 */
void BrowserScreen_segGo(int start) {
  if (start < 0) start = 0;
  if (g_segTotal > 0 && start >= g_segTotal)
    start = ((g_segTotal - 1) / PAGE_SEG_TILES) * PAGE_SEG_TILES;
  g_segPending = start;
}

int BrowserScreen_segStart() { return g_segStart; }
int BrowserScreen_segSize() { return PAGE_SEG_TILES; }

void BrowserScreen_segDump() {
  int segs = (g_segTotal + PAGE_SEG_TILES - 1) / PAGE_SEG_TILES;
  Serial.printf("[Browser] seg: tree=%p tiles=%d segSize=%d pages=%d cur=%d start=%d pending=%d\\n",
                (void*)g_layoutRoot, g_segTotal, PAGE_SEG_TILES, segs,
                g_segStart / PAGE_SEG_TILES, g_segStart, g_segPending);
}
"""
io.open(C, "w", encoding="utf-8", newline="").write(c)
print("OK: cpp impl")

# ── 串口命令 ──
s = io.open(S, encoding="utf-8", newline="").read()
old = """  } else if (strcmp(cmd, "serve") == 0 || strcmp(cmd, "servestop") == 0) {"""
new = """  } else if (strcmp(cmd, "seg") == 0) {
    /* 分段渲染诊断：seg = 看状态；seg 2 = 跳到第 2 段；seg next / seg prev */
    if (!arg || !*arg) {
      BrowserScreen_segDump();
    } else if (strcmp(arg, "next") == 0) {
      BrowserScreen_segDump();
      int st = BrowserScreen_segStart();
      BrowserScreen_segGo(st + BrowserScreen_segSize());
    } else if (strcmp(arg, "prev") == 0) {
      BrowserScreen_segGo(BrowserScreen_segStart() - BrowserScreen_segSize());
    } else {
      BrowserScreen_segGo(atoi(arg) * BrowserScreen_segSize());
    }
  } else if (strcmp(cmd, "serve") == 0 || strcmp(cmd, "servestop") == 0) {"""
if old not in s:
    print("MISS: seg cmd"); sys.exit(1)
s = s.replace(old, new, 1)
io.open(S, "w", encoding="utf-8", newline="").write(s)
print("OK: serial cmd")
print("DONE")
