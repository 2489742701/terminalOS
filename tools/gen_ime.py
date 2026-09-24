#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
生成中文输入法（拼音 -> 候选汉字）查询表：src/app/ime_pinyin.c / ime_pinyin.h

设计约束（2026-09-24）：
  1. 只收 font_zh_16 里**真的有字形**的汉字 —— 否则候选点了也是豆腐块。
     字符集来源 tools/font_symbols_16.txt。
  2. 候选顺序按真实字频（wordfreq 的 zh 语料）降序，高频字排前面。
  3. 产出是 const 表 -> 落在 .rodata（Flash），运行时不占 DRAM。
     查表用二分（按音节字母序），400 个音节最多 9 次比较。

用法（必须用装了 pypinyin + wordfreq 的解释器，当前是 python-sdk 的 3.13.2）：
    <py> tools/gen_ime.py [max_chars] [max_per_pinyin]
        max_chars       收多少个字（默认 2000）
        max_per_pinyin  每个音节最多几个候选（默认 12）
"""
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SYMBOLS = os.path.join(HERE, "font_symbols_16.txt")
OUT_C = os.path.join(ROOT, "src", "app", "ime_pinyin.c")
OUT_H = os.path.join(ROOT, "src", "app", "ime_pinyin.h")

MAX_CHARS = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
MAX_PER_PY = int(sys.argv[2]) if len(sys.argv) > 2 else 12


def load_chars():
    s = io.open(SYMBOLS, encoding="utf-8").read()
    s = "".join(s.split())
    return [c for c in s if "\u4e00" <= c <= "\u9fff"]


def build():
    from pypinyin import pinyin, Style
    from wordfreq import word_frequency

    chars = load_chars()
    scored = []
    for c in chars:
        f = word_frequency(c, "zh")
        if f <= 0:
            continue
        py = pinyin(c, style=Style.NORMAL, heteronym=False)
        if not py or not py[0]:
            continue
        key = "".join(ch for ch in py[0][0].lower() if "a" <= ch <= "z")
        if not key:
            continue
        scored.append((f, key, c))
    scored.sort(key=lambda t: -t[0])
    scored = scored[:MAX_CHARS]

    table = {}
    for f, key, c in scored:
        table.setdefault(key, []).append(c)
    keys = sorted(table.keys())

    totalHan = 0
    rows = []
    for k in keys:
        cs = table[k][:MAX_PER_PY]
        totalHan += len(cs)
        rows.append((k, "".join(cs)))
    return rows, len(scored), totalHan


def emit(rows, nChars, nHan):
    lines = []
    lines.append("/* 自动生成，请勿手改：tools/gen_ime.py */")
    lines.append("/* 拼音 -> 候选汉字表。字集 ⊆ font_zh_16（保证有字形），顺序按字频降序。 */")
    lines.append("/* 收录 %d 字 / %d 个音节，每音节最多 %d 个候选。 */" % (nChars, len(rows), MAX_PER_PY))
    lines.append("")
    lines.append('#include "ime_pinyin.h"')
    lines.append("#include <string.h>")
    lines.append("#include <stddef.h>")
    lines.append("")
    lines.append("/* 音节按字母序排列，供二分查找（ime_lookup）。 */")
    lines.append("const ImeEntry ime_table[] = {")
    for k, v in rows:
        lines.append('    {"%s", "%s"},' % (k, v))
    lines.append("};")
    lines.append("")
    lines.append("const int ime_table_count = (int)(sizeof(ime_table) / sizeof(ime_table[0]));")
    lines.append("")
    lines.append("/* ── 二分查找 ──")
    lines.append("   ⚠️ ime_table 是 Flash 里的 const，strcmp 走的是 cache 命中路径，")
    lines.append("   比线性扫还便宜；但别在这里做任何动态分配（IME 在 UI 线程里跑）。 */")
    lines.append("const char* ime_lookup(const char* py) {")
    lines.append("  if (!py || !py[0]) return NULL;")
    lines.append("  int lo = 0, hi = ime_table_count - 1;")
    lines.append("  while (lo <= hi) {")
    lines.append("    int mid = lo + (hi - lo) / 2;")
    lines.append("    int c = strcmp(py, ime_table[mid].key);")
    lines.append("    if (c == 0) return ime_table[mid].hanzi;")
    lines.append("    if (c < 0) hi = mid - 1; else lo = mid + 1;")
    lines.append("  }")
    lines.append("  return NULL;")
    lines.append("}")
    lines.append("")
    lines.append("const char* ime_pick(const char* hanzi, int n, char* out4) {")
    lines.append("  if (!hanzi || n < 0 || !out4) return NULL;")
    lines.append("  const char* p = hanzi;")
    lines.append("  for (int i = 0; i < n && *p; i++) {")
    lines.append("    unsigned char c0 = (unsigned char)p[0];")
    lines.append("    unsigned char c1 = (unsigned char)p[1];")
    lines.append("    /* 汉字在 UTF-8 里固定 3 字节；遇到单字节说明表坏了，直接停 */")
    lines.append("    if (c0 < 0x80) return NULL;")
    lines.append("    int step = (c1 && (c1 & 0xC0) == 0x80) ? 3 : 1;")
    lines.append("    p += step;")
    lines.append("  }")
    lines.append("  if (!*p) return NULL;")
    lines.append("  out4[0] = p[0]; out4[1] = p[1]; out4[2] = p[2]; out4[3] = 0;")
    lines.append("  return out4;")
    lines.append("}")
    lines.append("")
    text = "\n".join(lines) + "\n"
    with io.open(OUT_C, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)

    h = []
    h.append("/* 自动生成，请勿手改：tools/gen_ime.py */")
    h.append("#pragma once")
    h.append("")
    h.append("#ifdef __cplusplus")
    h.append('extern "C" {')
    h.append("#endif")
    h.append("/* 拼音输入法的最小查询表：一个音节 -> 一串候选汉字（UTF-8，按字频降序）。")
    h.append(" * 全部是 const，落在 Flash，运行期零 DRAM 开销。 */")
    h.append("typedef struct {")
    h.append("  const char* key;    /* 无声调小写拼音，如 \"nihao\" 的单字表按 \"ni\" 查 */")
    h.append("  const char* hanzi;  /* 候选字串，每个汉字 3 字节 UTF-8 */")
    h.append("} ImeEntry;")
    h.append("")
    h.append("extern const ImeEntry ime_table[];")
    h.append("extern const int ime_table_count;")
    h.append("")
    h.append("/* 查音节：命中返回候选字串（'\0' 结尾），未命中返回 NULL。 */")
    h.append("const char* ime_lookup(const char* py);")
    h.append("")
    h.append("/* 取第 n 个候选汉字的 UTF-8（3 字节，不含结尾 \\0），越界返回 NULL。")
    h.append("   调用方给 4 字节以上缓冲。 */")
    h.append("const char* ime_pick(const char* hanzi, int n, char* out4);")
    h.append("")
    h.append("")
    h.append("#ifdef __cplusplus")
    h.append("}")
    h.append("#endif")
    with io.open(OUT_H, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(h) + "\n")
    return os.path.getsize(OUT_C)


if __name__ == "__main__":
    import io

    rows, nChars, nHan = build()
    size = emit(rows, nChars, nHan)
    print("chars=%d syllables=%d hanzi=%d  %s (%d B)" % (nChars, len(rows), nHan, OUT_C, size))
