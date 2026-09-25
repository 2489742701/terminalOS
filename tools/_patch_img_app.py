# -*- coding: utf-8 -*-
"""缩略图链路 —— app 侧补丁（2026-09-25）"""
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
        fail.append('%s: count=%d :: %s' % (p, cnt, old.split('\n')[0][:60]))
        return text
    return text.replace(o, n, 1)


# ────────────────────────────────────────────────────────────────────
# browser_screen.cpp
# ────────────────────────────────────────────────────────────────────
p, t = load('src/app/browser_screen.cpp')

IMG_BLOCK = r'''
/* ═══ 缩略图（2026-09-25）══════════════════════════════════════════════════
 * <img> 以前只被映射成 ELEMENT_IMAGE，之后渲染阶段谁都不认识 ——
 * 页面上一张图都看不见（这是待办清单里第 ④ 项一直没动的原因）。
 *
 * 现在的链路：
 *   1) dom_renderer 把 src / data-src / data-original 解析成绝对地址写进 img_src
 *      （DOM 阶段**绝不联网**，否则一个页面几十张图 = 几十次 TLS 握手）；
 *   2) 后台 fetch 任务解析完布局树之后，挑最多 IMG_MAX 张小图下载，
 *      每张有字节上限；
 *   3) 下载完先用文件头看宽高再挡一道 —— tjpgd 在 decoder_open 里一次性分配
 *      w*h*3 的 RGB888 缓冲，一张大图就能把 PSRAM 吃穿；
 *   4) 过了闸的才包成 lv_img_dsc_t 填进 img_dsc，渲染时变成一块瓦片（lv_img）。
 *
 * 门槛全部集中在下面四个宏，改一个数就能调松紧。
 * 没有 img_dsc 的图片节点**一块瓦片都不占**，所以"图下不下来"不会撑变形，
 * 也不会吃掉 MAX_WIDGETS 的配额。 */
#define IMG_MAX         6         /* 一页最多几张图 */
#define IMG_MAX_BYTES   65536     /* 单张下载字节上限 */
#define IMG_MAX_W       464       /* 解码后宽度上限（= 内容区宽，超了戳出右边） */
#define IMG_MAX_PIXELS  110000    /* 解码后像素上限：w*h*3 ≈ 330KB 中间缓冲 */

static bool g_imgEnabled = true;  /* 串口 `img on|off`；慢页面可以临时关掉 */

/* 跑在后台 fetch 任务里（Core 0）：只下载、只填 img_dsc，**不碰 LVGL** */
static void fetchPageImages(const String& pageUrl) {
  if (!g_layoutRoot) return;
  LayoutNode* imgs[IMG_MAX];
  int n = layout_collect_images(g_layoutRoot, imgs, IMG_MAX);
  if (n <= 0) return;
  Serial.printf("[Img] %d candidate(s)\n", n);

  int ok = 0;
  for (int i = 0; i < n; i++) {
    if (g_stopRequested) break;      /* 用户点了停止 → 立刻收手 */
    uint8_t* data = nullptr;
    size_t len = 0;
    int rc = arduino_download_binary(imgs[i]->img_src, &data, &len,
                                     IMG_MAX_BYTES, pageUrl.c_str());
    if (rc != 0 || !data || len == 0) {
      Serial.printf("[Img] miss rc=%d %.64s\n", rc, imgs[i]->img_src);
      continue;
    }
    int w = 0, h = 0;
    if (!tb_image_peek_size(data, len, &w, &h)) {
      Serial.printf("[Img] drop unknown fmt %u B %.48s\n",
                    (unsigned)len, imgs[i]->img_src);
      heap_caps_free(data);
      continue;
    }
    if (w < 16 || h < 16) {
      Serial.printf("[Img] drop tiny %dx%d\n", w, h);
      heap_caps_free(data);
      continue;
    }
    if (w > IMG_MAX_W || (long)w * (long)h > IMG_MAX_PIXELS) {
      Serial.printf("[Img] drop too big %dx%d (%u B)\n", w, h, (unsigned)len);
      heap_caps_free(data);
      continue;
    }
    void* dsc = tb_image_dsc_create(data, len);
    if (!dsc) { heap_caps_free(data); continue; }
    imgs[i]->img_dsc = dsc;
    ok++;
    Serial.printf("[Img] ok %dx%d %u B  %.52s\n", w, h, (unsigned)len,
                  imgs[i]->img_src);
  }
  Serial.printf("[Img] ready %d/%d, PSRAM free=%u\n", ok, n,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

'''
t = sub(p, t,
"""/* ── 后台下载解析任务（Phase 1，不触碰 LVGL）──""",
IMG_BLOCK.lstrip('\n') + """/* ── 后台下载解析任务（Phase 1，不触碰 LVGL）──""")

