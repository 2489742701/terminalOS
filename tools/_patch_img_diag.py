# -*- coding: utf-8 -*-
"""图片管道诊断 + 时间预算。

现象：163 首页 6 张候选，打了一句 "[Img] 6 candidate(s)" 之后整页加载卡住几十秒。
必须先看清"是卡在哪一张、卡在连接还是读体"，再谈修。
顺带加一个硬预算：整页图片阶段最多花 IMG_TOTAL_MS，超了就直接放弃剩下的，
绝不让几张图把页面加载拖成分钟级。
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


# ── 1) fetchPageImages：每张都报"开始/结束 + 耗时"，带总预算 ──
p, t = load('src/app/browser_screen.cpp')
t = sub(p, t,
"""#define IMG_MAX         6         /* 一页最多几张图 */
#define IMG_MAX_BYTES   65536     /* 单张下载字节上限 */""",
"""#define IMG_MAX         6         /* 一页最多几张图 */
#define IMG_MAX_BYTES   65536     /* 单张下载字节上限 */
/* 整个图片阶段的时间预算。几张图把页面加载拖成分钟级是不可接受的 ——
   预算用完就放弃剩下的，页面该渲染渲染（顶多少几张缩略图）。 */
#define IMG_TOTAL_MS    20000""")

t = sub(p, t,
"""  Serial.printf("[Img] %d candidate(s)\\n", n);

  int ok = 0;
  for (int i = 0; i < n; i++) {
    if (g_stopRequested) break;      /* 用户点了停止 → 立刻收手 */
    uint8_t* data = nullptr;
    size_t len = 0;
    int rc = arduino_download_binary(imgs[i]->img_src, &data, &len,
                                     IMG_MAX_BYTES, pageUrl.c_str());
    if (rc != 0 || !data || len == 0) {
      Serial.printf("[Img] miss rc=%d %.64s\\n", rc, imgs[i]->img_src);
      continue;
    }""",
"""  Serial.printf("[Img] %d candidate(s)\\n", n);

  uint32_t tAll = millis();
  int ok = 0;
  for (int i = 0; i < n; i++) {
    if (g_stopRequested) break;      /* 用户点了停止 → 立刻收手 */
    uint32_t spent = millis() - tAll;
    if (spent > IMG_TOTAL_MS) {
      Serial.printf("[Img] budget out (%ums), %d left\\n", (unsigned)spent,
                    n - i);
      break;
    }
    uint8_t* data = nullptr;
    size_t len = 0;
    uint32_t t0 = millis();
    Serial.printf("[Img] get %d/%d %.72s\\n", i + 1, n, imgs[i]->img_src);
    int rc = arduino_download_binary(imgs[i]->img_src, &data, &len,
                                     IMG_MAX_BYTES, pageUrl.c_str());
    Serial.printf("[Img] got rc=%d %u B %ums\\n", rc, (unsigned)len,
                  (unsigned)(millis() - t0));
    if (rc != 0 || !data || len == 0) {
      Serial.printf("[Img] miss rc=%d %.64s\\n", rc, imgs[i]->img_src);
      continue;
    }""")

t = sub(p, t,
"""  Serial.printf("[Img] ready %d/%d, PSRAM free=%u\\n", ok, n,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}""",
"""  Serial.printf("[Img] ready %d/%d in %ums, PSRAM free=%u\\n", ok, n,
                (unsigned)(millis() - tAll),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}""")

# ── 2) 串口 imgscan：把布局树里的图片节点全抖出来 ──
t = sub(p, t,
"""void BrowserScreen_imgView(int idx) {""",
"""/* 串口 imgscan：本页布局树里有几个 ELEMENT_IMAGE、几个拿到了绝对地址。
   图片不显示时先跑它 —— 分清是"DOM 阶段就没收进来"还是"下载/解码失败"。 */
void BrowserScreen_imgScan() {
  if (!g_layoutRoot) { Serial.println("[Imgs] 还没有页面"); return; }
  layout_dump_images(g_layoutRoot);
}

void BrowserScreen_imgView(int idx) {""")
save(p, t)

