"""更新项目长期记忆：PSRAM 分流 / 页面缓存 / 底栏布局。"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\MEMORY.md"

ADD = """
## 内存：PSRAM 分流（本项目最重要的一条）
- ⚠️ **`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`：所有 <=4KB 的 malloc 一律进内部 DRAM。**
  布局树节点 / text_content / href·class·style 拷贝**全都是小分配** → 解析一页 DRAM 掉 40KB，
  而 PSRAM 7.29MB 纹丝不动。这是"第二页崩"的真正隐患。
- 已修：新增 `tb_alloc/tb_calloc`（`heap_caps_malloc(MALLOC_CAP_SPIRAM|8BIT)`，malloc 兜底），
  用于 `safe_strdup` / `safe_strndup` / `layout_node_create` / dom_renderer 的 text_content。
  **解析开销 40KB → 4KB**（实测 DRAM 谷值 205KB → 246KB）。
- ⚠️ ESP-IDF 里 `free()` == `heap_caps_free()`，所以调用方**不用改** free。
- 判据：PSRAM free 恒定不变 = LVGL 池/缓存是大块预分配；DRAM 逐页下滑 = 有小分配漏走 DRAM。

## 浏览器：页面缓存 + 下载
- **缓存**：URI→HTML 存 PSRAM（3 格 / 单页 400KB / **TTL 5 分钟**，满了挤最老的）。
  命中走 `tactilebrowser_parse_html_buffer()`（跳过下载，两阶段，不碰 LVGL）。
  下载器包装 `cache_download_html()` 顺手存一份（引擎会 free 原 buffer，必须拷）。
- **下载**：底栏「下载」键 + 串口命令 **`dl`** → 写 LittleFS `/p<hash>.html`。
  ⚠️ 首次 `LittleFS.begin(false)` 会失败，必须 `begin(true)` 格式化；分区约 1.5MB。
- 渲染完 HTML 原文由引擎内部 free（本来就是），只留 URI + 缓存拷贝。

## 浏览器底栏布局（2026-09-23 定稿）
- **取消隐藏分组**（组 A ↔ 组 B 靠三点切换，手指常按空）。
- y=410：URL 栏整行 476×30；y=444：后退(0) 前进(80) 首页(160) 刷新(240) **下载(320)** ⋯(404)。
- **三点键 = 回我们的搜索首页**（唯一的"退出"）；退出浏览器走左滑手势。
- 提示条 `toast()` **不带定时器**（定时器不属于对象树，容易漏删），靠下次导航/回首页隐藏。
"""

t = io.open(P, encoding="utf-8", newline="").read()
out = []
if "PSRAM 分流" in t:
    out.append("ALREADY")
else:
    crlf = "\r\n" in t
    lines = t.replace("\r\n", "\n").split("\n")
    A = "## 待办"
    idx = [i for i, l in enumerate(lines) if l.strip() == A]
    if not idx:
        out.append("*** anchor missing ***")
    else:
        i = idx[0]
        lines[i:i] = ADD.strip("\n").split("\n") + [""]
        t = "\n".join(lines)
        if crlf:
            t = t.replace("\n", "\r\n")
        io.open(P, "w", encoding="utf-8", newline="").write(t)
        out.append("inserted before 待办")

v = io.open(P, encoding="utf-8").read()
out.append("len=%d" % len(v))
out.append("has psram: %s" % ("tb_alloc" in v))
out.append("has cache: %s" % ("PAGE_CACHE" in v or "页面缓存" in v))
print("\n".join(out))
