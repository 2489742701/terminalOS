"""浏览器：页面缓存(TTL) + 下载存盘 + 底栏按键重排。

三件事：
1. 页面缓存：URI -> HTML 存在 PSRAM，带 TTL；命中就跳过联网直接建布局树。
   渲染完 HTML 原文由引擎内部 free（本来就是），只留 URI + 这份缓存拷贝。
2. 下载：把当前页 HTML 写进 LittleFS。
3. 底栏重排：取消"组 A / 组 B"隐藏切换（手指经常按空），改为
   y=410 整行 URL 栏 + y=444 六个键：后退/前进/首页/刷新/下载/⋯(回我们的首页)。
   原 nav 行第 5 键「退出」改成「下载」；组 A 那个门+箭头退出键删掉 —— 退出只留一个。
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"

log = []
t = io.open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in t
lines = t.replace("\r\n", "\n").split("\n")


def find(sub, start=0):
    for i in range(start, len(lines)):
        if sub in lines[i]:
            return i
    return -1


def repl(old, new, count=1):
    """整文件字符串替换（按 \n 归一化后再拼回去）"""
    global lines
    txt = "\n".join(lines)
    if old not in txt:
        return False
    txt = txt.replace(old, new, count)
    lines = txt.split("\n")
    return True


# ── 1. includes ──
if "#include <LittleFS.h>" not in "\n".join(lines):
    i = find('#include "screensaver.h"')
    if i < 0:
        log.append("include: *** anchor missing ***")
    else:
        lines[i:i] = ["#include <FS.h>", "#include <LittleFS.h>"]
        log.append("include: ok")
else:
    log.append("include: ALREADY")

# ── 2. g_exitBtn -> g_dlBtn ──
i = find("lv_obj_t* g_exitBtn = nullptr;")
if i >= 0:
    lines[i] = "lv_obj_t* g_dlBtn = nullptr;      /* 下载（原「退出」那一格） */"
    log.append("dlBtn decl: ok")
else:
    log.append("dlBtn decl: *** missing ***")

# ── 3. 页面缓存 + 下载：插在 updateNavButtons 之后 ──
CACHE = r'''
/* ══ 页面缓存（PSRAM，带 TTL）══
 * 渲染完成后 HTML 原文由引擎内部 free（本来就是），这里额外留一份 URI→HTML 的
 * 拷贝放在 PSRAM：前进/后退/重访命中就跳过 TLS+下载，直接建布局树。
 * ⚠️ 只缓存到 PAGE_CACHE_MAX_ENTRY 字节以内的大页；超了就不缓存（PSRAM 也要省着用）。 */
static const int PAGE_CACHE_SLOTS = 3;                 /* 缓存几页 */
static const uint32_t PAGE_CACHE_TTL_MS = 5 * 60 * 1000UL;  /* 5 分钟有效期 */
static const size_t PAGE_CACHE_MAX_ENTRY = 400 * 1024; /* 单页上限，超过不缓存 */

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
  for (int i = 0; i < PAGE_CACHE_SLOTS; i++) {
    if (g_pageCache[i].uri == uri) { slot = i; break; }
  }
  if (slot < 0) {
    for (int i = 0; i < PAGE_CACHE_SLOTS; i++) {
      if (!g_pageCache[i].data) { slot = i; break; }
    }
  }
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

if "pageCacheFind" not in "\n".join(lines):
    i = find("/* ── 进度回调（在后台任务中调用，不能触碰 LVGL）──")
    if i < 0:
        log.append("cache: *** anchor missing ***")
    else:
        lines[i:i] = CACHE.split("\n")
        log.append("cache: ok")
else:
    log.append("cache: ALREADY")

# ── 4. 注册包装下载器（两处）──
n = "\n".join(lines).count("tactilebrowser_set_html_downloader(arduino_download_html)")
"\n".join(lines)
txt = "\n".join(lines)
txt = txt.replace("tactilebrowser_set_html_downloader(arduino_download_html)",
                  "tactilebrowser_set_html_downloader(cache_download_html)")
lines = txt.split("\n")
log.append("downloader wrapper: %d replaced" % n)

# ── 5. fetch_task 走缓存 ──
OLD_FETCH = """    g_taskResult = tactilebrowser_download_and_parse(
        url.c_str(), g_browserViewportW, 0, &g_stopRequested, &g_layoutRoot);"""
NEW_FETCH = """    int ci = pageCacheFind(url);
    if (ci >= 0) {
      Serial.printf("[Browser] cache hit: %s (%u B, age %us)\\n",
                    url.c_str(), (unsigned)g_pageCache[ci].len,
                    (unsigned)((millis() - g_pageCache[ci].ts) / 1000));
      g_taskResult = tactilebrowser_parse_html_buffer(
          url.c_str(), (const char*)g_pageCache[ci].data, g_pageCache[ci].len,
          g_browserViewportW, 0, &g_stopRequested, &g_layoutRoot);
    } else {
      g_taskResult = tactilebrowser_download_and_parse(
          url.c_str(), g_browserViewportW, 0, &g_stopRequested, &g_layoutRoot);
    }"""
if OLD_FETCH not in "\n".join(lines):
    log.append("fetch: *** old missing (or already) ***")
else:
    txt = "\n".join(lines).replace(OLD_FETCH, NEW_FETCH, 1)
    lines = txt.split("\n")
    log.append("fetch: ok")

# ── 6. applyBarMode 简化（取消分组隐藏）──
i0 = find("static void applyBarMode() {")
i1 = find("static void switch_bar_cb(lv_event_t* e) {")
if i0 >= 0 and i1 > i0:
    NEW_AB = [
        "static void applyBarMode() {",
        "  /* 分组隐藏已取消：六个键常驻同一行，手指不会按空。",
        "     这里只负责刷新后退/前进/首页的灰态。 */",
        "  updateNavButtons();",
        "}",
        "",
    ]
    lines[i0:i1] = NEW_AB
    log.append("applyBarMode: ok")
else:
    log.append("applyBarMode: *** missing ***")

# ── 7. switch_bar_cb -> 回我们的搜索首页 ──
i0 = find("static void switch_bar_cb(lv_event_t* e) {")
i1 = find("lv_obj_t* BrowserScreen_create() {")
if i0 >= 0 and i1 > i0:
    NEW_SW = [
        "/* 三点键 = 回到我们自己的搜索首页（唯一一个\"退出\"）。",
        "   原来它负责组 A/组 B 切换，现在两组合一，改成回首页；",
        "   整个浏览器的退出交给左滑手势。 */",
        "static void switch_bar_cb(lv_event_t* e) {",
        "  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;",
        "  toastHide();",
        "  showSearchHome();",
        "}",
        "",
    ]
    lines[i0:i1] = NEW_SW
    log.append("switch_cb: ok")
else:
    log.append("switch_cb: *** missing ***")

# ── 8. 删掉组 A 的「退出」按钮 ──
i0 = find("  lv_obj_t* exitTop = lv_btn_create(scr);")
i1 = find("  /* URL 栏用 label 而非 textarea")
if i0 >= 0 and i1 > i0:
    lines[i0:i1] = ["  /* 组 A 的「退出」已删：退出只保留底栏三点一个（回我们的搜索首页），",
                    "     整个浏览器退出走左滑手势。空出来的横向空间全给 URL 栏。 */",
                    "  g_exitTopBtn = nullptr;", ""]
    log.append("exitTop: removed")
else:
    log.append("exitTop: *** missing ***")

# ── 9. URL 栏：上移到 y=410 整行 ──
txt = "\n".join(lines)
if 'lv_obj_align(g_urlArea, LV_ALIGN_TOP_LEFT, 2, 410)' in txt:
    log.append("urlbar: ALREADY")
else:
    txt = txt.replace("lv_obj_set_size(g_urlArea, 300, 36);",
                      "lv_obj_set_size(g_urlArea, 476, 30);", 1)
    txt = txt.replace("lv_obj_align(g_urlArea, LV_ALIGN_TOP_LEFT, 46, 444);",
                      "lv_obj_align(g_urlArea, LV_ALIGN_TOP_LEFT, 2, 410);", 1)
    lines = txt.split("\n")
    log.append("urlbar: ok")

# ── 10. 删掉组 A 的「加载」按钮（刷新已在 nav 行）──
i0 = find("  g_goBtn = lv_btn_create(scr);")
i1 = find("  /* 状态文字：原占 406 那一格")
if i0 >= 0 and i1 > i0:
    lines[i0:i1] = ["  /* 组 A 的「加载」已删：刷新统一放在底栏 nav 行第 4 格。 */",
                    "  g_goBtn = nullptr;", ""]
    log.append("goBtn: removed")
else:
    log.append("goBtn: *** missing ***")

# ── 11. navBar 常驻显示 ──
txt = "\n".join(lines)
if 'lv_obj_add_flag(g_navBar, LV_OBJ_FLAG_HIDDEN);' in txt:
    txt = txt.replace("  lv_obj_add_flag(g_navBar, LV_OBJ_FLAG_HIDDEN);",
                      "  /* 常驻显示：不再有隐藏的组 B */", 1)
    lines = txt.split("\n")
    log.append("navBar visible: ok")
else:
    log.append("navBar visible: ALREADY")

# ── 12. 第 5 键 退出 -> 下载 ──
txt = "\n".join(lines)
OLD_BTN = "  makeNavBtn(&g_exitBtn, Icon::ExitDoor, exit_event_cb, 320);"
if OLD_BTN in txt:
    txt = txt.replace(OLD_BTN,
                      "  /* 第 5 格：原「退出」→「下载」（把当前页存到 LittleFS） */\n"
                      "  makeNavBtn(&g_dlBtn, Icon::Download, download_cb, 320);", 1)
    lines = txt.split("\n")
    log.append("dlBtn: ok")
else:
    log.append("dlBtn: *** missing ***")

# ── 13. 切换按钮图标恒定三点 + 加 toast 控件 ──
txt = "\n".join(lines)
if "g_toast = lv_label_create" in txt:
    log.append("toast: ALREADY")
else:
    A = "  return scr;\n}"
    NEW_TOAST = """  /* 提示条（下载结果等）：不带定时器，靠下次导航/回首页顺手隐藏。
     放在内容区底部，不挡 URL 栏也不挡底栏。 */
  g_toast = lv_label_create(scr);
  lv_obj_set_size(g_toast, 476, 34);
  lv_obj_align(g_toast, LV_ALIGN_TOP_LEFT, 2, 350);
  lv_obj_set_style_bg_color(g_toast, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_opa(g_toast, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_toast, 1, 0);
  lv_obj_set_style_border_color(g_toast, lv_color_hex(0x4488ff), 0);
  lv_obj_set_style_radius(g_toast, 6, 0);
  lv_obj_set_style_pad_left(g_toast, 8, 0);
  lv_obj_set_style_text_color(g_toast, lv_color_white(), 0);
  lv_obj_set_style_text_font(g_toast, &font_zh_16, 0);
  lv_label_set_text(g_toast, "");
  lv_obj_add_flag(g_toast, LV_OBJ_FLAG_HIDDEN);

  return scr;
}"""
    if A not in txt:
        log.append("toast: *** anchor missing ***")
    else:
        txt = txt.replace(A, NEW_TOAST, 1)
        lines = txt.split("\n")
        log.append("toast: ok")

# ── 14. 导航/回首页时隐藏提示条 ──
txt = "\n".join(lines)
if "toastHide();\n  showLoadingOverlay();" in txt:
    log.append("hide-on-nav: ALREADY")
else:
    if "  showLoadingOverlay();" in txt:
        txt = txt.replace("  showLoadingOverlay();", "  toastHide();\n  showLoadingOverlay();", 1)
        lines = txt.split("\n")
        log.append("hide-on-nav: ok")
    else:
        log.append("hide-on-nav: *** missing ***")

out = "\n".join(lines)
if crlf:
    out = out.replace("\n", "\r\n")
io.open(P, "w", encoding="utf-8", newline="").write(out)

v = io.open(P, encoding="utf-8").read()
for k in ["pageCacheFind", "cache_download_html", "downloadCurrentPage",
          "Icon::Download", "g_dlBtn", "g_toast", "PAGE_CACHE_TTL_MS",
          "tactilebrowser_parse_html_buffer"]:
    log.append("verify %-32s %s" % (k, k in v))
for k in ["exitTop", "g_exitBtn", "g_navModeB"]:
    log.append("leftover %-30s %d" % (k, v.count(k)))

print("\n".join(log))