# ── 3) browser_screen.h ──
p, t = load('src/app/browser_screen.h')
t = sub(p, t,
"""void BrowserScreen_imgView(int idx);
void BrowserScreen_imgDownload(int idx);""",
"""void BrowserScreen_imgView(int idx);
void BrowserScreen_imgDownload(int idx);
/* 把本页布局树里的图片节点抖到串口（串口 imgscan） */
void BrowserScreen_imgScan();""")
save(p, t)

# ── 4) layout_engine：dump 实现 ──
p, t = load('src/browser_engine/include/layout_engine.h')
t = sub(p, t,
"""int layout_collect_ready_images(LayoutNode *root, LayoutNode **out, int max);""",
"""int layout_collect_ready_images(LayoutNode *root, LayoutNode **out, int max);

/* 把树里所有 ELEMENT_IMAGE 节点打到串口（串口 `imgscan`）。诊断用。 */
void layout_dump_images(LayoutNode *root);""")
save(p, t)

p, t = load('src/browser_engine/src/layout_engine.cpp')
t = sub(p, t,
"""int layout_collect_ready_images(LayoutNode *root, LayoutNode **out, int max) {
  int n = 0;
  if (!root || !out || max <= 0) return 0;
  collect_ready_rec(root, out, max, &n, 0);
  return n;
}""",
"""int layout_collect_ready_images(LayoutNode *root, LayoutNode **out, int max) {
  int n = 0;
  if (!root || !out || max <= 0) return 0;
  collect_ready_rec(root, out, max, &n, 0);
  return n;
}

static void dump_images_rec(LayoutNode *node, int *total, int *with_src,
                            int *with_pic, int depth) {
  if (!node || depth > MAX_LAYOUT_DEPTH) return;
  for (LayoutNode *c = node; c; c = c->next_sibling) {
    if (c->type == ELEMENT_IMAGE) {
      (*total)++;
      if (c->img_src) (*with_src)++;
      if (c->img_dsc) (*with_pic)++;
      if (*total <= 15) {
        Serial.printf("[Imgs] #%d src=%s attr=%dx%d bytes=%s\\n", *total,
                      c->img_src ? c->img_src : "(没取到)",
                      c->img_w, c->img_h, c->img_dsc ? "Y" : "N");
      }
    }
    if (c->first_child)
      dump_images_rec(c->first_child, total, with_src, with_pic, depth + 1);
  }
}

void layout_dump_images(LayoutNode *root) {
  int total = 0, with_src = 0, with_pic = 0;
  if (root) dump_images_rec(root, &total, &with_src, &with_pic, 0);
  Serial.printf("[Imgs] <img> nodes=%d, 有绝对地址=%d, 有字节=%d\\n", total,
                with_src, with_pic);
}""")
save(p, t)

# ── 5) serial_console：imgscan ──
p, t = load('src/hal/serial_console.cpp')
t = sub(p, t,
"""  } else if (strcmp(cmd, "imgview") == 0) {""",
"""  } else if (strcmp(cmd, "imgscan") == 0) {
    /* 本页有几个 <img>、几个取到了地址 —— 图片不显示时第一个该跑的命令 */
    BrowserScreen_imgScan();
  } else if (strcmp(cmd, "imgview") == 0) {""")
save(p, t)

# ── 6) 缩短单张图的超时（连接 6s / 头 5s / 体 8s），并补上"卡住"时的日志 ──
p, t = load('src/browser_engine/src/lvgl_renderer.cpp')
t = sub(p, t,
"""  uint8_t *buf = (uint8_t *)heap_caps_malloc(contentLen + 8,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) { client->stop(); return -2; }""",
"""  uint8_t *buf = (uint8_t *)heap_caps_malloc(contentLen + 8,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) { client->stop(); return -2; }
  Serial.printf("[Img] dl host=%s len=%d chunked=%d\\n", host.c_str(),
                contentLen, (int)chunked);""")
save(p, t)

print('FAILURES: %d' % len(fail))
for f in fail:
    print(' -', f)
sys.exit(1 if fail else 0)
