"""过滤必应的"时间筛选"链接（全部/24小时内/一周内/一个月内/去年）。

这些链接形如 /search?q=esp32&filters=ex1%3a%22ez1%22&FORM=000017。
在我们的 UA 下必应**直接忽略 filters 参数**（实测结果与不带筛选完全一样），
于是点了会跳到一个"看起来跟刚才一样、但又有点怪"的页面 —— 纯粹是噪音。
480 屏上留着只会挤掉真结果，直接丢掉。
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\layout_engine.cpp"

HELPER = r'''
/* 必应的时间筛选链接（filters=ex1...）：本机上点了没效果（必应对我们的 UA
   直接忽略 filters 参数，实测结果与不带筛选完全相同），只会挤掉真结果。 */
static bool flat_is_time_filter_link(LayoutNode *n) {
  if (!n) return false;
  const char *h = n->href_resolved ? n->href_resolved
                                   : (n->href ? n->href : n->href_path);
  if (!h) return false;
  return (strstr(h, "filters=ex1") != NULL);
}
'''

OLD_BODY = """  if (node->text_content && flat_is_junk_text(node->text_content)) {
    free(node->text_content);
    node->text_content = NULL;
    n++;
  }
  return n;"""

NEW_BODY = """  if (node->text_content && flat_is_junk_text(node->text_content)) {
    free(node->text_content);
    node->text_content = NULL;
    n++;
  }
  if (flat_is_time_filter_link(node)) {
    if (node->text_content) { free(node->text_content); node->text_content = NULL; }
    if (node->href)          { free(node->href);         node->href = NULL; }
    if (node->href_resolved) { free(node->href_resolved); node->href_resolved = NULL; }
    if (node->href_path)     { free(node->href_path);     node->href_path = NULL; }
    n++;
  }
  return n;"""

t = io.open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in t
lines = t.replace("\r\n", "\n").split("\n")
log = []

if "flat_is_time_filter_link" in "\n".join(lines):
    log.append("ALREADY")
else:
    i = -1
    for k, l in enumerate(lines):
        if "static int layout_drop_junk" in l:
            i = k
            break
    if i < 0:
        log.append("*** drop_junk not found ***")
    else:
        lines[i:i] = HELPER.strip("\n").split("\n")
        log.append("helper inserted")
    txt = "\n".join(lines)
    if OLD_BODY not in txt:
        log.append("*** body not found ***")
    else:
        txt = txt.replace(OLD_BODY, NEW_BODY, 1)
        lines = txt.split("\n")
        log.append("body patched")

out = "\n".join(lines)
if crlf:
    out = out.replace("\n", "\r\n")
io.open(P, "w", encoding="utf-8", newline="").write(out)

v = io.open(P, encoding="utf-8").read()
log.append("verify helper: %s" % ("flat_is_time_filter_link" in v))
log.append("verify body:   %s" % ("filters=ex1" in v))
print("\n".join(log))
