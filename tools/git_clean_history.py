#!/usr/bin/env python3
"""把仓库历史里的「非自家产物」剔除掉。

用途：git-filter-repo 在本机装不上（GitHub raw 不通），所以用
git fast-export -> 本脚本过滤 -> git fast-import 自己重写一遍历史。

只做一件事：按路径规则丢弃 filemodify / filedelete / rename / copy 记录。
commit 结构、作者、时间、提交信息全部原样保留。

用法：
    python tools/git_clean_history.py <源仓库> <目标空仓库>
目标仓库必须先 git init 过。导入完成后需要手动：
    git -C <目标仓库> reset --hard <分支名>
"""
import subprocess
import sys
import os

# ---- 剔除规则（路径相对仓库根，用 / 分隔）----
DROP_EXACT = {
    'tools/arduino-cli.exe',   # 32MB，下载的第三方 CLI
}
DROP_SUFFIX = ('.log',)        # 86 个构建/串口日志
DROP_PREFIX = (
    # lexbor 的测试/工具/示例资源：build_src_filter 里本来就排除了，
    # 只是当初一股脑 commit 进来了
    'src/browser_engine/lexbor/test/',
    'src/browser_engine/lexbor/utils/',
    'src/browser_engine/lexbor/examples/',
    'src/browser_engine/lexbor/wasm/',
    'src/browser_engine/lexbor/benchmarks/',
    'src/browser_engine/lexbor/packaging/',
    '.wb_build/',               # 曾经的构建产物误入
)


def should_drop(path):
    if path in DROP_EXACT:
        return True
    if path.endswith(DROP_SUFFIX):
        return True
    return any(path.startswith(p) for p in DROP_PREFIX)


# fast-export 对含特殊字符的路径会做 C 风格转义（core.quotePath=false 后
# 中文不再转义，但空格/反斜杠之类仍会）
def unquote(b):
    if not b.startswith(b'"'):
        return b
    esc = {ord('n'): 10, ord('t'): 9, ord('r'): 13, ord('a'): 7,
           ord('b'): 8, ord('f'): 12, ord('v'): 11,
           ord('\\'): 92, ord('"'): 34}
    out = bytearray()
    i = 1
    end = len(b) - 1 if b.endswith(b'"') else len(b)
    while i < end:
        c = b[i]
        if c == 0x5c and i + 1 < end:
            n = b[i + 1]
            if n in esc:
                out.append(esc[n])
                i += 2
                continue
            if 0x30 <= n <= 0x37:
                out.append(int(b[i + 1:i + 4], 8) & 0xFF)
                i += 4
                continue
            out.append(n)
            i += 2
            continue
        out.append(c)
        i += 1
    return bytes(out)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]

    exp = subprocess.Popen(
        ['git', '-c', 'core.quotePath=false', 'fast-export', '--all',
         '--signed-tags=strip'],
        cwd=src, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    imp = subprocess.Popen(
        ['git', 'fast-import', '--quiet', '--force'],
        cwd=dst, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)

    out = imp.stdin
    inp = exp.stdout
    kept = dropped = 0

    while True:
        line = inp.readline()
        if not line:
            break
        # data <n> 后面是 n 字节原始内容（可能是二进制），整块搬运
        if line.startswith(b'data '):
            n = int(line.split()[1])
            out.write(line)
            rem = n
            while rem > 0:
                chunk = inp.read(min(65536, rem))
                if not chunk:
                    break
                out.write(chunk)
                rem -= len(chunk)
            nxt = inp.peek(1)[:1]
            if nxt == b'\n':
                inp.read(1)
                out.write(b'\n')
            continue

        # ⚠️ readline 带行尾 \n，必须先剥掉 —— 否则 endswith('.log')
        # 和精确路径匹配永远不成立（前缀匹配却会生效，极具迷惑性）
        bare = line.rstrip(b'\r\n')
        tag = bare[:2]
        if tag in (b'M ', b'D ', b'R ', b'C '):
            parts = bare.split(b' ')
            if tag == b'M ':
                path = unquote(b' '.join(parts[3:]))
            elif tag == b'D ':
                path = unquote(b' '.join(parts[1:]))
            else:                                   # R / C:  old new
                a = unquote(parts[1])
                b_ = unquote(parts[2])
                if should_drop(a.decode('utf-8', 'replace')) or \
                   should_drop(b_.decode('utf-8', 'replace')):
                    dropped += 1
                    continue
                out.write(line)
                kept += 1
                continue
            p = path.decode('utf-8', 'replace')
            if should_drop(p):
                dropped += 1
                continue
            out.write(line)
            kept += 1
            continue

        out.write(line)

    out.close()
    imp.stdin = None
    exp.stdout.close()
    rc_exp = exp.wait()
    rc_imp = imp.wait()
    err_imp = imp.stderr.read().decode('utf-8', 'replace')
    err_exp = exp.stderr.read().decode('utf-8', 'replace')

    print('kept fileops  :', kept)
    print('dropped       :', dropped)
    print('fast-export rc:', rc_exp, err_exp[-500:])
    print('fast-import rc:', rc_imp, err_imp[-2000:])
    return 0 if rc_imp == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
