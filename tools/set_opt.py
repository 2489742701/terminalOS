"""切换编译优化等级：-Os(默认) / -O2 / -O3。

⚠️ 不能新增一个 build_flags 键 —— PlatformIO 的 ini 解析器遇到同 section 重复键会报错。
必须直接改 [env:esp32s3] 里已有的 build_flags 块，把 -O 追加到最后一行。
gcc 取命令行上最后一个 -O，而 build_flags(CPPFLAGS) 排在 platform 的 CCFLAGS(-Os) 之后，
所以直接追加就能覆盖，不需要 build_unflags。

⚠️ 这个工程里 .ini/.cpp/.h 的换行符不统一（有的 CRLF 有的 LF），
按行处理、保留每行自己的行尾，别做整块字符串替换。

用法:
  python tools/set_opt.py os|o2|o3
"""
import io, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
INI = os.path.join(ROOT, 'platformio.ini')

ANCHOR = '-Isrc/browser_engine/lexbor/source'
INJECTED = re.compile(r'^\t-(Os|O2|O3)\b')


def apply(level):
    lines = io.open(INI, 'r', encoding='utf-8', newline='').read().split('\n')
    out = []
    for ln in lines:
        if INJECTED.match(ln):
            continue
        out.append(ln)
        if ln.strip() == ANCHOR and level != 'os':
            flag = '-O2' if level == 'o2' else '-O3'
            out.append('\t' + flag + '  ; PERF: LVGL 绘制热路径换速度（platform 默认 -Os）')
    io.open(INI, 'w', encoding='utf-8', newline='').write('\n'.join(out))
    cur = 'os(default)'
    for ln in out:
        m = INJECTED.match(ln)
        if m:
            cur = m.group(1)
    print('OPT_LEVEL=%s' % cur)


if __name__ == '__main__':
    lv = sys.argv[1] if len(sys.argv) > 1 else 'os'
    if lv not in ('os', 'o2', 'o3'):
        raise SystemExit('usage: set_opt.py os|o2|o3')
    apply(lv)
