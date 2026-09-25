# -*- coding: utf-8 -*-
"""文档：缩略图链路（README + docs/09 + LVGL 配置存档）。"""
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


# ══ README ══
p, t = load('README.md')
t = sub(p, t,
"""  - **分段渲染**（2026-09-25）：一次只铺 60 块瓦片，内容末尾给「上一段 / 第 x/y 段 / 下一段」。""",
"""  - **分段渲染**（2026-09-25）：一次只铺 80 块瓦片，内容末尾给「上一段 / 第 x/y 段 / 下一段」。""")

t = sub(p, t,
"""  - 下载当前页（底栏下载键）：**插了 SD 卡优先写卡**，否则退回 LittleFS""",
"""  - **网页图片（缩略图 → 全屏 → 另存）**（2026-09-25）：
    `<img>` 一律重采样成 **96px 小图**铺进页面（盒子滤波，糊但看得清），
    点一下开**全屏大图**（小图最多放大到 2 倍），左下角可**另存**（SD 优先 / LittleFS 兜底）。
    - ⚠️ 图片必须在**解析之前**抓完：解析会把内部 DRAM 吃到只剩几百字节，
      之后 DNS 直接失败（`hostByName(): DNS Failed`），一张图都下不来。
      所以流程是「下 HTML → 扫原始 HTML 抓图 → 解析 → 按 URL 认领回节点」。
    - 每页最多 6 张、单张 ≤64KB、整个图片阶段 ≤20s（`IMG_MAX` / `IMG_TOTAL_MS`）。
    - 串口：`img on|off`、`thumb <px>`、`imgscan`、`imgview <n>`、`imgdl <n>`
  - 下载当前页（底栏下载键）：**插了 SD 卡优先写卡**，否则退回 LittleFS""")
save(p, t)

# ══ docs/09：M 段 ══
p, t = load('docs/09-坑点速查表.md')
SEC = """## M · 网页图片：缩略图 / 全屏 / 另存（2026-09-25）

链路：`<img>` → 后台任务下原始字节（`img_dsc`）→ **渲染前**在 UI 线程重采样成
小图（`img_thumb`，默认 96px）→ 页面上一小块 → 点开全屏大图 → 另存。

### M1 · ⛔⛔ 图片必须在"解析之前"抓，顺序不能换
**症状**：页面文字正常，但一张图都没有。串口里每张图都是
`[Img] miss rc=-1`，前面还有一行 `[E][WiFiGeneric.cpp:1583] hostByName(): DNS Failed`。

**根因**：解析 + 建布局树会把**内部 DRAM** 从 ~94KB 吃到只剩几百字节，
之后 DNS 解析发不出去。跟 TLS 一点关系都没有 —— 报错在 DNS 那一步就断了。

**正确顺序**（`fetch_task`）：
```
下 HTML → 扫原始 HTML 里的 <img> 地址 → 趁 DRAM 富余把图抓完
        → 解析 + 建布局树 → layout_assign_images() 按 URL 认领回节点
```
代价是要在原始 HTML 上做一遍朴素扫描（`scanImageUrls`），
属性优先级必须跟 `dom_renderer` 一致（`src` > `data-src` > `data-original`），
否则两边取到不同 URL，认领时对不上。

### M2 · ⛔⛔ LVGL 内存池大小决定 PNG 能不能解
`lodepng` 的分配器是 `lodepng_malloc() { return lv_mem_alloc(size); }`
（`lvgl/src/extra/libs/png/lodepng.c`）—— 走的是 **LVGL 自己的池**，不是 PSRAM 直通。
池原本 `LV_MEM_SIZE = 128KB`，而一张 300x200 的 PNG 光解码输出就要
300*200*4 = **240KB** → `lv_mem_alloc` 必失败 → PNG 解码器 `open_cb` 返回 INV。
**已改成 768KB**（池本身就在 PSRAM，多占 640KB，PSRAM 还有 6MB+）。

### M3 · ⛔⛔ 内建解码器会"递假像素"，而且不报错
`lv_img_decoder_open` 是**依次问** SJPG → PNG → 内建。
内建解码器的受理范围是 `CF_BUILT_IN_FIRST(TRUE_COLOR=4) .. CF_BUILT_IN_LAST(ALPHA_8BIT)`。
如果源 dsc 的 `header.cf` 填了 `LV_IMG_CF_TRUE_COLOR_ALPHA(5)`，那么当 PNG
解码器失败（见 M2）时，**内建会接管**，而它对 VARIABLE 源的做法是：
```c
dsc->img_data = img_dsc->data;   /* 把压缩的 PNG 原文当像素交出来 */
```
于是我们按 300x200x3 去读一个 8KB 的缓冲区 → 屏幕上**一坨彩色乱码**，
而 `dec.header.w/h` 还是我们自己填的 300x200，日志看着一切正常。

**修**：源 dsc 的 PNG 用 `LV_IMG_CF_RAW_ALPHA(2)`（这本来就是 LVGL 对 png
**文件**源的官方写法），2 < 4 落在内建受理范围外 —— 只有 PNG 解码器能认领，
失败就是干净地失败。另加一条兜底：`img_data == dsc->data` 一律当"没解出来"。

### M4 · JPEG 解码降采样走 `lv_img_header_t.reserved`
tjpgd 的 `jd_decomp()` 支持 1/1、1/2、1/4、1/8（`tjpgdcnf.h` 里 `JD_USE_SCALE=1`），
但 LVGL 固定传 0。网页图动辄上千像素宽，原图 `w*h*3` 的 RGB888 中间缓冲直接吃穿 PSRAM。
补丁把档位放在 **`header.reserved`（2 bit，正好 0~3）随图携带**，
`jd_decomp(..., scale)` 且 `header->w/h` 一并右移。
⚠️ **别用全局变量存档位**：解码是懒加载的，LVGL 图片缓存淘汰后会重新 open，
那时全局变量早被别的图改过了 —— 会缩错。
存档脚本：`tools/patch_lvgl_jpeg_scale.py`。同脚本还把 `is_jpg()` 从
"必须有 JFIF APP0"放宽到 `FF D8 FF`——原实现会拒掉 Exif / 无 APP0 的一大半 JPEG。

### M5 · 缩略图瓦片会被导航胶囊挤出第 1 段
163 首页的导航条摊平出 ~58 个胶囊，`PAGE_SEG_TILES=60` 时前 58 块全被吃光，
图片节点排在 60 名开外 → **第 1 段一块缩略图都铺不出来**，现象是"页面上根本找不到图"。
已把 `PAGE_SEG_TILES` 提到 **80**。换页面如果还是看不到图，先跑 `imgscan`
看有几张、再翻段。

### M6 · 文本节点用裸 malloc = DRAM 黑洞
`dom_renderer` 建文本节点时曾经是 `malloc(trimmed_len+1)`。
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` 会把 ≤4KB 的 malloc **一律塞进内部 DRAM**，
而文本节点是布局树里数量最多的东西（一页几百个）→ 解析一页 DRAM 掉几十 KB。
它兄弟（`safe_strdup` / `safe_strndup` / 元素文本）早就走 `tb_alloc` 了，只有这条漏了。
改 `tb_alloc` 的同时补了判空 —— 原代码不判空就 `memcpy`，DRAM 见底时就是崩溃。

### M7 · 图片重采样为什么不用 `lv_img_set_zoom`
两条都踩过：
1. JPEG 在 LVGL 里是 RAW + 逐行 `read_line`（`img_data` 为 NULL）。LVGL 缩放时
   拿**每一行**当整张图单独变换，画出来上下错位；
2. zoom 是最近邻，缩到 1/5 就是马赛克 —— 而我们要的是"糊但看得清"。
所以自己在 `tb_image_resample()` 里把像素抠出来做**区域平均**（box filter）：
· JPEG → `lv_img_decoder_read_line` 逐行取（出来已经是 RGB565，2 字节/像素）；
· PNG  → 直接从 `dec.img_data` 取（RGB565+alpha，3 字节/像素）。
全屏大图 = 同一函数换个 box。

### M8 · 诊断命令
`img`（开关）/ `thumb <px>`（缩略图尺寸）/ `imgscan`（本页几个 `<img>`、几个取到地址）/
`imgview <n>` / `imgdl <n>` / `imgtest <url>`（单张图下载+解码+重采样全链路）。

---

"""
t = sub(p, t, """## 附：一句话红线""", SEC + """## 附：一句话红线""")
save(p, t)

