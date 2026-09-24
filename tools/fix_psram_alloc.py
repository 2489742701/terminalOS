"""引擎的短期分配改走 PSRAM。

为什么必须改：`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` —— 所有 <=4KB 的 malloc
**一律落进内部 DRAM**。布局树节点、text_content、href/class/style 拷贝全都是小分配，
于是解析一页内部 DRAM 掉 ~40KB，而 PSRAM 7.29MB 纹丝不动（实测日志印证）。
第二页基线更低 + 堆更碎 → 解析时更容易 OOM / 触发崩溃。

改法：新增 tb_alloc/tb_calloc（PSRAM 优先，malloc 兜底），替换
  · tactilebrowser_core.cpp: safe_strdup / safe_strndup
  · layout_engine.cpp: layout_node_create（calloc）
  · dom_renderer.cpp: text_content 的 malloc

⚠️ ESP-IDF 里 free() == heap_caps_free()，所以原有的 free() 调用不用改。
"""
import io, re

CORE = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\tactilebrowser_core.cpp"
HDR = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\include\tactilebrowser_core.h"
LAYOUT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\layout_engine.cpp"
DOM = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\dom_renderer.cpp"

log = []

# ---------- 1. core: 新增 tb_alloc / tb_calloc，改两个 strdup ----------
t = io.open(CORE, encoding="utf-8", newline="").read()
if "tb_alloc" in t:
    log.append("core: ALREADY")
else:
    ANCHOR = "char *safe_strdup(const char *str) {"
    NEWFN = '''/* ── 引擎短期分配的"去 DRAM 化" ──
 * 这些分配全都 <=4KB，而 CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096 会把
 * <=4KB 的 malloc 一律塞进内部 DRAM。结果：解析一页 DRAM 掉 ~40KB，
 * 而 PSRAM 7.29MB 完全空闲 —— 稀有资源被挤，充裕资源睡着。
 * 布局树/文本/href 都是"渲染完就扔"的短命对象，放 PSRAM 正合适。
 * ⚠️ ESP-IDF 的 free() 等价于 heap_caps_free()，所以调用方 free() 不用改。 */
void *tb_alloc(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = malloc(n);            /* PSRAM 用尽时退回 DRAM，宁可慢也别崩 */
  return p;
}

void *tb_calloc(size_t count, size_t size) {
  size_t n = count * size;
  void *p = tb_alloc(n);
  if (p) memset(p, 0, n);
  return p;
}

'''
    if ANCHOR not in t:
        log.append("core: *** ANCHOR NOT FOUND ***")
    else:
        t = t.replace(ANCHOR, NEWFN + ANCHOR, 1)
        t = t.replace("  char *result = (char *)malloc(len);",
                      "  char *result = (char *)tb_alloc(len);", 1)
        t = t.replace("  char *result = (char *)malloc(n + 1);",
                      "  char *result = (char *)tb_alloc(n + 1);", 1)
        io.open(CORE, "w", encoding="utf-8", newline="").write(t)
        log.append("core: patched")
v = io.open(CORE, encoding="utf-8").read()
log.append("core verify tb_alloc def=%d strdup=%d strndup=%d" % (
    v.count("void *tb_alloc("),
    v.count("(char *)tb_alloc(len)"),
    v.count("(char *)tb_alloc(n + 1)")))

# ---------- 2. header: 声明 ----------
h = io.open(HDR, encoding="utf-8", newline="").read()
if "tb_alloc" in h:
    log.append("hdr: ALREADY")
else:
    A = "char *safe_strdup(const char *str);"
    if A not in h:
        log.append("hdr: *** ANCHOR NOT FOUND ***")
    else:
        DECL = ("/* 引擎短期分配（PSRAM 优先），详见 tactilebrowser_core.cpp */\n"
                "void *tb_alloc(size_t n);\n"
                "void *tb_calloc(size_t count, size_t size);\n")
        h = h.replace(A, DECL + A, 1)
        io.open(HDR, "w", encoding="utf-8", newline="").write(h)
        log.append("hdr: patched")
log.append("hdr verify: %s" % ("tb_alloc" in io.open(HDR, encoding="utf-8").read()))

# ---------- 3. layout_engine: layout_node_create 用 tb_calloc ----------
l = io.open(LAYOUT, encoding="utf-8", newline="").read()
if "tb_calloc(1, sizeof(LayoutNode))" in l:
    log.append("layout: ALREADY")
else:
    OLD = "LayoutNode *node = (LayoutNode *)calloc(1, sizeof(LayoutNode));"
    if OLD not in l:
        log.append("layout: *** OLD NOT FOUND ***")
    else:
        l = l.replace(OLD, "LayoutNode *node = (LayoutNode *)tb_calloc(1, sizeof(LayoutNode));", 1)
        io.open(LAYOUT, "w", encoding="utf-8", newline="").write(l)
        log.append("layout: patched")
log.append("layout verify: %s" % (
    "tb_calloc(1, sizeof(LayoutNode))" in io.open(LAYOUT, encoding="utf-8").read()))

# ---------- 4. dom_renderer: text_content 用 tb_alloc ----------
d = io.open(DOM, encoding="utf-8", newline="").read()
if "layout_node->text_content = (char *)tb_alloc" in d:
    log.append("dom: ALREADY")
else:
    OLD = "layout_node->text_content = (char *)malloc(trimmed_len + 1);"
    if OLD not in d:
        log.append("dom: *** OLD NOT FOUND ***")
    else:
        d = d.replace(OLD, "layout_node->text_content = (char *)tb_alloc(trimmed_len + 1);", 1)
        io.open(DOM, "w", encoding="utf-8", newline="").write(d)
        log.append("dom: patched")
log.append("dom verify: %s" % (
    "tb_alloc(trimmed_len + 1)" in io.open(DOM, encoding="utf-8").read()))

print("\n".join(log))
