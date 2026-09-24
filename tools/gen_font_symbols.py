#!/usr/bin/env python3
"""生成 LVGL 中文字体所需的字符集。

背景：之前那个 font_zh_16 是坏的 —— 生成时用 --symbols 在命令行里传中文，
      被 Windows shell 的代码页/转义搞坏，落进字体里的变成了一批乱码码位
      （韩文音节、希腊字母、数学符号、生僻汉字），常用汉字一个都没有。
      所以这里**先把字符集写进 UTF-8 文件**，再由 Node 读出后以 argv 数组
      传给 lv_font_conv（spawn 不走 shell），彻底绕开编码损坏。

字符集策略（master 定的方向）：
  - 英文/数字/半角符号：**不编进中文字体**。ESP32 侧 LVGL 自带 montserrat
    （lv_conf 的 LV_FONT_DEFAULT），由 fallback 兜底，编进来纯属浪费 flash。
  - 汉字：GB2312 一级字库（3755 个最常用字），覆盖日常中文 99%+。
  - 中文标点/全角符号：montserrat 没有，必须编进来。
"""
import sys

# GB2312 一级字库：区 16-55，即首字节 0xB0-0xD7
LEVEL1 = []
for b1 in range(0xB0, 0xD8):
    for b2 in range(0xA1, 0xFF):
        try:
            ch = bytes([b1, b2]).decode('gb2312')
        except UnicodeDecodeError:
            continue
        LEVEL1.append(ch)

# 中文标点与全角符号（montserrat 没有，必须自带）
PUNCT = "，。、！？；：（）《》【】“”‘’…—·～￥％＃＠　"

def build(level2=False):
    chars = LEVEL1
    if level2:  # GB2312 二级字库（区 56-87），生僻些，一般不需要
        l2 = []
        for b1 in range(0xD8, 0xF8):
            for b2 in range(0xA1, 0xFF):
                try:
                    l2.append(bytes([b1, b2]).decode('gb2312'))
                except UnicodeDecodeError:
                    pass
        chars = chars + l2
    # 去重保序
    seen, out = set(), []
    for c in list(chars) + list(PUNCT):
        if c not in seen:
            seen.add(c)
            out.append(c)
    return ''.join(out)

if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else 'font_symbols.txt'
    level2 = '--level2' in sys.argv
    s = build(level2)
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(s)
    print('charset written: %s, %d chars' % (path, len(s)))
    print('sample:', s[:40])
