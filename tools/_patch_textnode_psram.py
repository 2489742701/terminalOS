# -*- coding: utf-8 -*-
"""解析期的 DRAM 黑洞：文本节点用裸 malloc。

现象（2026-09-25 实测）：163 首页下完 HTML 时内部 DRAM 还有 93.6KB，解析完
布局树只剩 **几百字节**（render 后 2.5KB，再存张图 560B）。

根因：文本节点是这棵布局树里数量最多的东西（一个页面几百个），
而 dom_renderer 建文本节点时写的是**裸 malloc**。CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL
=4096 会把 <=4KB 的 malloc 一律塞进**内部 DRAM** —— 于是每段文字都从最紧张的
资源里抠一块。整棵树里别的字符串（href / text / class）早就走 tb_alloc 了，
只有这一条漏了。

顺带修一个真·崩溃隐患：原代码对 malloc 返回值**不做判空**就 memcpy，
DRAM 见底时就是往 NULL 写。

⚠️ DRAM 掉到几百字节的后果不只是"少几 KB"：
  · lwIP / WiFi 驱动拿不到内部内存 → DNS 直接失败（图片全下不来，实测过）；
  · 任何一处小分配失败 → 空指针 → 整机重启。
"""
import io, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fail = []


def load(rel):
    p = os.path.join(ROOT, rel)
    with io.open(p, 'r', encoding='utf-8', newline='') as f:
        return p, f.read()


def save(p, text):
    with io.open(p, 'w', encoding='utf-8', newline='') as f:
        f.write(text)


def sub(p, text, old, new):
    nl = '\r\n' if '\r\n' in text else '\n'
    o = old.replace('\n', nl)
    n = new.replace('\n', nl)
    cnt = text.count(o)
    if cnt != 1:
        fail.append('%s: count=%d :: %s' % (p, cnt, old.split('\n')[0][:70]))
        return text
    return text.replace(o, n, 1)


p, t = load('src/browser_engine/src/dom_renderer.cpp')
t = sub(p, t,
"""      if (start <= end) {
        LayoutNode *text_node = layout_node_create(ELEMENT_SPAN);
        if (text_node) {
          size_t trimmed_len = end - start + 1;
          text_node->text_content = (char *)malloc(trimmed_len + 1);
          memcpy(text_node->text_content, start, trimmed_len);
          text_node->text_content[trimmed_len] = '\\0';
        }
        free(txt);
        return text_node;""",
"""      if (start <= end) {
        LayoutNode *text_node = layout_node_create(ELEMENT_SPAN);
        if (text_node) {
          size_t trimmed_len = end - start + 1;
          /* ⚠️ 这里以前是裸 malloc。文本节点是布局树里数量最多的东西
             （一页几百个），而 CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096 会把
             <=4KB 的 malloc 全塞进**内部 DRAM** —— 解析一页 DRAM 掉几十 KB，
             掉到几百字节时 lwIP 连 DNS 都发不出去、随便一个分配失败就空指针崩。
             tb_alloc 走 PSRAM（满了才退回 DRAM）。 */
          text_node->text_content = (char *)tb_alloc(trimmed_len + 1);
          if (!text_node->text_content) {
            /* 原代码不判空就 memcpy —— 往 NULL 写，DRAM 见底时就是崩溃 */
            layout_node_destroy(text_node);
            free(txt);
            return NULL;
          }
          memcpy(text_node->text_content, start, trimmed_len);
          text_node->text_content[trimmed_len] = '\\0';
        }
        free(txt);
        return text_node;""")
save(p, t)

print('FAILURES: %d' % len(fail))
for f in fail:
    print(' -', f)
sys.exit(1 if fail else 0)
