"""补插页面缓存块（上一轮的锚点没匹配上，改用行号定位）"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"

CACHE = r'''
/* ══ 页面缓存（PSRAM，带 TTL）══
 * 渲染完成后 HTML 原文由引擎内部 free（本来就是），这里额外留一份 URI→HTML 的
 * 拷贝放在 PSRAM：前进/后退/重访命中就跳过 TLS+下载，直接建布局树。
 * ⚠️ 只缓存 PAGE_CACHE_MAX_ENTRY 字节以内的大页；超了就不缓存（PSRAM 也要省着用）。 */
static const int PAGE_CACHE_SLOTS = 3;                      /* 缓存几页 */
static const uint32_t PAGE_CACHE_TTL_MS = 5 * 60 * 1000UL; /* 5 分钟有效期 */
static const size_t PAGE_CACHE_MAX_ENTRY = 400 * 1024;      /* 单页上限 */

struct PageCacheEntry {
  String uri;
  uint8_t* data = nullptr;
  size_t len = 0;
  uint32_t ts = 0;
};
static PageCacheEntry g_pageCache[PAGE_CACHE_SLOTS];

static void pageCacheFree(int i) {
  if (i < 0 || i >= PAGE_CACHE_SLOTS) return;
  if (g_pageCache[i].data) {
    heap_caps_free(g_pageCache[i].data);
    g_pageCache[i].data = nullptr;
  }
  g_pageCache[i].uri = String();
  g_pageCache[i].len = 0;
  g_pageCache[i].ts = 0;
}

/* 找一页：命中且未过期返回下标，否则 -1（顺手丢掉过期的） */
static int pageCacheFind(const String& uri) {
  if (!uri.length()) return -1;
  const uint32_t now = millis();
  for (int i = 0; i < PAGE_CACHE_SLOTS; i++) {
    if (!g_pageCache[i].data) continue;
    if (g_pageCache[i].uri != uri) continue;
    if (now - g_pageCache[i].ts > PAGE_CACHE_TTL_MS) {
      Serial.printf("[Browser] cache expired: %s\n", uri.c_str());
      pageCacheFree(i);
      return -1;
    }
    return i;
  }
  return -1;
}

/* 存一页：满了就挤掉最老的那格（LRU 的穷亲戚，够用） */
static void pageCachePut(const String& uri, const uint8_t* data, size_t len) {
  if (!uri.length() || !data || len == 0) return;
  if (len > PAGE_CACHE_MAX_ENTRY) {
    Serial.printf("[Browser] cache skip (too big): %u B\n", (unsigned)len);
    return;
  }
  int slot = -1;
  for (int i = 0; i < PAGE_CACHE_SLOTS; i++)
    if (g_pageCache[i].uri == uri) { slot = i; break; }
  if (slot < 0)
    for (int i = 0; i < PAGE_CACHE_SLOTS; i++)
      if (!g_pageCache[i].data) { slot = i; break; }
  if (slot < 0) {                       /* 全满：挤掉最老的 */
    slot = 0;
    for (int i = 1; i < PAGE_CACHE_SLOTS; i++)
      if (g_pageCache[i].ts < g_pageCache[slot].ts) slot = i;
  }
  pageCacheFree(slot);
  uint8_t* copy = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!copy) {
    Serial.println("[Browser] cache: PSRAM alloc failed");
    return;
  }
  memcpy(copy, data, len);
  g_pageCache[slot].uri = uri;
  g_pageCache[slot].data = copy;
  g_pageCache[slot].len = len;
  g_pageCache[slot].ts = millis();
  Serial.printf("[Browser] cache put: %s (%u B)\n", uri.c_str(), (unsigned)len);
}

/* 下载器包装：真下载完之后顺手塞一份进缓存。
   引擎拿到 buffer 后会自己 free，所以这里必须拷一份。 */
static RenderResult cache_download_html(const char* url, MemoryBuffer* buffer) {
  RenderResult r = arduino_download_html(url, buffer);
  if (r == RENDER_SUCCESS && buffer && buffer->data && buffer->size > 0)
    pageCachePut(String(url), (const uint8_t*)buffer->data, buffer->size);
  return r;
}

/* ══ 下载：把当前页存进 LittleFS ══ */
static uint32_t url_hash(const String& s) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < s.length(); i++) {
    h ^= (uint8_t)s[i];
    h *= 16777619u;
  }
  return h;
}

/* 提示条：不带定时器（定时器不属于对象树，容易漏删），靠下次导航/回首页时顺手隐藏 */
static lv_obj_t* g_toast = nullptr;
static void toast(const char* msg) {
  Serial.printf("[Browser] toast: %s\n", msg);
  if (!g_toast || !lv_obj_is_valid(g_toast)) return;
  lv_label_set_text(g_toast, msg);
  lv_obj_clear_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
}
static void toastHide() {
  if (g_toast && lv_obj_is_valid(g_toast)) lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
}

static void downloadCurrentPage() {
  if (!g_currentUrl.length()) { toast("还没有页面"); return; }
  int ci = pageCacheFind(g_currentUrl);
  if (ci < 0) { toast("页面已释放，请刷新后再下载"); return; }

  if (!LittleFS.begin(false)) {
    Serial.println("[Browser] LittleFS mount failed, formatting...");
    if (!LittleFS.begin(true)) { toast("存储不可用"); return; }
  }
  char path[48];
  snprintf(path, sizeof(path), "/p%08x.html", (unsigned)url_hash(g_currentUrl));
  File f = LittleFS.open(path, "w");
  if (!f) { toast("写文件失败"); LittleFS.end(); return; }
  size_t w = f.write(g_pageCache[ci].data, g_pageCache[ci].len);
  f.close();
  char msg[96];
  snprintf(msg, sizeof(msg), "已保存 %u B (剩余 %u KB)", (unsigned)w,
           (unsigned)((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024));
  Serial.printf("[Browser] download: %s -> %s (%u B)\n", g_currentUrl.c_str(),
                path, (unsigned)w);
  LittleFS.end();
  toast(msg);
}

static void download_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  downloadCurrentPage();
}
'''

t = io.open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in t
lines = t.replace("\r\n", "\n").split("\n")
log = []

if "PAGE_CACHE_TTL_MS" in "\n".join(lines):
    log.append("ALREADY")
else:
    idx = [i for i, l in enumerate(lines) if "进度回调" in l]
    if not idx:
        log.append("*** anchor missing ***")
    else:
        i = idx[0]
        lines[i:i] = CACHE.split("\n")
        log.append("inserted at line %d" % (i + 1))

# 清掉残留的 g_navModeB 声明（分组已取消）
lines = [l for l in lines if l.strip() != "static bool g_navModeB = false;"]
txt = "\n".join(lines)
txt = txt.replace("/* 底栏最右：组 A ↔ 组 B */", "/* 底栏最右：三点 = 回到我们的搜索首页 */")

out = txt
if crlf:
    out = out.replace("\n", "\r\n")
io.open(P, "w", encoding="utf-8", newline="").write(out)

v = io.open(P, encoding="utf-8").read()
for k in ["PAGE_CACHE_TTL_MS", "downloadCurrentPage", "pageCachePut",
          "cache_download_html", "download_cb", "url_hash"]:
    log.append("verify %-24s %s" % (k, k in v))
log.append("g_navModeB leftover: %d" % v.count("g_navModeB"))
print("\n".join(log))