t = sub(p, t,
"""    Serial.printf("[Browser] fetch_task done: result=%d layout=%p DRAM free=%u\\n",""",
"""    /* 缩略图：布局树有了才谈得上挑图。放在这里而不是 DOM 阶段，是为了让
       "页面文字先出来"和"图片慢慢下"分开 —— 图下不到也只是没有图，不影响正文。 */
    if (g_taskResult == RENDER_SUCCESS && g_layoutRoot && !g_stopRequested &&
        g_imgEnabled) {
      fetchPageImages(url);
    }

    Serial.printf("[Browser] fetch_task done: result=%d layout=%p DRAM free=%u\\n",""")

t = sub(p, t,
"""int  BrowserScreen_segStart();""",
"""int  BrowserScreen_segStart();""")

# 外部链接的三个入口放在匿名 namespace 之后（segGo 附近）
t = sub(p, t,
"""void BrowserScreen_segGo(int start) {""",
"""/* 串口 `imgtest <url>`：只下这一张，走完整的 下载 → 看头 → 交 LVGL 解码器认一遍
   （不建任何控件），专门用来回答"这张图到底能不能解"。
   ⚠️ 必须在 UI 线程调（里面碰 LVGL），串口控制台就是在 loopTask 里跑的。 */
void BrowserScreen_imgTest(const char* url) {
  if (!url || !*url) { Serial.println("[Img] usage: imgtest <url>"); return; }
  uint8_t* data = nullptr;
  size_t len = 0;
  uint32_t t0 = millis();
  int rc = arduino_download_binary(url, &data, &len, IMG_MAX_BYTES, nullptr);
  Serial.printf("[Img] test rc=%d len=%u (%ums)\\n", rc, (unsigned)len,
                (unsigned)(millis() - t0));
  if (rc != 0 || !data) { Serial.println("[Img] test: download failed"); return; }

  int w = 0, h = 0;
  bool known = tb_image_peek_size(data, len, &w, &h);
  Serial.printf("[Img] test peek=%d %dx%d\\n", (int)known, w, h);
  if (known && (w > IMG_MAX_W || (long)w * (long)h > IMG_MAX_PIXELS)) {
    Serial.printf("[Img] test: over budget (%dx%d vs %dx%ld)\\n", w, h,
                  IMG_MAX_W, (long)IMG_MAX_PIXELS);
  }

  void* dsc = tb_image_dsc_create(data, len);
  if (!dsc) { heap_caps_free(data); Serial.println("[Img] test: dsc alloc failed"); return; }
  lv_img_header_t hdr;
  memset(&hdr, 0, sizeof(hdr));
  lv_res_t r = lv_img_decoder_get_info((const lv_img_dsc_t*)dsc, &hdr);
  Serial.printf("[Img] test decoder: res=%d cf=%u %ux%u\\n", (int)r,
                (unsigned)hdr.cf, (unsigned)hdr.w, (unsigned)hdr.h);
  Serial.printf("[Img] test PSRAM free=%u\\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  tb_image_dsc_free(dsc);
}

void BrowserScreen_setImages(bool on) {
  g_imgEnabled = on;
  Serial.printf("[Img] images %s (下次加载生效)\\n", on ? "ON" : "OFF");
}
bool BrowserScreen_imagesEnabled() { return g_imgEnabled; }

void BrowserScreen_segGo(int start) {""")
save(p, t)

# ────────────────────────────────────────────────────────────────────
# browser_screen.h
# ────────────────────────────────────────────────────────────────────
p, t = load('src/app/browser_screen.h')
t = sub(p, t,
"""void BrowserScreen_segGo(int start);
void BrowserScreen_segDump();
int  BrowserScreen_segStart();
int  BrowserScreen_segSize();""",
"""void BrowserScreen_segGo(int start);
void BrowserScreen_segDump();
int  BrowserScreen_segStart();
int  BrowserScreen_segSize();

/* ── 缩略图（串口 `img on|off` / `imgtest <url>`）── */
void BrowserScreen_setImages(bool on);
bool BrowserScreen_imagesEnabled();
void BrowserScreen_imgTest(const char* url);""")
save(p, t)

# ────────────────────────────────────────────────────────────────────
# serial_console.cpp
# ────────────────────────────────────────────────────────────────────
p, t = load('src/hal/serial_console.cpp')
t = sub(p, t,
"""  } else if (strcmp(cmd, "seg") == 0) {""",
"""  } else if (strcmp(cmd, "img") == 0) {
    /* 缩略图开关：img / img on / img off。
       图多的页面会把加载时间拉长（每张一次 TLS 握手），关掉可以救回来。 */
    if (!arg || !*arg) {
      Serial.printf("[Img] images %s\\n",
                    BrowserScreen_imagesEnabled() ? "ON" : "OFF");
    } else {
      BrowserScreen_setImages(!(strcmp(arg, "off") == 0 ||
                                strcmp(arg, "0") == 0));
    }
  } else if (strcmp(cmd, "imgtest") == 0) {
    /* 单张图连通性+可解码性验证：imgtest https://.../a.jpg */
    BrowserScreen_imgTest(arg);
  } else if (strcmp(cmd, "seg") == 0) {""")
save(p, t)

print('FAILURES: %d' % len(fail))
for f in fail:
    print(' -', f)
sys.exit(1 if fail else 0)
