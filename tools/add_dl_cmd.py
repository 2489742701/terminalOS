"""加串口命令 dl：把当前页面存进 LittleFS（不点屏也能测试下载/取页面）。"""
import io

H = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.h"
C = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"
S = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\hal\serial_console.cpp"

log = []

# ---- header: 声明 ----
h = io.open(H, encoding="utf-8", newline="").read()
if "BrowserScreen_download" in h:
    log.append("hdr: ALREADY")
else:
    A = "/* 退出浏览器：释放网页内容 widget 与布局树（屏壳保留） */"
    if A not in h:
        log.append("hdr: *** anchor missing ***")
    else:
        h = h.replace(A,
                      "/* 把当前页 HTML 存进 LittleFS（底栏「下载」键走的就是这个）。\n"
                      "   串口命令：dl —— 不用点屏也能取页面。 */\n"
                      "void BrowserScreen_download();\n\n" + A, 1)
        io.open(H, "w", encoding="utf-8", newline="").write(h)
        log.append("hdr: ok")
log.append("hdr verify: %s" % ("BrowserScreen_download" in io.open(H, encoding="utf-8").read()))

# ---- browser_screen.cpp: 公开包装 ----
c = io.open(C, encoding="utf-8", newline="").read()
if "void BrowserScreen_download()" in c:
    log.append("cpp: ALREADY")
else:
    A = "/* ── tick：状态机驱动 ── */"
    if A not in c:
        log.append("cpp: *** anchor missing ***")
    else:
        NEW = ("/* 串口命令 dl / 底栏「下载」键共用：把当前页存进 LittleFS。\n"
               "   downloadCurrentPage() 在匿名 namespace 里，这里给它一个外部入口。 */\n"
               "void BrowserScreen_download() { downloadCurrentPage(); }\n\n")
        c = c.replace(A, NEW + A, 1)
        io.open(C, "w", encoding="utf-8", newline="").write(c)
        log.append("cpp: ok")
log.append("cpp verify: %s" % ("void BrowserScreen_download()" in io.open(C, encoding="utf-8").read()))

# ---- serial_console.cpp: 命令 ----
s = io.open(S, encoding="utf-8", newline="").read()
if 'strcmp(cmd, "dl")' in s:
    log.append("console: ALREADY")
else:
    A = '  } else if (strcmp(cmd, "flat") == 0) {'
    if A not in s:
        log.append("console: *** anchor missing ***")
    else:
        NEW = ('  } else if (strcmp(cmd, "dl") == 0) {\n'
               '    /* 把当前页 HTML 存进 LittleFS（与底栏「下载」键同一条路径）。\n'
               '       页面必须还在缓存里（5 分钟 TTL），否则先刷新再 dl。 */\n'
               '    BrowserScreen_download();\n')
        s = s.replace(A, NEW + A, 1)
        io.open(S, "w", encoding="utf-8", newline="").write(s)
        log.append("console: ok")
log.append("console verify: %s" % ('strcmp(cmd, "dl")' in io.open(S, encoding="utf-8").read()))

print("\n".join(log))
