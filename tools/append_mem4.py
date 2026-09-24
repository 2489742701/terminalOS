"""追加当天工作记录（UTF-8，避开 Edit 工具的静默失败）。"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\2026-09-23.md"

ADD = """
## 搜索结果按块渲染 + 高度诊断修复（2026-09-23 夜，已烧录验证）

### 1. 结果条目不再压成一坨
现象：一条结果是 `espressif.comhttps://www.espressif.com — zh-hans — products — socsESP32 Wi-Fi & 蓝牙 SoC | 乐鑫科技ESP32 is a feature-rich...`，三块首尾相接。

根因：`<li class="b_algo">` 被判"有文本"→ 抽成**纯文本叶子** → `get_element_text()`
把整棵子树的 innerText 无分隔符拼起来；同时里面的 `<a><h2>标题</h2></a>` 被丢掉。

真实 DOM（PC 抓的，KitKat UA）：
```
<li class="b_algo">
  <link rel=stylesheet> x N            ← 干扰项，必须跳过
  <div class="b_tpcn"><a class="tilk" href>espressif.com + <cite>网址</cite></a></div>
  <div class="b_algoheader"><a href><h2>标题</h2></a></div>
  <div class="b_caption"><p>摘要</p></div>
</li>
```

修法（`dom_renderer.cpp`）：新增 `li_has_block_children()` —— 数 li 的**元素子节点**
（跳过 link/script/style/meta/br/hr，必应在 b_algo 里塞了一长串 `<link rel=stylesheet>`），
>=2 个就**不抽文本**，递归下去让各块各自成行：
```c
should_extract_text = !li_is_link_wrapper(dom_node) &&
                      !li_has_block_children(dom_node);
```
副作用正是想要的：标题 `<a>` 活下来 → 渲染成可点胶囊 → **只有标题可点**，不会误触。

### 2. 实测对比（cn.bing.com/search?q=esp32）
| | 改前 | 改后 |
|---|---|---|
| 布局节点 | 67 | **115** |
| widget | 22 | **37** |
| clickable links | 17 | **21** |
| 一条结果 | 1 行连写 | **3 行**（来源 / 标题胶囊 / 摘要） |

渲染形态：`[Flat] 12 [来源…]` / `[Flat] 13 [标题]` / `[Flat] 14 摘要`。
方括号 = 胶囊（chip，带框可点），无括号 = 普通 label。

### 3. ⚠️ 修掉一个会骗人的诊断：`render done total=80`
`lv_obj_get_scroll_bottom()` 读的是 `child->coords.y2`，而**刚创建完 widget 时 LVGL 还没
重算坐标**，于是恒为负（scrollable=-334 → total=80），看着像"内容被压扁到 80px"。
历次 probe 全是这个值，我一度误判成回归。
修法：测量前 `lv_obj_update_layout(g_content)`。修好后 **total=914 scrollable=500**，
和 37 个 widget 对得上。

### 4. 工具
- `tools/fix_serp_split.py`、`tools/fix_height_diag.py`（CRLF 文件必须**按行**替换，
  整串 `\n` 匹配会失败）。
- 本轮 Edit 工具又静默失败，已第 6 次；关键改动一律用 python 脚本 + 当场核对。
- NTP 这次走 `ntp.aliyun.com` 成功了（上次超时走 pool.ntp.org）→ 印证多服务器轮换不能省。
"""

t = io.open(P, encoding="utf-8").read()
if "搜索结果按块渲染" in t:
    print("ALREADY")
else:
    io.open(P, "a", encoding="utf-8").write(ADD)
    print("appended")

v = io.open(P, encoding="utf-8").read()
print("len =", len(v))
print("has section:", "搜索结果按块渲染" in v)