# ══ tools/enable_lvgl_decoders.py：把 LV_MEM_SIZE 也记进去 ══
p, t = load('tools/enable_lvgl_decoders.py')
t = sub(p, t,
"""WANT = {
    "LV_USE_SJPG": 1,   # tjpgd：能解**普通 JPEG**（lv_sjpg.c 里有 is_jpg 分支）
    "LV_USE_PNG": 1,    # lodepng：网页图标/logo 有不少 PNG
    "LV_USE_BMP": 0,
    "LV_USE_GIF": 0,    # gifdec 要一直跑动画，480x480 上太贵，先不开
}""",
"""WANT = {
    "LV_USE_SJPG": 1,   # tjpgd：能解**普通 JPEG**（lv_sjpg.c 里有 is_jpg 分支）
    "LV_USE_PNG": 1,    # lodepng：网页图标/logo 有不少 PNG
    "LV_USE_BMP": 0,
    "LV_USE_GIF": 0,    # gifdec 要一直跑动画，480x480 上太贵，先不开
}

# ⚠️ 2026-09-25：LV_MEM_SIZE 必须从 128KB 提到 768KB。
#   lodepng 的分配器是 `lodepng_malloc() { return lv_mem_alloc(size); }` ——
#   走的是 **LVGL 自己的池**。一张 300x200 的 PNG 光解码输出就要 300*200*4 =
#   240KB，128KB 的池必然分配失败 → PNG 解码器返回 INV → 再被内建解码器
#   "接管"（内建会把压缩的 PNG 原文当像素交出去）→ 屏幕上一坨彩色乱码。
#   池本身就在 PSRAM（LV_MEM_POOL_IN_PSRAM=1），多占 640KB，PSRAM 还有 6MB+。
MEM_WANT = {"LV_MEM_SIZE": "(768U*1024U)"}
for k, v in MEM_WANT.items():
    old = "#define %s (128U*1024U)" % k
    new = "#define %s %s" % (k, v)
    if new in s:
        print("already: %s" % new.strip())
    elif old in s:
        s = s.replace(old, new, 1)
        print("set:     %s" % new.strip())
    else:
        print("MISS:    %s" % k)""")
save(p, t)

print('FAILURES: %d' % len(fail))
for f in fail:
    print(' -', f)
sys.exit(1 if fail else 0)
