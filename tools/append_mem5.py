"""追加当天记录：内存优化（PSRAM 分流）+ 页面缓存 + 底栏重排。"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\2026-09-23.md"

ADD = """
## 内存优化 + 页面缓存 + 底栏重排（2026-09-23 深夜，已烧录验证）

master 反馈"浏览器加载第二个页面会崩，是不是没做内存优化"。

### 1. 复现尝试（都没崩，但挖到真问题）
`tools/probe_nav2.py`（连续翻多页，自动识别 panic/assert/Backtrace）：
- 三连翻 bing（esp32/lvgl/esp32）→ 不崩
- 第二页用 espressif 文档站（64 widget）→ 不崩
- 第二页用 m.baidu.com（2.93MB，撞下载上限）→ 不崩
- 但发现：**解析阶段 DRAM 从 248KB 掉到 205~220KB**，而 PSRAM 恒为 7292723（一动不动）

### 2. 根因：小分配全被塞进内部 DRAM
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` —— **所有 <=4KB 的 malloc 一律进内部 DRAM**。
而布局树节点、text_content、href/class/style 拷贝**全都是小分配**，于是
"稀有资源被挤爆，充裕资源睡大觉"。

修法（`tools/fix_psram_alloc.py`）：新增 `tb_alloc/tb_calloc`（PSRAM 优先，malloc 兜底），
替换 `safe_strdup` / `safe_strndup` / `layout_node_create` / dom_renderer 的 text_content。
⚠️ ESP-IDF 的 `free()` 等价于 `heap_caps_free()`，所以调用方一个都不用改。

**效果（实测）**：解析阶段 DRAM 谷值
| | 改前 | 改后 |
|---|---|---|
| bing esp32 | 205~218 KB | **246 KB** |
| 单页解析开销 | ~40 KB | **~4 KB** |

### 3. 页面缓存（URI → HTML，PSRAM，带 TTL）
- 新增 `tactilebrowser_parse_html_buffer()`：跳过下载，直接拿内存 HTML 建布局树（两阶段，不碰 LVGL）。
- `cache_download_html()` 包装原下载器，下载完顺手往 PSRAM 塞一份拷贝（引擎会 free 原 buffer）。
- 3 格 / 单页上限 400KB / **TTL 5 分钟**，满了挤最老的。
- 实测：`cache put (58281 B)` → 二次访问 `cache hit ... age 60s`，**不再联网**。

### 4. 下载：LittleFS
底栏「下载」键 + 串口命令 `dl` → `BrowserScreen_download()` 写 `/p<hash>.html`。
⚠️ 首次必须格式化（`LittleFS.begin(false)` 失败 → `begin(true)`），实测
`已保存 59976 B（剩余 1468 KB）`，分区约 1.5MB。

### 5. 底栏重排（取消隐藏分组）
旧：组 A（退出/网址/加载）↔ 组 B（后退/前进/首页/加载/退出），靠三点键切换 —— 手指常按空。
新：**一组常驻**
- y=410：URL 栏整行 476×30（原来只有 300px，退出键删掉后腾出来的）
- y=444：后退(0) 前进(80) 首页(160) 刷新(240) **下载(320)** ⋯(404)
- 三点键 = **回到我们的搜索首页**（唯一的"退出"）；整个浏览器退出走左滑手势
- 原 nav 行第 5 格「退出」→「下载」；组 A 的门+箭头退出键删除

### 6. 其它
- 新增 `Icon::Download`（向下箭头+底线）；`toast()` 提示条**不带定时器**（定时器不属于
  对象树容易漏删），靠下次导航/回首页顺手隐藏。
- 提示条反馈：`已保存 N B (剩余 N KB)` / `页面已释放，请刷新后再下载`。
- 工具：`probe_nav2.py`（多页连翻+崩溃检测）、`probe_dl.py`（下载测试）。
"""

t = io.open(P, encoding="utf-8").read()
if "内存优化 + 页面缓存" in t:
    print("ALREADY")
else:
    io.open(P, "a", encoding="utf-8").write(ADD)
    print("appended")

v = io.open(P, encoding="utf-8").read()
print("len =", len(v))
print("has section:", "内存优化 + 页面缓存" in v)
