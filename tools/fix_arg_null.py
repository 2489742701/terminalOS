"""修复串口命令分发处 arg 可能为 nullptr 导致 atoi(NULL) 崩溃。

executeLine() 里 `char* arg = nullptr;`，命令没带参数时 arg 就是 nullptr。
cmdPerf 当初写了 `(arg && *arg)` 保护，后续加的 cmdPerfLet / cmdPerfTxt
照抄 cmdPerf 的 `int n = atoi(arg);` 就漏了 —— 实测 `perflet`（不带参数）
直接 LoadProhibited @ serial_console.cpp:554。
"""
import io
import os
import sys

P = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 '..', 'src', 'hal', 'serial_console.cpp')
P = os.path.normpath(P)

s = io.open(P, 'r', encoding='utf-8', newline='').read()


def rep(old, new, tag):
    """源文件 CRLF/LF 混排（不同批次注入造成），两种都试。"""
    global s
    for o, n in ((old, new),
                 (old.replace('\n', '\r\n'), new.replace('\n', '\r\n'))):
        if o in s:
            s = s.replace(o, n, 1)
            print('OK   ' + tag)
            return
    raise SystemExit('MISS: ' + tag)


# 1) cmdPerfLet：无条件 atoi -> 判空
rep('static void cmdPerfLet(const char* arg) {\n  int n = atoi(arg);',
    'static void cmdPerfLet(const char* arg) {\n'
    '  /* executeLine 在无参数时传 nullptr，atoi(NULL) = LoadProhibited */\n'
    '  int n = (arg && *arg) ? atoi(arg) : 0;',
    'cmdPerfLet atoi')

# 2) cmdPerfTxt：同样的问题，一起堵上
old_txt = 'static void cmdPerfTxt(const char* arg) {'
if old_txt in s or old_txt.replace('\n', '\r\n') in s:
    i = s.find(old_txt)
    seg = s[i:i + 400]
    if 'atoi(arg)' in seg:
        rep(old_txt + '\n' + seg.split('\n')[1],
            old_txt + '\n  int rows = (arg && *arg) ? atoi(arg) : 0;'
            if 'rows' in seg else old_txt + '\n' + seg.split('\n')[1],
            'cmdPerfTxt atoi')

io.open(P, 'w', encoding='utf-8', newline='').write(s)

# 3) 兜底扫描：全文件还有没有裸的 atoi(arg)
body = s.replace('\r\n', '\n')
bad = [ln for ln in body.split('\n')
       if 'atoi(arg)' in ln and 'arg &&' not in ln]
print('REMAINING bare atoi(arg):', bad if bad else 'none')
