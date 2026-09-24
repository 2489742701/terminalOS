"""搜索结果 <li> 按块渲染，不再压成一坨连写文本。

必应 b_algo 的真实结构：
  <li class="b_algo">
    <link rel=stylesheet> x N          <- 干扰项，必须跳过
    <div class="b_tpcn"><a class="tilk" href=...>espressif.com + cite 网址</a></div>
    <div class="b_algoheader"><a href=...><h2>标题</h2></a></div>
    <div class="b_caption"><p>摘要</p></div>
  </li>
原来 li 被判"有文本"→ 抽成纯文本叶子 → get_element_text() 把三块的 innerText
首尾相接拼成一坨（espressif.comhttps://www.espressif.com...），读不了，
而且标题那个 <a> 根本进不了布局树，点不动。

改法：li 有 >=2 个块级子元素时**不抽文本**，递归下去让各块各自成行。
副作用（正是想要的）：标题 <a> 活下来 → 渲染成可点胶囊，只有标题可点。
"""
import io, sys

PATH = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\dom_renderer.cpp"

FUNC = r'''
/* li 里有没有 >=2 个"能各自成块"的子元素（搜索结果条目的判据）。
   只数元素节点；跳过 <link>/<script>/<style>/<meta>/<br>/<hr> —— 必应在
   b_algo 里塞了一长串 <link rel="stylesheet">，不跳过的话任意 li 都会被误判。 */
static bool li_has_block_children(lxb_dom_node_t *node) {
  int n = 0;
  for (lxb_dom_node_t *c = html_parser.get_first_child(node); c;
       c = html_parser.get_next_sibling(c)) {
    if (c->type != LXB_DOM_NODE_TYPE_ELEMENT)
      continue;
    lxb_dom_element_t *el = (lxb_dom_element_t *)c;
    size_t tl = 0;
    const char *tag = html_parser.get_element_tag(el, &tl);
    if (!tag || tl == 0)
      continue;
    /* 手写下划线比较，不依赖 strncasecmp（各平台头文件不一致） */
    bool skip = false;
    static const char *kNop[] = {"link", "script", "style", "meta", "br", "hr",
                                 NULL};
    for (int k = 0; kNop[k]; k++) {
      const char *p = kNop[k];
      size_t pl = strlen(p);
      if (pl != tl)
        continue;
      bool eq = true;
      for (size_t i = 0; i < tl; i++) {
        if (tolower((unsigned char)tag[i]) != p[i]) {
          eq = false;
          break;
        }
      }
      if (eq) {
        skip = true;
        break;
      }
    }
    if (skip)
      continue;
    n++;
    if (n >= 2)
      return true;
  }
  return false;
}
'''

OLD_LI = '''  case ELEMENT_LIST_ITEM:
    /* 2026-09-23：导航条 <li><a href>我的关注</a></li> 被当成纯文本叶子，
       里面的 <a> 连同 href 一起被丢掉 —— 平铺出来是四行白字，既没并成一行、
       也不是链接、将来还点不动。li 只是个 <a> 的包装时，不抽文本，让 <a> 自己活。 */
    should_extract_text = !li_is_link_wrapper(dom_node);
    break;'''

NEW_LI = '''  case ELEMENT_LIST_ITEM:
    /* 2026-09-23：导航条 <li><a href>我的关注</a></li> 被当成纯文本叶子，
       里面的 <a> 连同 href 一起被丢掉 —— 平铺出来是四行白字，既没并成一行、
       也不是链接、将来还点不动。li 只是个 <a> 的包装时，不抽文本，让 <a> 自己活。 */
    /* 2026-09-23（二）：搜索结果 <li class="b_algo"> 同理，但它更复杂 ——
       里面是「来源行 / <a><h2>标题 / <p>摘要」三块，抽成纯文本叶子会把三块的
       innerText 首尾相接拼成一坨（espressif.comhttps://www.espressif.com...），
       480 屏上根本读不了，标题那个 <a> 还被一起丢掉（点不动）。
       li 里有 >=2 个块级子元素时不抽文本，递归下去让各块各自成行；
       标题 <a> 因此活下来，渲染成可点胶囊 —— 只有标题可点，不会误触。 */
    should_extract_text = !li_is_link_wrapper(dom_node) &&
                          !li_has_block_children(dom_node);
    break;'''

ANCHOR = "static bool li_is_link_wrapper(lxb_dom_node_t *node) {"

t = io.open(PATH, encoding="utf-8", newline="").read()
out = []
out.append("orig bytes = %d" % len(t))

if "li_has_block_children" in t:
    out.append("ALREADY PATCHED: li_has_block_children exists")
elif ANCHOR not in t:
    out.append("*** ANCHOR NOT FOUND ***")
else:
    t = t.replace(ANCHOR, FUNC.lstrip("\n") + "\n" + ANCHOR, 1)
    out.append("inserted li_has_block_children")

if OLD_LI not in t:
    out.append("*** OLD_LI NOT FOUND ***")
else:
    t = t.replace(OLD_LI, NEW_LI, 1)
    out.append("replaced LIST_ITEM rule")

io.open(PATH, "w", encoding="utf-8", newline="").write(t)
out.append("new bytes = %d" % len(t))

# 核对
v = io.open(PATH, encoding="utf-8").read()
out.append("verify li_has_block_children def: %d" % v.count("static bool li_has_block_children"))
out.append("verify in rule: %s" % ("li_has_block_children(dom_node)" in v))

print("\n".join(out))
