# -*- coding: utf-8 -*-
"""去掉 weather_screen.cpp 里的匿名 namespace，把辅助函数/变量收成 static。

为什么要拆：WeatherScreen_create / tick / scr_delete_cb / fetchNow 必须保持
外部链接（nav.cpp、serial_console.cpp 要链接它们），不能待在匿名 namespace 里；
可它们又要读写 g_* 指针和调 weatherStart/applyResult。全提到外层 + static，
链接性收在本 TU，跟别的 .cpp 里的同名符号也不会撞。
"""
import io, re, sys

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\weather_screen.cpp"

src = io.open(P, encoding="utf-8").read()
orig = src

# 1) 删掉 namespace 起止
src = src.replace("namespace {\n\n", "", 1)
src = src.replace("\n}  // namespace\n", "\n", 1)
if "namespace {" in src or "// namespace" in src:
    print("!! namespace 没删干净")
    sys.exit(1)

# 2) 这些函数改成 static（内部链接）
names = [
    "wmoDesc", "windDirName", "weekdayOf", "swipe_cb", "refresh_cb",
    "extractStr", "extractFloatFrom", "arrElem", "arrNum", "arrStr",
    "fetchOnce", "mkLabel", "mkSectionTitle", "mkCell",
]
for n in names:
    pat = re.compile(r"^([A-Za-z_][\w:<>&\* ]*?\b" + n + r"\s*\()", re.M)
    def rep(m):
        s = m.group(1)
        if s.startswith("static "):
            return s
        return "static " + s
    src, cnt = pat.subn(rep, src)
    print("%-18s -> %d" % (n, cnt))

# 3) 变量也收 static
src = src.replace("const char* kWeekName[7] =",
                  "static const char* kWeekName[7] =", 1)

io.open(P, "w", encoding="utf-8", newline="").write(src)
print("bytes %d -> %d" % (len(orig), len(src)))
