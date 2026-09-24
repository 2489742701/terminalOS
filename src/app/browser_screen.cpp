#include "browser_screen.h"
#include "icons.h"
#include "nav.h"
#include "status_bar.h"
#include "font_zh.h"
#include "ime_pinyin.h"
#include <lvgl.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "lvgl_renderer.h"
#include "tactilebrowser_core.h"
#include "layout_engine.h"
#include <FS.h>
#include <LittleFS.h>
#include "screensaver.h"
#include "news.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * 异步浏览器架构
 *
 * 状态机：IDLE → LOADING → LOADED / STOPPED / ERROR
 *
 * Phase 1（后台任务 Core 0）：下载 HTML + 解析 DOM + 构建布局树（不触碰 LVGL）
 *   - 协作式停止：g_stopRequested = true → 任务尽快退出
 *   - 进度通过全局变量传递，tick 读取更新 UI
 * Phase 2（UI 任务 Core 1）：渲染布局树到 LVGL 控件（快速）
 *
 * 加载时 UI：半透明遮罩 + "加载中，请稍等..." + 大停止按钮 + 进度条
 * 加载后 UI：网页内容，支持上下滚动、链接点击
 * ═══════════════════════════════════════════════════════════════════════════ */

namespace {

/* ── 浏览器状态机 ── */
enum BrowserState {
  BROWSER_IDLE,     /* 未加载 */
  BROWSER_LOADING,  /* 后台任务运行中 */
  BROWSER_LOADED,   /* 已加载，可交互 */
  BROWSER_STOPPED,  /* 加载被用户取消 */
  BROWSER_ERROR     /* 加载失败 */
};

/* ── UI 控件 ── */
SwipeState g_swipe;
lv_obj_t* g_urlArea = nullptr;   /* label：只显示 URL */
String g_currentUrl;             /* URL 真值来源（label 无法回读） */
/* 首页不再直接抓门户页，改成我们自己画的搜索首页 showSearchHome()。 */

/* 统一更新 URL 显示 + 真值 */
void setUrlText(const char* s) {
  g_currentUrl = s ? s : "";
  if (g_urlArea && lv_obj_is_valid(g_urlArea))
    lv_label_set_text(g_urlArea, g_currentUrl.c_str());
}
lv_obj_t* g_content = nullptr;

/* ── 自建搜索首页的控件 ──
   首页不是网页，是我们自己画的 LVGL 控件树，照样挂在 g_content 下，
   所以 g_content 被 clean 后这些指针会全部失效 —— 必须一起置空。 */
static lv_obj_t* g_searchKb = nullptr;      /* 键盘挂在屏上，不跟 content 一起清 */

/* ── 热点新闻 ──
   数据由后台 fetch 任务填（g_fetchKind=FETCH_NEWS 分支），UI 只读。
   ⚠️ 数组是全局静态：网页内容会被 lv_obj_clean 清掉，新闻不能跟着没。 */
static NewsItem g_news[NEWS_MAX_ITEMS];
static int g_newsCount = 0;
static char g_newsPlatform[16] = "baidu";

/* ── 每个源一份缓存（2026-09-25 加）────────────────────────────────────────
 * master：「各个新闻源应该再记录一下，半小时内更新」。
 * 所以不能只缓存当前看的那个源 —— 切走再切回就白拉一次。
 * 每源 NEWS_MAX_ITEMS(12) × (72+256) ≈ 3.9KB，9 个源 ~35KB。
 * ⚠️ 必须放 PSRAM：内部 DRAM 只剩 ~85KB，35KB 静态数组放进去太贵。
 *    malloc 失败就退回"不缓存"，功能不受影响。 */
struct NewsSrcCache {
  NewsItem items[NEWS_MAX_ITEMS];
  int      count;
  uint32_t ts;        /* 上次更新 millis() */
  bool     used;      /* 这个槽位分配过 */
};
static NewsSrcCache* g_newsCache = nullptr;
static int           g_newsCacheN = 0;
static bool          g_newsCacheTried = false;

#define NEWS_TTL_MS   (30 * 60 * 1000)   /* 30 分钟内不再联网 */

static void newsCacheInit() {
  if (g_newsCache || g_newsCacheTried) return;
  g_newsCacheTried = true;
  g_newsCacheN = kNewsPlatformCount;
  /* 8MB PSRAM 里拿 35KB 毫无压力；拿不到就整功能降级，不硬来 */
  g_newsCache = (NewsSrcCache*)heap_caps_calloc(
      g_newsCacheN, sizeof(NewsSrcCache), MALLOC_CAP_SPIRAM);
  Serial.printf("[Browser] news cache: %s (%u B in PSRAM)\n",
                g_newsCache ? "ok" : "FAILED",
                (unsigned)(g_newsCacheN * sizeof(NewsSrcCache)));
}

/* 平台 code -> 缓存槽位下标，-1 = 不在表里 */
static int newsCacheSlot(const char* code) {
  if (!g_newsCache || !code) return -1;
  for (int i = 0; i < kNewsPlatformCount; i++) {
    if (strcmp(kNewsPlatforms[i].code, code) == 0) return i;
  }
  return -1;
}

/* 有没过期（30 分钟内算新鲜） */
static bool newsFresh(const char* code) {
  int s = newsCacheSlot(code);
  if (s < 0 || !g_newsCache[s].used || g_newsCache[s].count <= 0) return false;
  if (g_newsCache[s].ts == 0) return false;
  return (millis() - g_newsCache[s].ts) < NEWS_TTL_MS;
}

/* 把缓存倒进显示用的 g_news */
static bool newsLoadFromCache(const char* code) {
  int s = newsCacheSlot(code);
  if (s < 0 || !g_newsCache[s].used || g_newsCache[s].count <= 0) return false;
  int n = g_newsCache[s].count;
  if (n > NEWS_MAX_ITEMS) n = NEWS_MAX_ITEMS;
  memcpy(g_news, g_newsCache[s].items, n * sizeof(NewsItem));
  g_newsCount = n;
  snprintf(g_newsPlatform, sizeof(g_newsPlatform), "%s", code);
  Serial.printf("[Browser] news cache hit %s (%d items, age %us)\n",
                code, n, (unsigned)((millis() - g_newsCache[s].ts) / 1000));
  return true;
}

/* 拉完写回缓存 */
static void newsSaveToCache(const char* code) {
  int s = newsCacheSlot(code);
  if (s < 0) return;
  int n = g_newsCount;
  if (n > NEWS_MAX_ITEMS) n = NEWS_MAX_ITEMS;
  if (n <= 0) return;                 /* 失败不留缓存：下次要真重试 */
  memcpy(g_newsCache[s].items, g_news, n * sizeof(NewsItem));
  g_newsCache[s].count = n;
  g_newsCache[s].ts = millis();
  g_newsCache[s].used = true;
}

/* 上次更新距今多久，给 UI 显示用（返回秒） */
static uint32_t newsAgeSec(const char* code) {
  int s = newsCacheSlot(code);
  if (s < 0 || !g_newsCache[s].used || g_newsCache[s].ts == 0) return 0;
  return (millis() - g_newsCache[s].ts) / 1000;
}

/* 每天自动刷一次：记录"当天已自动刷过"的日期（YYYYMMDD） */
static uint32_t g_newsAutoDay = 0;
static volatile int g_fetchKind = 0;   /* 0=网页 1=新闻 */
#define FETCH_WEB  0
#define FETCH_NEWS 1
static lv_obj_t* g_searchTa = nullptr;

/* ── 中文输入法（IME）状态 ──
   lv_keyboard 自带的是纯 ASCII 键盘，没有 IME，中文一个字也打不出来。
   这里不改键盘，只监听输入框文本变化：从**尾部**抠出连续的 ASCII 小写字母
   当拼音，查 ime_pinyin.c 的表，把候选画成一条 chip 行贴在键盘上方。
   点候选 = 把尾部这串拼音替换成那个汉字 —— 中英混输、退格、接着打字都天然可用。 */
static lv_obj_t* g_imeBar = nullptr;   /* 候选条容器：挂在屏上，不跟 content 一起清 */
static char g_imePy[8] = {0};          /* 当前正在拼的拼音（不含声调） */
static int g_imePyLen = 0;             /* 拼音长度：上屏时要从文本尾部砍掉这么多字节 */
static char g_imePendHz[8] = {0};      /* 待上屏的汉字（延迟一拍用） */
static bool g_imePendSet = false;
static void ime_hide();   /* 定义在下面，焦点/搜索回调里要先用到 */
/* 搜索引擎固定为必应（2026-09-23 由 master 拍板去掉百度）：
     · 必应 cn.bing.com：桌面 UA 下 ~100KB，10 条结果是**静态 HTML**
       （li.b_algo，<h2> 标题 + <a href> 真链），平铺渲染能直接吃下。
     · 百度 m.baidu.com：2.93MB 起步（光 <head> 就 380KB），786KB 的下载上限
       连正文都摸不全，结果还靠 JS 渲染 + 反爬，对我们这种无 JS 客户端不可用了。 */

/* 清空内容区的唯一出口：网页 widget 和搜索首页 widget 都会失效，
   指针必须一起置空，否则下次 lv_obj_is_valid() 会踩到已释放内存。 */
static void contentReset() {
  if (g_content && lv_obj_is_valid(g_content)) lv_obj_clean(g_content);
  g_searchTa = nullptr;
}

static void showSearchHome();   /* 定义在下方（导航回调里要用） */
static void showDownloadsHome();  /* 下载列表（定义在下方） */
static void dl_open_list_cb(lv_event_t* e);   /* 搜索首页 -> 下载列表 */

/* 排版视口宽度：网页按这个宽度排版，渲染时再等比压缩到屏幕宽度。
   0 = 自动（推荐）：读页面的 <meta name="viewport">，
       声明了 width=device-width → 用屏幕宽 464 排版（移动端页面原比例）；
       桌面站没有该 meta → 回退 1024（桌面设计宽）。
   非 0 = 手动覆盖，可用串口 `vp <宽度>` / `vp 0`（恢复自动）实时调，下次加载生效。 */
int g_browserViewportW = 0;

/* 内容区实际可用宽度（g_content 476 减去左右 padding 6*2） */
static const int CONTENT_W = 464;
/* 顶栏由 40px 的「退出+URL+加载」改成 28px 系统状态栏后，
   纵向多出来的空间全部给网页内容（366 → 424）。 */
static const int CONTENT_H = 414;
lv_obj_t* g_status = nullptr;
lv_obj_t* g_goBtn = nullptr;
lv_obj_t* g_progressBar = nullptr;
lv_obj_t* g_progressLabel = nullptr;
lv_obj_t* g_backBtn = nullptr;
lv_obj_t* g_fwdBtn = nullptr;
lv_obj_t* g_homeBtn = nullptr;
lv_obj_t* g_dlBtn = nullptr;      /* 下载（原「退出」那一格） */

/* ── 加载遮罩 UI（覆盖在内容区上） ── */
lv_obj_t* g_loadingOverlay = nullptr;   /* 半透明遮罩 */
lv_obj_t* g_loadingLabel = nullptr;     /* "加载中，请稍等..." */
lv_obj_t* g_stopBtn = nullptr;          /* 大停止按钮 */
lv_obj_t* g_loadProgressBar = nullptr;  /* 遮罩上的进度条 */
lv_obj_t* g_loadProgressText = nullptr; /* "下载中 45%" */

/* ── 异步状态（后台任务和 UI 任务共享，用 volatile 保护） ── */
volatile BrowserState g_state = BROWSER_IDLE;
volatile bool g_stopRequested = false;
volatile bool g_taskDone = false;
volatile int g_progressPct = 0;
volatile char g_progressStage[32] = {0};
volatile RenderResult g_taskResult = RENDER_SUCCESS;
LayoutNode* g_layoutRoot = nullptr;     /* Phase 1 产出的布局树 */
TaskHandle_t g_fetchTask = nullptr;     /* 常驻后台任务句柄 */
String g_pendingUrl;                    /* 加载中被抢占时排队的下一次请求 */
bool g_hasPending = false;
/* 网页里的链接被点击时暂存的跳转目标（见 link_click_cb：不能在事件回调里清内容） */
String g_linkPending;
bool g_linkPendingSet = false;
String g_fetchUrl;
bool g_firstLoad = true;

/* ── 引擎 ── */
LvglRenderer* g_renderer = nullptr;
bool g_engineInited = false;

/* ── 导航历史栈 ── */
#define MAX_HISTORY 20
String g_history[MAX_HISTORY];
int g_historySize = 0;
int g_historyIdx = -1;

void historyPush(const String& url) {
  if (g_historyIdx < g_historySize - 1)
    g_historySize = g_historyIdx + 1;
  if (g_historySize < MAX_HISTORY) {
    g_history[g_historySize] = url;
    g_historySize++;
  } else {
    for (int i = 1; i < MAX_HISTORY; i++)
      g_history[i-1] = g_history[i];
    g_history[MAX_HISTORY-1] = url;
    g_historySize = MAX_HISTORY;
  }
  g_historyIdx = g_historySize - 1;
}

/* 灰态：按钮压暗 + 里面的图标一起压暗（LVGL 8 的 opa 不会级联到子对象，
   只设按钮的话图标仍是纯白，看不出"这颗不能按"）。 */
static void set_nav_enabled(lv_obj_t* btn, bool on) {
  if (!btn || !lv_obj_is_valid(btn)) return;
  lv_obj_set_style_bg_opa(btn, on ? LV_OPA_COVER : LV_OPA_40, 0);
  lv_obj_t* ic = lv_obj_get_child(btn, 0);
  if (ic && lv_obj_is_valid(ic)) lv_obj_set_style_opa(ic, on ? LV_OPA_COVER : LV_OPA_40, 0);
}

void updateNavButtons() {
  set_nav_enabled(g_backBtn, g_historyIdx > 0);
  set_nav_enabled(g_fwdBtn,  g_historyIdx < g_historySize - 1);
  set_nav_enabled(g_homeBtn, g_historySize > 0);
}


/* ══ 页面缓存（PSRAM，带 TTL）══
 * 渲染完成后 HTML 原文由引擎内部 free（本来就是），这里额外留一份 URI→HTML 的
 * 拷贝放在 PSRAM：前进/后退/重访命中就跳过 TLS+下载，直接建布局树。
 * ⚠️ 只缓存 PAGE_CACHE_MAX_ENTRY 字节以内的大页；超了就不缓存（PSRAM 也要省着用）。
 *
 * 2026-09-23 master 要求：只保留「当前页 + 上一页」两页，其余一律及时释放。
 * 所以这里是 2 槽 —— 这是**硬上限**，不是性能调优参数。想要更多请走
 * 下载（LittleFS）或设置里的缓存管理。 */
static const int PAGE_CACHE_SLOTS = 2;                      /* 当前页 + 上一页 */
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
/* toast 自动消失的定时器。⚠️ 定时器持有 g_toast 指针 → 退出浏览器时必须
   注销（BrowserScreen_close 里做），否则屏销毁后回调会碰悬空指针。
   （master 2026-09-25：「已更新」会卡住不消失 —— 之前 toast() 只负责显示，
     从来没人负责收起来，只有下一次 startNews 才顺手 toastHide。） */
static lv_timer_t* s_toastTimer = nullptr;

/* 前向声明：toastHide 定义在下面，而这个回调在它之前（同一个 TU 内
   必须先声明后使用）。 */
static void toastHide();

static void toast_hide_cb(lv_timer_t* t) {
  (void)t;
  s_toastTimer = nullptr;
  toastHide();
}

static void toast(const char* msg) {
  Serial.printf("[Browser] toast: %s\n", msg);
  if (!g_toast || !lv_obj_is_valid(g_toast)) return;
  lv_label_set_text(g_toast, msg);
  lv_obj_clear_flag(g_toast, LV_OBJ_FLAG_HIDDEN);
  /* 连着弹时重置计时，别让上一条的定时器把新的一条提前收掉 */
  if (s_toastTimer) {
    lv_timer_del(s_toastTimer);
    s_toastTimer = nullptr;
  }
  s_toastTimer = lv_timer_create(toast_hide_cb, 1800, nullptr);
  if (s_toastTimer) lv_timer_set_repeat_count(s_toastTimer, 1);
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
  /* 第一行写原 URL：文件名只有 hash，"下载的网站"列表页靠它显示可读名字，
     也才能在离线状态下把这个 URL 塞回页面缓存重渲染。 */
  size_t w = f.print("<!--URL:");
  w += f.print(g_currentUrl);
  w += f.print("-->\n");
  w += f.write(g_pageCache[ci].data, g_pageCache[ci].len);
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

/* ── 进度回调（在后台任务中调用，不能触碰 LVGL） ──
 * 只把进度存到全局变量，tick 中读取并更新 UI。 */
void progressCb(int downloaded, int total, const char* stage) {
  g_progressPct = total > 0 ? downloaded * 100 / total : 0;
  if (stage) {
    strncpy((char*)g_progressStage, stage, sizeof(g_progressStage) - 1);
    g_progressStage[sizeof(g_progressStage) - 1] = '\0';
  }
}

/* ── 事件回调 ── */
void exit_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    /* 退出时真释放：网页内容是最大的内存开销（数十个 widget + 布局树），
       之前只切屏不释放，来回几次就把 DRAM 吃干净了。
       注意：这里不能用 nav_back_home() —— 它会在本按钮的事件回调里
       lv_obj_del() 掉当前所在的屏幕对象，LVGL 事件栈还没退完就可能踩到
       已释放内存。只清内容 + 切屏；整棵屏树的销毁交给下次进其他应用时
       nav_release_all_except() 处理（那时不在任何浏览器对象回调里）。 */
    BrowserScreen_close();
    nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
  }
}

void swipe_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
    Serial.println("[Browser] swipe_cb PRESSED");
  }
  swipe_back_to_any(e, g_swipe, nav_launcher, false);
}

void url_focus_cb(lv_event_t* e) {
  /* URL 框点击暂时不弹键盘（DRAM 不足），通过串口输入 */
  (void)e;
}

/* 停止按钮回调：设置停止标志，后台任务会尽快退出 */
void stop_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_stopRequested = true;
  if (g_loadingLabel) lv_label_set_text(g_loadingLabel, "正在停止...");
}

/* ── 网页里的链接被点击 ─────────────────────────────────────────────────────
 * ⚠️ 这里**只能记 URL，不能立刻 startFetch()**：startFetch 会 lv_obj_clean(g_content)，
 *   而此刻正处在那颗胶囊自己的 LV_EVENT_CLICKED 回调里 —— 等于在 LVGL 事件派发
 *   循环中把对象（及其 event dsc 链表）free 掉，百分百 LoadProhibited。
 *   真正的导航推迟到下一个 BrowserScreen_tick() 再执行。 */
/* 相对 URL 兜底：dom_renderer 有时只留下纯路径（/s?word=x），补上当前页的
   scheme+host。返回空串 = 解析不出来，调用方应当忽略这次点击。 */
static String resolveMaybeRelative(const char* url) {
  String u(url ? url : "");
  if (u.startsWith("http://") || u.startsWith("https://")) return u;
  if (u.startsWith("//")) return String("http:") + u;
  if (u.startsWith("/") && g_currentUrl.length() > 0) {
    int schemeEnd = g_currentUrl.indexOf("://");
    if (schemeEnd > 0) {
      int hostEnd = g_currentUrl.indexOf('/', schemeEnd + 3);
      String base = (hostEnd < 0) ? g_currentUrl
                                  : g_currentUrl.substring(0, hostEnd);
      return base + u;
    }
  }
  return String();
}

/* ══ 伪链接识别 ═══════════════════════════════════════════════════════════
 * 网页上大量 href 不是真地址：
 *   javascript: void(0);   —— 纯 JS 空按钮（乐鑫官网菜单里一大堆）
 *   mailto: tel: intent:   —— 唤起别的 App
 *   #anchor                —— 页内锚点
 * 这些一旦被当成 URL 下载，服务器往往回一个几百 KB 的 404/首页，
 * 解析出几千个节点，DRAM 一次掉 130KB —— 连点几下就重启。
 *
 * ⚠️ 难点：这些 href 经常已经被拼上了域名前缀，变成
 *    https://host/path/javascript: void(0);
 * 所以除了看前缀，还得看**子串**里有没有 javascript:。
 * ═════════════════════════════════════════════════════════════════════════ */
static bool isPseudoUrl(const char* u) {
  if (!u || !u[0]) return true;
  if (u[0] == '#') return true;                 /* 页内锚点 */

  static const char* kSchemes[] = {
      "javascript:", "mailto:", "tel:", "data:", "about:", "blob:",
      "sms:", "intent:", "weixin:", "alipays:", "viber:", nullptr};
  for (int i = 0; kSchemes[i]; i++) {
    size_t n = strlen(kSchemes[i]);
    if (strncasecmp(u, kSchemes[i], n) == 0) return true;      /* 前缀 */
  }
  /* 被域名前缀包装过的形态：子串里有 javascript: 也一律不要 */
  if (strcasestr(u, "javascript:") != nullptr) return true;
  if (strcasestr(u, "void(0)") != nullptr) return true;
  return false;
}

static void link_click_cb(const char* url) {
  if (isPseudoUrl(url)) {
    Serial.printf("[Browser] link ignored (pseudo): %.48s\n", url ? url : "(null)");
    /* 以前是静默丢弃 —— 用户看到"点了没反应"会以为又崩了。
       明确告诉他是 JS 菜单，这类按钮在无 JS 的浏览器里本来就点不动。 */
    toast("此按钮需要 JavaScript");
    return;
  }
  String target = resolveMaybeRelative(url);
  if (!target.length()) {
    Serial.printf("[Browser] link ignored (unresolvable): %s\n", url);
    return;
  }
  if (isPseudoUrl(target.c_str())) {
    /* 相对解析也可能把 javascript: 拼进来（宿主 + 伪路径），再拦一次 */
    Serial.printf("[Browser] link ignored (pseudo after resolve): %.48s\n",
                  target.c_str());
    return;
  }
  g_linkPending = target;
  g_linkPendingSet = true;
}

/* ── 引擎管理 ── */
void ensureEngineInit() {
  if (g_engineInited) return;
  tactilebrowser_core_init();
  g_renderer = lvgl_renderer_create();
  tactilebrowser_set_renderer(&g_renderer->base);
  tactilebrowser_set_html_downloader(cache_download_html);
  arduino_set_progress_callback(progressCb);
  /* 网页里链接/胶囊被点击 → link_click_cb（只存 URL，tick 里再真正导航） */
  lvgl_renderer_set_link_callback(link_click_cb);
  /* 告诉引擎屏幕内容区有多宽：<meta viewport width=device-width> 要用它排版 */
  layout_set_screen_width(CONTENT_W);
  g_engineInited = true;
}

/* 后台任务栈。必须在启动早期（DRAM 未碎片化时）一次性分配，常驻不销毁。
   之前在每次加载时用 xTaskCreatePinnedToCore 现申请这块连续内存，跑到第二次
   内部 DRAM 只剩 ~93KB，凑不出 48KB 连续块 → "Failed to create fetch task"。

   大小依据（2026-09 实测，串口会打 "[Browser] fetch stack: x/y B used (peak)"）：
   下载 + lexbor 解析 + mbedTLS 握手跑完，栈峰值只用了 3712 B。
   48KB 是当初拍脑袋给的数，白占 32KB DRAM。改成 16KB = 峰值的 4.4 倍余量。
   换更复杂的页面如果打不出来，把这里的数调大即可（日志会提示真实峰值）。 */
#define FETCH_STACK_BYTES 16384

void fetch_task(void* param);  // 前向声明（定义在下方）

void ensureFetchTask() {
  if (g_fetchTask) return;
  BaseType_t ret = xTaskCreatePinnedToCore(
      fetch_task, "browser_fetch", FETCH_STACK_BYTES, NULL, 5, &g_fetchTask, 0);
  if (ret != pdPASS) {
    g_fetchTask = nullptr;
    Serial.printf("[Browser] FATAL: fetch task create failed, DRAM free=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  } else {
    Serial.printf("[Browser] fetch task created, stack=%u, DRAM free=%u\n",
                  (unsigned)FETCH_STACK_BYTES,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  }
}

/* ── 后台下载解析任务（Phase 1，不触碰 LVGL）──
 * 常驻循环：靠任务通知唤醒，干完活回到等待，不再 vTaskDelete。
 * 好处：① 不产生堆碎片 ② 不存在失效句柄 ③ 不会分配失败。 */
void fetch_task(void *param) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // 等待 UI 通知
    if (g_stopRequested) {                    // 唤醒时已被取消
      g_taskDone = true;
      continue;
    }

    /* 热点新闻是纯 JSON，不走 HTML 引擎：自己下载 + 解析，填 g_news。
       复用这个常驻任务，省掉再开一个任务栈（任务栈只能从 DRAM 分配）。 */
    if (g_fetchKind == FETCH_NEWS) {
      g_newsCount = news_fetch(g_newsPlatform, g_news, NEWS_MAX_ITEMS);
      /* 拉完立刻写回该源的缓存槽，下次 30 分钟内就不用联网了 */
      if (g_newsCount > 0) newsSaveToCache(g_newsPlatform);
      Serial.printf("[Browser] news %s -> %d items, DRAM free=%u\n",
                    g_newsPlatform, g_newsCount,
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
      g_taskDone = true;
      continue;
    }

    String url = g_fetchUrl;
    if (!url.startsWith("http://") && !url.startsWith("https://"))
      url = "http://" + url;

    Serial.printf("[Browser] fetch_task start: %s\n", url.c_str());

    /* 用宽视口排版（max_height 在布局阶段未使用，传 0 即可） */
    int ci = pageCacheFind(url);
    if (ci >= 0) {
      Serial.printf("[Browser] cache hit: %s (%u B, age %us)\n",
                    url.c_str(), (unsigned)g_pageCache[ci].len,
                    (unsigned)((millis() - g_pageCache[ci].ts) / 1000));
      g_taskResult = tactilebrowser_parse_html_buffer(
          url.c_str(), (const char*)g_pageCache[ci].data, g_pageCache[ci].len,
          g_browserViewportW, 0, &g_stopRequested, &g_layoutRoot);
    } else {
      g_taskResult = tactilebrowser_download_and_parse(
          url.c_str(), g_browserViewportW, 0, &g_stopRequested, &g_layoutRoot);
    }

    Serial.printf("[Browser] fetch_task done: result=%d layout=%p DRAM free=%u\n",
      (int)g_taskResult, g_layoutRoot,
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    /* 栈实测：high water mark = 历史最小剩余（字），x4 = 字节。
       用于判断 49152 的栈是不是开太大了。 */
    {
      UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
      Serial.printf("[Browser] fetch stack: %u/%u B used (peak), low-water=%u B\n",
        (unsigned)(FETCH_STACK_BYTES - hwm * sizeof(StackType_t)),
        (unsigned)FETCH_STACK_BYTES, (unsigned)(hwm * sizeof(StackType_t)));
    }

    g_taskDone = true;
  }
}

/* ── 显示/隐藏加载遮罩 ── */
void showLoadingOverlay() {
  if (g_loadingOverlay) lv_obj_clear_flag(g_loadingOverlay, LV_OBJ_FLAG_HIDDEN);
  if (g_loadingLabel) lv_label_set_text(g_loadingLabel, "加载中，请稍等...");
  if (g_loadProgressText) lv_label_set_text(g_loadProgressText, "");
  if (g_loadProgressBar) lv_bar_set_value(g_loadProgressBar, 0, LV_ANIM_OFF);
  g_progressPct = 0;
  g_progressStage[0] = '\0';
}

void hideLoadingOverlay() {
  if (g_loadingOverlay) lv_obj_add_flag(g_loadingOverlay, LV_OBJ_FLAG_HIDDEN);
}

/* ── 启动加载（创建后台任务） ── */
void startFetch(const String& url) {
  /* 正在加载：请求停止当前任务，并把本次请求排队，等 tick 收尾后自动续上。
     原先这里用 vTaskDelay 死等旧任务退出，但 g_fetchTask 只在 tick 里清空，
     而 tick 和 startFetch 同在 UI 任务 → 阻塞期间 tick 永远跑不到，必然白等。 */
  if (g_state == BROWSER_LOADING) {
    g_stopRequested = true;
    g_pendingUrl = url;
    g_hasPending = true;
    Serial.println("[Browser] busy: stopping current load, url queued");
    return;
  }

  /* 兜底：任何漏网的伪 URL 都不许走到网络去 —— 代价是几百 KB 下载 +
     几千节点解析 + DRAM 一次掉上百 KB（2026-09-23 实测）。 */
  if (isPseudoUrl(url.c_str())) {
    Serial.printf("[Browser] refuse fetch (not a real URL): %.48s\n", url.c_str());
    return;
  }

  ensureEngineInit();
  ensureFetchTask();
  if (!g_fetchTask) return;  // 任务缺失，ensureFetchTask 已打印原因

  Serial.printf("[Browser] load start: nav_browser=%p valid=%d DRAM=%u\n",
    nav_browser, nav_browser ? (int)lv_obj_is_valid(nav_browser) : 0,
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  /* 引擎重建在 UI 任务中做，避免后台任务操作全局状态与 UI 冲突 */
  tactilebrowser_core_cleanup();
  tactilebrowser_core_init();
  tactilebrowser_set_renderer(&g_renderer->base);
  tactilebrowser_set_html_downloader(cache_download_html);
  arduino_set_progress_callback(progressCb);

  g_fetchUrl = url;
  g_stopRequested = false;
  g_taskDone = false;
  g_layoutRoot = nullptr;
  g_state = BROWSER_LOADING;

  /* 显示加载 UI */
  if (g_status) lv_label_set_text(g_status, "加载中");
  toastHide();
  showLoadingOverlay();
  contentReset();
  updateNavButtons();

  /* 加载期间抑制息屏：下载+解析要几十秒，中途被锁屏抢走屏幕的话
     浏览器 tick 停摆，渲染永远执行不到（表现为"一直加载不出来"）。 */
  ScreenSaver::setSuppressed(true);

  /* 唤醒常驻后台任务（不重新创建，不申请新栈） */
  xTaskNotifyGive(g_fetchTask);
}

/* ── 导航回调 ── */
void go_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  /* URL 栏是 label，不可编辑；加载的是当前显示的地址（串口 browser <url> 可改） */
  if (g_currentUrl.length() == 0) return;
  historyPush(g_currentUrl);
  startFetch(g_currentUrl);
}

void back_nav_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_historyIdx <= 0) return;
  g_historyIdx--;
  setUrlText(g_history[g_historyIdx].c_str());
  startFetch(g_history[g_historyIdx]);
}

void fwd_nav_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_historyIdx >= g_historySize - 1) return;
  g_historyIdx++;
  setUrlText(g_history[g_historyIdx].c_str());
  startFetch(g_history[g_historyIdx]);
}

void home_nav_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  showSearchHome();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 自建搜索首页（Route D 的第一刀）
 *
 *   为什么不再直接抓 m.baidu.com：门户页靠 float/absolute/flex 排版，我们的布局
 *   引擎只支持简单 block flow → 大半塌成 0 高，最后只剩一条顶栏。自己画的首页
 *   没有这个问题：搜索框 + 引擎切换 + 常用词 + 新闻源占位，提交后直接打搜索引擎
 *   的 search 端点（m.baidu.com/s?word= / cn.bing.com/search?q=），
 *   结果页照旧走平铺渲染。
 *
 *   ⚠️ 导航一律走 g_linkPending 延迟通道，别在按钮自己的 CLICKED 回调里
 *      startFetch() —— 那会 lv_obj_clean() 掉正在回调的按钮本身。
 * ═══════════════════════════════════════════════════════════════════════════ */

/* URL 百分号编码：中文/空格必须转，否则塞进 URL 会被服务端判成非法字符。
   逐字节处理 = 天然按 UTF-8 编码，不需要先转码。 */
static String urlEncode(const String& s) {
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < (size_t)s.length(); i++) {
    unsigned char c = (unsigned char)s[i];
    bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                c == '~';
    if (keep) {
      out += (char)c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

/* ⚠️ 这两个**必须定义在 startNews 之前**：缓存命中分支也要走 UI_PEND 通道
   重建首页。原来定义在文件后面（下载管理那节），startNews 看不见。 */
enum PendingUiKind { UI_PEND_NONE = 0, UI_PEND_SEARCH, UI_PEND_DOWNLOADS };
static volatile int g_uiPendingKind = UI_PEND_NONE;

/* 搜索引擎（master 2026-09-25：必应之外再加 360）
   实测：www.so.com/s?q=  -> 200 / 449KB，能正常打开。
   ⚠️ 必应必须用桌面 Chrome120 UA（移动 UA 只给 5 条、无分页），
      这条规矩在 browser_engine 那边，别动。 */
struct SearchEngine { const char* name; const char* tpl; };
static const SearchEngine kEngines[] = {
    {"必应", "https://cn.bing.com/search?q="},
    {"360",  "https://www.so.com/s?q="},
    {"百度", "https://www.baidu.com/s?word="},
};
static const int kEngineCount = (int)(sizeof(kEngines) / sizeof(kEngines[0]));
static int g_engineIdx = 0;

static void startSearch(const String& q) {
  String enc = urlEncode(q);
  if (!enc.length()) return;
  String url = String(kEngines[g_engineIdx].tpl) + enc;
  Serial.printf("[Search] \"%s\" via %s -> %s\n", q.c_str(),
                kEngines[g_engineIdx].name, url.c_str());
  if (g_searchKb && lv_obj_is_valid(g_searchKb))
    lv_obj_add_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);
  ime_hide();
  g_linkPending = url;
  g_linkPendingSet = true;
}

static void search_go_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_READY) return;
  if (!g_searchTa || !lv_obj_is_valid(g_searchTa)) return;
  const char* q = lv_textarea_get_text(g_searchTa);
  if (!q || !q[0]) return;
  startSearch(String(q));
}

/* 切换搜索引擎：切完当场重画搜索首页（行的重建走 UI_PEND 通道，别在回调里 clean） */
static void engine_switch_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_engineIdx = (g_engineIdx + 1) % kEngineCount;
  Serial.printf("[Search] engine -> %s\n", kEngines[g_engineIdx].name);
  toast((String("搜索引擎：") + kEngines[g_engineIdx].name).c_str());
  g_uiPendingKind = UI_PEND_SEARCH;
}

/* ── 热点新闻：起后台任务拉一个平台 ── */
static void startNews(const char* platform, bool force = false) {
  if (g_state == BROWSER_LOADING) { toast("正在加载，请稍等"); return; }
  ensureFetchTask();
  if (!g_fetchTask) return;

  snprintf(g_newsPlatform, sizeof(g_newsPlatform), "%s", platform);

  /* 30 分钟内的重复点击：直接用缓存画出来，不再打一次网络（master 2026-09-25）。
     想强制刷新就点底部的「更新」按钮（news_refresh_cb）。 */
  if (!force && newsFresh(platform)) {
    if (newsLoadFromCache(platform)) {
      g_state = BROWSER_LOADED;
      g_currentUrl = "";
      g_fetchKind = FETCH_WEB;
      if (g_urlArea && lv_obj_is_valid(g_urlArea))
        lv_label_set_text(g_urlArea, "热点新闻");
      toast("已是最新");
      g_uiPendingKind = UI_PEND_SEARCH;
      return;
    }
  }

  /* ⚠️ 必须清停止标志 —— 这是"换了源却永远显示上一个源"的根因：
     g_stopRequested 只有 startFetch / tick 会清，而退出浏览器
     (BrowserScreen_close) 会把它置 true 且不再走那两处。于是下一次
     startNews 唤醒后台任务时，任务第一句 `if (g_stopRequested)` 直接
     g_taskDone=true 就返回 —— **什么都没拉**，tick 却照样弹"已更新"，
     显示的还是 g_news 里的旧数据。 */
  g_stopRequested = false;
  g_hasPending = false;
  g_pendingUrl = "";
  g_fetchKind = FETCH_NEWS;
  g_taskDone = false;
  g_state = BROWSER_LOADING;
  toastHide();
  showLoadingOverlay();
  updateNavButtons();
  ScreenSaver::setSuppressed(true);
  xTaskNotifyGive(g_fetchTask);
}

/* 底部「更新」：强制刷新当前源（跳过 30 分钟节流）。
   ⚠️ 回调里只调 startNews，真正的重建走 tick 通道（老规矩）。 */
static void news_refresh_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  Serial.printf("[Browser] news refresh (force): %s\n", g_newsPlatform);
  startNews(g_newsPlatform, true);
}

static void news_platform_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char* code = (const char*)lv_event_get_user_data(e);
  if (!code) return;
  Serial.printf("[Browser] news platform: %s\n", code);
  startNews(code);
}

/* 点新闻标题 -> 打开原文。⚠️ 只置待办，真正的导航在下一 tick（老规矩） */
static void news_item_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (idx < 0 || idx >= g_newsCount) return;
  /* 点中的那条**当场**高亮：跳转要等下一 tick 才发生，中间还要走网络，
     不给反馈的话用户不知道自己点中了哪条（master 2026-09-25）。 */
  lv_obj_t* btn = lv_event_get_target(e);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x2b4a6f), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_t* lb = lv_obj_get_child(btn, 0);
  if (lb) lv_obj_set_style_text_color(lb, lv_color_white(), 0);
  g_linkPending = String(g_news[idx].url);
  g_linkPendingSet = true;
}

/* 聚焦搜索框才弹键盘。键盘常驻隐藏、只切 HIDDEN —— 反复 create/delete 会打碎堆。 */
static void search_focus_cb(lv_event_t* e) {
  if (!g_searchKb) return;
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(g_searchKb, g_searchTa);
    lv_obj_clear_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED) {
    lv_obj_add_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);
    ime_hide();
  }
}

static void kb_hide_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    if (g_searchKb) lv_obj_add_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);
  }
}

static void preset_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char* q = (const char*)lv_event_get_user_data(e);
  if (!q) return;
  if (g_searchTa && lv_obj_is_valid(g_searchTa)) lv_textarea_set_text(g_searchTa, q);
  startSearch(String(q));
}

/* 一个横向排布的透明容器。绝不用 lv_obj_set_pos —— 让 flex 排。 */
static lv_obj_t* makeRow(lv_obj_t* parent, bool wrap) {
  lv_obj_t* r = lv_obj_create(parent);
  lv_obj_set_width(r, lv_pct(100));
  lv_obj_set_height(r, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, wrap ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(r, 0, 0);
  lv_obj_set_style_pad_gap(r, 6, 0);
  lv_obj_set_style_pad_row(r, 6, 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
  return r;
}

/* 带框小按钮（跟网页里的"胶囊"一个视觉语言：能点的都画框） */
static lv_obj_t* makeChipBtn(lv_obj_t* parent, const char* text,
                             lv_event_cb_t cb, void* ud) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 36);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x333333), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(0x444444), 0);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_obj_set_style_text_font(l, &font_zh_16, 0);
  lv_obj_center(l);
  return b;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * 中文输入法（拼音 -> 候选汉字）
 *
 * ⚠️ 上屏必须延迟一拍：候选 chip 的 CLICKED 回调里如果直接改输入框，
 *    会连锁触发 VALUE_CHANGED -> 重建候选条 -> lv_obj_clean 掉**正在派发事件的
 *    chip 自己**。这是本项目崩过好几次的模式，统一用一次性 lv_timer 绕开。
 * ═══════════════════════════════════════════════════════════════════════════ */
static void ime_pick_cb(lv_event_t* e);   /* 下面定义，ime_update 里要先引用 */
static void ime_apply_cb(lv_timer_t* t);

static void ime_hide() {
  if (g_imeBar && lv_obj_is_valid(g_imeBar))
    lv_obj_add_flag(g_imeBar, LV_OBJ_FLAG_HIDDEN);
  g_imePyLen = 0;
  g_imePy[0] = 0;
}

/* 重建候选条：从输入框尾部抠拼音 -> 查表 -> 画 chip。 */
static void ime_update() {
  if (!g_imeBar || !lv_obj_is_valid(g_imeBar)) return;
  if (!g_searchTa || !lv_obj_is_valid(g_searchTa)) return;
  /* 键盘没弹出来就不显示候选：那条 bar 是贴在键盘上沿的，
     没有键盘时它会孤零零浮在内容区中间。 */
  if (g_searchKb && lv_obj_is_valid(g_searchKb) &&
      lv_obj_has_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN)) {
    ime_hide();
    return;
  }

  const char* t = lv_textarea_get_text(g_searchTa);
  if (!t || !t[0]) { ime_hide(); return; }

  /* 只认尾部连续小写字母，最多 6 个（chuang/shuang 也就 6 个） */
  int len = (int)strlen(t);
  int p = len;
  while (p > 0 && len - p < 6) {
    unsigned char c = (unsigned char)t[p - 1];
    if (c >= 'a' && c <= 'z') p--; else break;
  }
  int pyLen = len - p;
  if (pyLen <= 0) { ime_hide(); return; }
  if (pyLen >= (int)sizeof(g_imePy)) { ime_hide(); return; }
  memcpy(g_imePy, t + p, (size_t)pyLen);
  g_imePy[pyLen] = 0;

  const char* han = ime_lookup(g_imePy);
  if (!han) { ime_hide(); return; }

  /* 3 字节一个汉字（表里全是 CJK 基本区） */
  int cnt = 0;
  while (han[cnt * 3]) cnt++;
  if (cnt <= 0) { ime_hide(); return; }

  lv_obj_clean(g_imeBar);

  lv_obj_t* pyLab = lv_label_create(g_imeBar);
  lv_label_set_text(pyLab, g_imePy);
  lv_obj_set_style_text_color(pyLab, lv_color_hex(0x66ccff), 0);
  lv_obj_set_style_text_font(pyLab, &lv_font_montserrat_16, 0);
  lv_obj_set_style_pad_right(pyLab, 6, 0);

  for (int i = 0; i < cnt; i++) {
    char hz[4];
    if (!ime_pick(han, i, hz)) break;
    lv_obj_t* b = makeChipBtn(g_imeBar, hz, ime_pick_cb, (void*)(intptr_t)i);
    lv_obj_set_size(b, 44, 36);
  }

  lv_obj_clear_flag(g_imeBar, LV_OBJ_FLAG_HIDDEN);
  lv_obj_scroll_to_x(g_imeBar, 0, LV_ANIM_OFF);
  g_imePyLen = pyLen;
}

/* 选中第 idx 个候选：只记下来，真正的改文本在下一拍做。 */
static void ime_pick_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!g_searchTa || !lv_obj_is_valid(g_searchTa)) return;
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  const char* han = ime_lookup(g_imePy);
  if (!han) return;
  if (!ime_pick(han, idx, g_imePendHz)) return;
  g_imePendSet = true;
  lv_timer_t* t = lv_timer_create(ime_apply_cb, 1, NULL);
  if (t) lv_timer_set_repeat_count(t, 1);
}

/* 上一拍选中的汉字落地：砍掉尾部拼音 + 补上汉字。 */
static void ime_apply_cb(lv_timer_t* t) {
  lv_timer_del(t);
  if (!g_imePendSet) return;
  g_imePendSet = false;
  if (!g_searchTa || !lv_obj_is_valid(g_searchTa)) return;

  const char* cur = lv_textarea_get_text(g_searchTa);
  if (!cur) return;
  String s(cur);
  int cut = g_imePyLen;
  if (cut > (int)s.length()) cut = (int)s.length();
  if (cut > 0) s.remove((unsigned int)(s.length() - cut), (unsigned int)cut);
  s += g_imePendHz;
  lv_textarea_set_text(g_searchTa, s.c_str());
  ime_update();   /* 尾部通常已经没有拼音了 -> 顺带把候选条收起来 */
}

static void ime_changed_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  ime_update();
}

static void showSearchHome() {
  /* 诊断：这条日志是"浏览器有没有显示初始页"的判据。
     退出浏览器再进、如果这里不打，就是 g_firstLoad 没被重置。 */
  Serial.println("[Browser] showSearchHome");
  newsCacheInit();   /* 多源缓存槽（PSRAM），只初始化一次 */
  if (!g_content || !lv_obj_is_valid(g_content)) return;
  hideLoadingOverlay();
  if (g_searchKb) lv_obj_add_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);
  ime_hide();
  contentReset();
  g_state = BROWSER_LOADED;
  g_currentUrl = "";
  if (g_urlArea && lv_obj_is_valid(g_urlArea))
    lv_label_set_text(g_urlArea, "搜索首页");
  updateNavButtons();

  lv_obj_t* title = lv_label_create(g_content);
  lv_label_set_text(title, "搜索");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &font_zh_16, 0);

  /* ── 输入行：输入框 + 搜索 ── */
  lv_obj_t* row = makeRow(g_content, false);
  g_searchTa = lv_textarea_create(row);
  lv_obj_set_size(g_searchTa, 344, 40);
  lv_textarea_set_one_line(g_searchTa, true);
  lv_textarea_set_placeholder_text(g_searchTa, "输入关键词");
  lv_obj_set_style_text_font(g_searchTa, &font_zh_16, 0);
  lv_obj_set_style_text_font(g_searchTa, &font_zh_16,
                             LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_text_color(g_searchTa, lv_color_white(), 0);
  lv_obj_set_style_bg_color(g_searchTa, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(g_searchTa, lv_color_hex(0x555555), 0);
  lv_obj_set_style_border_width(g_searchTa, 1, 0);
  lv_obj_set_style_radius(g_searchTa, 8, 0);
  lv_obj_add_event_cb(g_searchTa, search_focus_cb, LV_EVENT_FOCUSED, NULL);
  lv_obj_add_event_cb(g_searchTa, search_focus_cb, LV_EVENT_DEFOCUSED, NULL);
  lv_obj_add_event_cb(g_searchTa, search_go_cb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(g_searchTa, ime_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t* go = makeChipBtn(row, "搜索", search_go_cb, NULL);

  /* 搜索引擎切换：点一下换下一个（必应 -> 360 -> 百度 -> …），
     名字旁显示当前引擎，选中态白底黑字。 */
  lv_obj_t* eb = makeChipBtn(row, kEngines[g_engineIdx].name,
                             engine_switch_cb, NULL);
  lv_obj_set_size(eb, 68, 36);
  lv_obj_set_style_bg_color(eb, lv_color_hex(0x2a2a2a), 0);
  lv_obj_set_style_border_color(eb, lv_color_hex(0xFFD700), 0);
  lv_obj_set_size(go, 96, 40);

  /* 搜索引擎固定必应 —— 百度因体积与反爬已弃用（见文件头注释），不再给切换入口 */

  /* ── 常用词：键盘只能敲 ASCII，先给几个能直接点的 ── */
  lv_obj_t* presetTitle = lv_label_create(g_content);
  lv_label_set_text(presetTitle, "常用");
  lv_obj_set_style_text_color(presetTitle, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(presetTitle, &font_zh_16, 0);
  lv_obj_t* pRow = makeRow(g_content, true);
  static const char* kPresets[] = {"ESP32", "LVGL", "Arduino", "FreeRTOS",
                                   "ST7701S"};
  for (int i = 0; i < 5; i++)
    makeChipBtn(pRow, kPresets[i], preset_cb, (void*)kPresets[i]);

  /* ── 进页面自动拉一次（master 2026-09-25）──
     规则：首次进（或当天还没自动刷过）就拉默认源 kNewsPlatforms[0]（豆瓣）。
     ⚠️ 判断"当天"要用真实日期（NTP 校准后），不能用 millis()
        —— millis 只有开机时长，重启就白记了。拿不到时间就退化为每次进都拉。 */
  if (g_newsCount <= 0) {
    time_t now = 0;
    struct tm tmv;
    time(&now);
    uint32_t day = 0;
    if (now > 1000000000 && localtime_r(&now, &tmv)) {
      day = (uint32_t)((tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 +
                       tmv.tm_mday);
    }
    if (day == 0 || day != g_newsAutoDay) {
      g_newsAutoDay = day;
      Serial.printf("[Browser] news auto-fetch day=%u src=%s\n", day,
                    kNewsPlatforms[0].code);
      startNews(kNewsPlatforms[0].code);
    }
  }

  /* ── 热点新闻（news.orz.ai）──
     平台 chip 点了会起后台任务；条目点了打开原文。数据来自 g_news，
     搜索首页每次重建时都重画一遍 —— 所以不用管"刷新后 UI 怎么更新"。 */
  lv_obj_t* newsTitle = lv_label_create(g_content);
  lv_label_set_text(newsTitle, "热点新闻");
  lv_obj_set_style_text_color(newsTitle, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(newsTitle, &font_zh_16, 0);

  lv_obj_t* npRow = makeRow(g_content, true);
  for (int i = 0; i < kNewsPlatformCount; i++) {
    const NewsPlatform& np = kNewsPlatforms[i];
    lv_obj_t* b = lv_btn_create(npRow);
    /* 宽度按名字长度自适应："稀土掘金"这种 4 字名固定 76 会被挤扁 */
    int cw = 0;
    for (const char* p = np.name; *p; ++p)
      cw += (*(unsigned char*)p < 0x80) ? 8 : 17;
    if (np.star) cw += 14;
    lv_obj_set_size(b, cw + 24, 36);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x333333), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x444444), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(b, 2, 0);
    lv_obj_add_event_cb(b, news_platform_cb, LV_EVENT_CLICKED,
                        (void*)np.code);

    lv_obj_t* lb = lv_label_create(b);
    lv_label_set_text(lb, np.name);
    lv_obj_set_style_text_color(lb, lv_color_white(), 0);
    lv_obj_set_style_text_font(lb, &font_zh_16, 0);

    /* 星标：2=金星（详情页完全能看） 1=白星（部分内容/网络问题） 0=不标。
       ⚠️ 星星单独一个 label：选中态要把**文字**刷成黑色，
          而星星"选中的时候也是金色"，不能跟着变。 */
    if (np.star) {
      lv_obj_t* st = lv_label_create(b);
      lv_label_set_text(st, "*");
      lv_obj_set_style_text_color(st,
          lv_color_hex(np.star == 2 ? 0xFFD700 : 0xDDDDDD), 0);
      lv_obj_set_style_text_font(st, &font_zh_16, 0);
    }

    /* 当前正在看的源 -> 白底黑字（选中态）。
       master 2026-09-25：「正在被选中的热点新闻应该有一个高亮」——
       不然点完根本看不出列表是哪个源的。g_newsCount<=0 表示还没拉过，不高亮。 */
    if (g_newsCount > 0 && strcmp(g_newsPlatform, np.code) == 0) {
      lv_obj_set_style_bg_color(b, lv_color_white(), 0);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
      lv_obj_set_style_border_color(b, lv_color_white(), 0);
      lv_obj_set_style_text_color(lb, lv_color_black(), 0);
    }
  }

  /* ── 更新按钮 + 上次更新时间 ──
     master：「在底部留一个更新按钮，点击之后才会主动更新这个新闻源」。
     默认 30 分钟内不会重复联网，所以要强制刷新就点它。 */
  {
    lv_obj_t* ur = lv_obj_create(g_content);
    lv_obj_set_width(ur, lv_pct(100));
    lv_obj_set_height(ur, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ur, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ur, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(ur, 8, 0);
    lv_obj_clear_flag(ur, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ur, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ur, 0, 0);
    lv_obj_set_style_pad_all(ur, 0, 0);

    lv_obj_t* rb = makeChipBtn(ur, "更新", news_refresh_cb, NULL);
    lv_obj_set_size(rb, 72, 32);

    lv_obj_t* tl = lv_label_create(ur);
    uint32_t age = newsAgeSec(g_newsPlatform);
    if (g_newsCount > 0 && age) {
      char t[40];
      if (age < 60) snprintf(t, sizeof(t), "%s · 刚刚更新", g_newsPlatform);
      else if (age < 3600) snprintf(t, sizeof(t), "%s · %u 分钟前更新",
                                    g_newsPlatform, (unsigned)(age / 60));
      else snprintf(t, sizeof(t), "%s · %u 小时前更新",
                    g_newsPlatform, (unsigned)(age / 3600));
      lv_label_set_text(tl, t);
    } else {
      lv_label_set_text(tl, "每 30 分钟更新一次");
    }
    lv_obj_set_style_text_color(tl, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(tl, &font_zh_16, 0);
  }

  lv_obj_t* box = lv_obj_create(g_content);
  lv_obj_set_width(box, lv_pct(100));
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(box, 8, 0);
  lv_obj_set_style_pad_gap(box, 6, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(box, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0x333333), 0);
  lv_obj_set_style_radius(box, 8, 0);

  if (g_newsCount <= 0) {
    lv_obj_t* hint = lv_label_create(box);
    lv_label_set_text(hint, g_newsCount < 0 ? "拉取失败，换个源试试" : "点上面的源加载");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(hint, &font_zh_16, 0);
  } else {
    for (int i = 0; i < g_newsCount && i < NEWS_MAX_ITEMS; i++) {
      lv_obj_t* it = lv_btn_create(box);
      lv_obj_set_width(it, lv_pct(100));
      lv_obj_set_height(it, LV_SIZE_CONTENT);
      lv_obj_set_style_bg_opa(it, LV_OPA_TRANSP, 0);
      lv_obj_set_style_bg_color(it, lv_color_hex(0x222222), LV_STATE_PRESSED);
      lv_obj_set_style_bg_opa(it, LV_OPA_COVER, LV_STATE_PRESSED);
      lv_obj_set_style_border_width(it, 0, 0);
      lv_obj_set_style_radius(it, 6, 0);
      lv_obj_set_style_pad_all(it, 4, 0);
      lv_obj_add_event_cb(it, news_item_cb, LV_EVENT_CLICKED,
                          (void*)(intptr_t)i);

      /* 序号用 ASCII，标题用中文字体（montserrat 是纯拉丁，会出豆腐块） */
      lv_obj_t* lab = lv_label_create(it);
      char num[8];
      snprintf(num, sizeof(num), "%d. ", i + 1);
      lv_label_set_text(lab, (String(num) + g_news[i].title).c_str());
      lv_obj_set_width(lab, lv_pct(100));
      lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_color(lab, lv_color_hex(0xDDDDDD), 0);
      lv_obj_set_style_text_font(lab, &font_zh_16, 0);
    }
  }

  /* ── 下载管理入口：点进去看 LittleFS 里存下来的所有页面 ──
     回调里只置一个"待办"，真正的重建在下一 tick（见 g_uiPendingKind 的说明）。 */
  lv_obj_t* dlRow = makeRow(g_content, false);
  makeChipBtn(dlRow, "下载的网站", dl_open_list_cb, NULL);

  Serial.println("[Browser] search home shown");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 下载的网站（LittleFS 列表 + 删除管理）
 *
 * 文件名只有 URL 的 hash（/pXXXXXXXX.html），所以「下载」时我们把原 URL 写在
 * 文件第一行 <!--URL:xxx--> —— 列表页靠它显示可读名字，离线重看也靠它回填。
 *
 * ⚠️ 两条安全规矩（都是这个项目用崩溃换来的）：
 *   1. **回调里不许重建列表 / 不许开页面**。那样等于 LVGL 正在派发某个 widget
 *      的事件时把它的父容器 lv_obj_clean 掉。所有跳转统一走 tick 里的延迟通道。
 *      只有纯文件操作（删除）可以当场做。
 *   2. **LittleFS 挂载后不许 end()** —— 会把正在跑的页面服务器整成 404
 *      （listSavedPages 踩过。LittleFS.begin() 幂等，已挂载时直接返回 true。
 * ═══════════════════════════════════════════════════════════════════════════ */
#define MAX_DL_ROWS 14

/* 自建 UI 之间的跳转：延迟到下一 tick 执行（理由见上）
   ⚠️ PendingUiKind / g_uiPendingKind 已移到文件前部（startNews 要用） */
static char g_dlPendingPath[64] = {0};   /* 待离线打开的下载文件 */

static void showDownloadsHome();   /* 下面会用到（定义在最后） */

static void dl_goto(int kind) {
  g_uiPendingKind = kind;
}

static void dl_open_list_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  dl_goto(UI_PEND_DOWNLOADS);
}

static void dl_back_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  dl_goto(UI_PEND_SEARCH);
}

static void dl_open_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char* p = (const char*)lv_event_get_user_data(e);
  if (!p) return;
  snprintf(g_dlPendingPath, sizeof(g_dlPendingPath), "%s", p);
}

/* chip 的 user_data 是 malloc 出来的路径；contentReset() 把整棵内容树 clean 掉时
   由这个回调回收 —— 否则每进一次列表就漏一份，几天后 DRAM 单调下降到崩。 */
static void dl_chip_free_ud(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  char* p = (char*)lv_event_get_user_data(e);
  if (p) free(p);
}

/* 取出下载时写在第一行的 <!--URL:xxx--> */
static bool savedFileUrl(const char* path, String& url) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  String first = f.readStringUntil('\n');
  f.close();
  int a = first.indexOf("<!--URL:");
  if (a < 0) return false;
  int b = first.indexOf("-->", a + 8);
  if (b < 0) return false;
  url = first.substring(a + 8, b);
  return url.length() > 0;
}

/* 把一个已下载的页面塞回页面缓存 —— 之后 startFetch(url) 直接命中，不再联网。
   pageCachePut 内部会拷一份，这里的临时 buffer 用完就还。 */
static bool savedToCache(const char* path, String& url) {
  if (!savedFileUrl(path, url)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t sz = f.size();
  bool ok = false;
  if (sz > 0 && sz <= PAGE_CACHE_MAX_ENTRY) {
    uint8_t* buf = (uint8_t*)heap_caps_malloc(
        sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf) {
      f.readBytes((char*)buf, sz);
      pageCachePut(url, buf, sz);
      heap_caps_free(buf);
      ok = true;
    } else {
      Serial.println("[Browser] savedToCache: PSRAM alloc failed");
    }
  }
  f.close();
  return ok;
}

static void dl_del_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char* p = (const char*)lv_event_get_user_data(e);
  if (!p) return;
  if (!LittleFS.begin(false)) { toast("存储不可用"); return; }
  bool ok = LittleFS.remove(p);
  Serial.printf("[Browser] delete saved %s -> %s\n", p, ok ? "ok" : "FAIL");
  if (ok) {
    toast("已删除");
    g_uiPendingKind = UI_PEND_DOWNLOADS;   /* 下一 tick 重建列表 */
  } else {
    toast("删除失败");
  }
}

/* 删掉 LittleFS 里所有已存页面。返回删了几个。
   ⚠️ 挂载后不 end() —— 会打挂正在运行的页面服务器。 */
static int clearSavedPages() {
  if (!LittleFS.begin(false)) return -1;
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  int n = 0;
  while (f) {
    String nm = String(f.name());
    f = root.openNextFile();          /* 先推进迭代，再删当前项 */
    if (!nm.endsWith(".html")) continue;
    if (!nm.startsWith("/")) nm = String("/") + nm;
    if (LittleFS.remove(nm)) n++;
  }
  Serial.printf("[Browser] cleared %d saved pages\n", n);
  return n;
}

/* 清空全部：破坏性操作，要求再点一次确认（3 秒有效） */
static uint32_t s_clearArmUntil = 0;
static void dl_clear_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  uint32_t now = millis();
  if (s_clearArmUntil == 0 || now > s_clearArmUntil) {
    s_clearArmUntil = now + 3000;
    toast("再点一次确认清空");
    return;
  }
  s_clearArmUntil = 0;
  int n = clearSavedPages();
  if (n < 0) { toast("存储不可用"); return; }
  char msg[48];
  snprintf(msg, sizeof(msg), "已清空 %d 个", n);
  toast(msg);
  g_uiPendingKind = UI_PEND_DOWNLOADS;
}

/* ── 列表本体 ──
   每行 = [可读名字 + 大小] [删]：点名字离线重看，点「删」删掉这一项。 */
static void showDownloadsHome() {
  if (!g_content || !lv_obj_is_valid(g_content)) return;
  hideLoadingOverlay();
  contentReset();
  g_state = BROWSER_LOADED;
  g_currentUrl = "";
  if (g_urlArea && lv_obj_is_valid(g_urlArea))
    lv_label_set_text(g_urlArea, "下载的网站");
  updateNavButtons();

  lv_obj_t* title = lv_label_create(g_content);
  lv_label_set_text(title, "下载的网站");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &font_zh_16, 0);

  lv_obj_t* row = makeRow(g_content, false);
  makeChipBtn(row, "返回", dl_back_cb, NULL);
  makeChipBtn(row, "清空全部", dl_clear_cb, NULL);

  /* ⚠️ 挂载后不 end()：LittleFS.begin() 幂等，end() 反而会打挂页面服务器 */
  if (!LittleFS.begin(false)) {
    lv_obj_t* m = lv_label_create(g_content);
    lv_label_set_text(m, "存储不可用");
    lv_obj_set_style_text_color(m, lv_color_hex(0xFF6666), 0);
    lv_obj_set_style_text_font(m, &font_zh_16, 0);
    return;
  }

  File root = LittleFS.open("/");
  File f = root.openNextFile();
  int n = 0;
  while (f && n < MAX_DL_ROWS) {
    String nm = String(f.name());
    size_t sz = f.size();
    f = root.openNextFile();          /* 先推进，后面要 open 同一个文件读 URL */
    if (!nm.endsWith(".html")) continue;
    if (!nm.startsWith("/")) nm = String("/") + nm;

    String url;
    /* 下载功能加上 URL 头之前存的老文件没有头 —— 退化成显示文件名 */
    if (!savedFileUrl(nm.c_str(), url)) url = nm;

    String shown = url;
    int se = shown.indexOf("://");
    if (se > 0) shown = shown.substring(se + 3);   /* 砍掉 https:// */
    if (shown.length() > 26) shown = shown.substring(0, 26);
    char label[80];
    snprintf(label, sizeof(label), "%s  %uK", shown.c_str(), (unsigned)(sz / 1024));

    lv_obj_t* r = makeRow(g_content, false);
    char* dup = (char*)malloc(nm.length() + 1);
    char* dup2 = (char*)malloc(nm.length() + 1);
    if (!dup || !dup2) { free(dup); free(dup2); break; }
    strcpy(dup, nm.c_str());
    strcpy(dup2, nm.c_str());

    lv_obj_t* openBtn = makeChipBtn(r, label, dl_open_cb, dup);
    lv_obj_set_width(openBtn, 340);
    lv_obj_add_event_cb(openBtn, dl_chip_free_ud, LV_EVENT_DELETE, dup);
    lv_obj_t* delBtn = makeChipBtn(r, "删", dl_del_cb, dup2);
    lv_obj_set_width(delBtn, 52);
    lv_obj_add_event_cb(delBtn, dl_chip_free_ud, LV_EVENT_DELETE, dup2);
    n++;
  }

  if (n == 0) {
    lv_obj_t* m = lv_label_create(g_content);
    lv_label_set_text(m, "还没有下载任何页面");
    lv_obj_set_style_text_color(m, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(m, &font_zh_16, 0);
  }
  Serial.printf("[Browser] downloads list: %d rows\n", n);
}

}  // namespace

/* ═══════════════════════════════════════════════════════════════════════════
 * 创建浏览器屏幕
 * ═══════════════════════════════════════════════════════════════════════════ */
/* ═══════════════════════════════════════════════════════════════════════════
 * 底部可切换工具栏
 *   组 A（默认）：  退出 / 网址 / 加载
 *   组 B（点「更多」）：后退 / 前进 / 首页 / 加载 / 退出
 *   两组共用屏幕底部 444 那一行；切换按钮就占在原「已加载」状态位上，
 *   配色与其余底部按钮完全一致（0x1a1a1a 底 / #444 描边 / 6 圆角 / 白字）。
 * ═══════════════════════════════════════════════════════════════════════════ */
static lv_obj_t* g_navBar = nullptr;     /* 组 B 容器 */
static lv_obj_t* g_switchBtn = nullptr;  /* 底栏最右：三点 = 回到我们的搜索首页 */
static lv_obj_t* g_switchIcon = nullptr; /* 三点(更多) ↔ 左箭头(返回)，就地重画 */
static const uint16_t kBrowserIconSize = 26;
static bool g_navModeB = true;           /* true = 显示组 B（导航键），false = 组 A（搜索框+刷新+退出） */
static lv_obj_t* g_urlBar = nullptr;     /* 组 A 容器：URL 搜索框 + 刷新 + 退出浏览器 */
static lv_obj_t* g_exitTopBtn = nullptr; /* 组 A 的「退出浏览器」 */

/* 按 g_navModeB 把两组按钮的可见性落到 UI 上（唯一出口，别到处手搓 flag） */
static void applyBarMode() {
  /* true = 组 B（后退/前进/首页/刷新/下载）；false = 组 A（搜索框/刷新/退出浏览器） */
  const bool b = g_navModeB;
  if (g_urlBar)  b ? lv_obj_add_flag(g_urlBar, LV_OBJ_FLAG_HIDDEN)
                  : lv_obj_clear_flag(g_urlBar, LV_OBJ_FLAG_HIDDEN);
  if (g_navBar)  b ? lv_obj_clear_flag(g_navBar, LV_OBJ_FLAG_HIDDEN)
                  : lv_obj_add_flag(g_navBar, LV_OBJ_FLAG_HIDDEN);
  /* 三点 = 还有一组（组 A）；左箭头 = 回组 B。就地重画同一块画布，不再 malloc。 */
  if (g_switchIcon) icon_set_type(g_switchIcon, b ? Icon::More : Icon::Back, kBrowserIconSize);
  if (b) updateNavButtons();
}

/* 三点（⋯）= 更多：展开组 A —— 底部搜索框(URL) + 刷新 + 退出浏览器。
   再点一次（图标变左箭头）回到组 B 的导航键。
   ⚠️ 它**不是**回首页：回我们自己做的小引擎首页是组 B 里的房子键。 */
static void switch_bar_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_navModeB = !g_navModeB;
  applyBarMode();
}

lv_obj_t* BrowserScreen_create() {
  /* ⚠️ g_firstLoad 是文件作用域变量，进程级只初始化一次。
     浏览器 Activity 退出时被 nav 销毁（lv_obj_del + onDestroy），下次再进
     是全新 create —— 但 g_firstLoad 还停留在 false，于是
     BrowserScreen_tick 里那个 "首次进入 → showSearchHome()" 分支永远不再
     触发，表现就是「第二次打开浏览器一片空白，没有初始页」。
     每次 create 都要重置它。 */
  g_firstLoad = true;

  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_RELEASED, NULL);

  /* ── 顶栏：系统状态栏 (0-28) ── */
  StatusBar_create(scr, "浏览器");

  /* ── 组 A：URL 搜索框 + 刷新 + 退出浏览器（由三点键展开，默认隐藏）──
     与组 B（后退/前进/首页/刷新/下载）互斥，两组共用底部 444 这一行。 */
  g_urlBar = lv_obj_create(scr);
  lv_obj_set_size(g_urlBar, 396, 36);
  lv_obj_align(g_urlBar, LV_ALIGN_TOP_LEFT, 2, 444);
  lv_obj_set_style_bg_opa(g_urlBar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_urlBar, 0, 0);
  lv_obj_set_style_pad_all(g_urlBar, 0, 0);
  lv_obj_clear_flag(g_urlBar, LV_OBJ_FLAG_SCROLLABLE);

  /* URL 栏用 label 而非 textarea：
     textarea 是 LVGL 里最贵的对象之一（内含 label + 光标 + 游标层），而它在本项目
     根本无法输入（键盘依赖串口）。换成 label 直接省下一块 DRAM。
     当前 URL 由 g_currentUrl 维护（label 只负责显示）。 */
  g_urlArea = lv_label_create(g_urlBar);
  lv_obj_set_size(g_urlArea, 286, 36);
  lv_obj_align(g_urlArea, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_label_set_long_mode(g_urlArea, LV_LABEL_LONG_DOT);  // 过长截断，不撑破布局
  setUrlText("搜索首页");
  lv_obj_add_flag(g_urlArea, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_text_font(g_urlArea, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(g_urlArea, lv_color_white(), 0);
  lv_obj_set_style_border_color(g_urlArea, lv_color_hex(0x444444), 0);
  lv_obj_set_style_border_width(g_urlArea, 1, 0);
  lv_obj_set_style_radius(g_urlArea, 6, 0);
  lv_obj_set_style_bg_color(g_urlArea, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_opa(g_urlArea, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_left(g_urlArea, 6, 0);
  lv_obj_add_event_cb(g_urlArea, url_focus_cb, LV_EVENT_CLICKED, NULL);

  /* 组 A 的「刷新」：与组 B 第 4 格是两颗独立按钮，回调都走 go_cb（读 g_currentUrl） */
  g_goBtn = lv_btn_create(g_urlBar);
  lv_obj_set_size(g_goBtn, 52, 36);
  lv_obj_align(g_goBtn, LV_ALIGN_TOP_LEFT, 292, 0);
  lv_obj_set_style_bg_color(g_goBtn, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_bg_color(g_goBtn, lv_color_hex(0x333333), LV_STATE_PRESSED);
  lv_obj_set_style_radius(g_goBtn, 6, 0);
  lv_obj_set_style_border_width(g_goBtn, 1, 0);
  lv_obj_set_style_border_color(g_goBtn, lv_color_hex(0x444444), 0);
  lv_obj_add_event_cb(g_goBtn, go_cb, LV_EVENT_CLICKED, NULL);
  { lv_obj_t* gi = icon_create(g_goBtn, Icon::Refresh, 26); lv_obj_center(gi); }

  /* 组 A 的「退出浏览器」：门+箭头，回启动器（不是回搜索首页） */
  g_exitTopBtn = lv_btn_create(g_urlBar);
  lv_obj_set_size(g_exitTopBtn, 52, 36);
  lv_obj_align(g_exitTopBtn, LV_ALIGN_TOP_LEFT, 348, 0);
  lv_obj_set_style_bg_color(g_exitTopBtn, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_bg_color(g_exitTopBtn, lv_color_hex(0x333333), LV_STATE_PRESSED);
  lv_obj_set_style_radius(g_exitTopBtn, 6, 0);
  lv_obj_set_style_border_width(g_exitTopBtn, 1, 0);
  lv_obj_set_style_border_color(g_exitTopBtn, lv_color_hex(0x444444), 0);
  lv_obj_add_event_cb(g_exitTopBtn, exit_event_cb, LV_EVENT_CLICKED, NULL);
  { lv_obj_t* ei = icon_create(g_exitTopBtn, Icon::ExitDoor, 26); lv_obj_center(ei); }


  /* 状态文字：原占 406 那一格，现在让位给切换按钮。对象保留但隐藏 ——
     startFetch/tick/close 里几十处 lv_label_set_text(g_status, ...) 照常调用，
     隐藏后不影响任何逻辑，将来要恢复显示只要去掉这一行。 */
  g_status = lv_label_create(scr);
  lv_label_set_text(g_status, WiFi.status() == WL_CONNECTED ? "就绪" : "无WiFi");
  lv_obj_set_style_text_color(g_status, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(g_status, &font_zh_16, 0);
  lv_obj_align(g_status, LV_ALIGN_TOP_LEFT, 406, 452);
  lv_obj_add_flag(g_status, LV_OBJ_FLAG_HIDDEN);

  /* ── 内容区 (40-410) ── */
  g_content = lv_obj_create(scr);
  lv_obj_set_size(g_content, 476, CONTENT_H);
  lv_obj_align(g_content, LV_ALIGN_TOP_LEFT, 2, 28);
  lv_obj_set_style_bg_color(g_content, lv_color_hex(0x0a0a0a), 0);
  lv_obj_set_style_bg_opa(g_content, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_content, 0, 0);
  lv_obj_set_style_pad_all(g_content, 6, 0);
  lv_obj_set_scroll_dir(g_content, LV_DIR_VER);
  /* ⚠️ 必须显式关滚动条：LVGL 默认 LV_SCROLLBAR_MODE_AUTO，会在可滚动时
     沿内容区边沿画一条滚动条。本工程没启用自定义 scrollbar 样式（暗色 UI），
     它用的是默认样式的浅色 —— 表现就是「滚动时边上一条白线」
     （docs/09 B3 记的那条）。desktop_screen.cpp 早已这么处理，浏览器这里漏了。 */
  lv_obj_set_scrollbar_mode(g_content, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_text_color(g_content, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_text_font(g_content, &font_zh_16, 0);
  lv_obj_set_flex_flow(g_content, LV_FLEX_FLOW_COLUMN);
  /* 块与块之间留 6px（原来是 2px，全挤在一起分不出哪条是哪条）；
     搜索结果条目自己还会再加 12px 底部留白 + 一条分隔线。 */
  lv_obj_set_style_pad_gap(g_content, 6, 0);

  /* ── 搜索首页用的键盘（常驻隐藏，聚焦时才显示）──
     挂在屏上而不是 g_content 里：网页内容会被 lv_obj_clean 清掉，键盘不能跟着没。
     也不反复 create/delete —— 那是打碎 LVGL 堆的最快方式。 */
  g_searchKb = lv_keyboard_create(scr);
  lv_obj_set_size(g_searchKb, 480, 240);
  lv_obj_align(g_searchKb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_mode(g_searchKb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_add_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(g_searchKb, kb_hide_cb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(g_searchKb, kb_hide_cb, LV_EVENT_CANCEL, NULL);

  /* ── 中文候选条：贴在键盘上沿，横向可滑（候选最多 12 个，屏宽放不下就滑）──
     挂在屏上而不是 g_content 里：网页内容会被 lv_obj_clean 清掉。 */
  g_imeBar = lv_obj_create(scr);
  lv_obj_set_size(g_imeBar, 480, 44);
  lv_obj_align_to(g_imeBar, g_searchKb, LV_ALIGN_OUT_TOP_MID, 0, -2);
  lv_obj_set_flex_flow(g_imeBar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_imeBar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(g_imeBar, 4, 0);
  lv_obj_set_style_pad_gap(g_imeBar, 4, 0);
  lv_obj_set_style_border_width(g_imeBar, 0, 0);
  lv_obj_set_style_radius(g_imeBar, 0, 0);
  lv_obj_set_style_bg_color(g_imeBar, lv_color_hex(0x0a0a0a), 0);
  lv_obj_set_style_bg_opa(g_imeBar, LV_OPA_COVER, 0);
  lv_obj_set_scroll_dir(g_imeBar, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(g_imeBar, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(g_imeBar, LV_OBJ_FLAG_HIDDEN);

  /* ── 加载遮罩（覆盖在内容区上，默认隐藏） ── */
  g_loadingOverlay = lv_obj_create(scr);
  lv_obj_set_size(g_loadingOverlay, 476, CONTENT_H);
  lv_obj_align(g_loadingOverlay, LV_ALIGN_TOP_LEFT, 2, 28);
  lv_obj_set_style_bg_color(g_loadingOverlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_loadingOverlay, LV_OPA_80, 0);
  lv_obj_set_style_border_width(g_loadingOverlay, 0, 0);
  lv_obj_set_style_radius(g_loadingOverlay, 0, 0);
  lv_obj_set_style_pad_all(g_loadingOverlay, 0, 0);
  lv_obj_clear_flag(g_loadingOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_loadingOverlay, LV_OBJ_FLAG_HIDDEN);

  /* "加载中，请稍等..." 文字 */
  g_loadingLabel = lv_label_create(g_loadingOverlay);
  lv_label_set_text(g_loadingLabel, "加载中，请稍等...");
  lv_obj_set_style_text_color(g_loadingLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(g_loadingLabel, &font_zh_16, 0);
  lv_obj_align(g_loadingLabel, LV_ALIGN_TOP_MID, 0, 80);

  /* 进度条 */
  g_loadProgressBar = lv_bar_create(g_loadingOverlay);
  lv_obj_set_size(g_loadProgressBar, 300, 12);
  lv_obj_align(g_loadProgressBar, LV_ALIGN_TOP_MID, 0, 120);
  lv_bar_set_range(g_loadProgressBar, 0, 100);
  lv_bar_set_value(g_loadProgressBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_loadProgressBar, lv_color_hex(0x222222), 0);
  lv_obj_set_style_bg_color(g_loadProgressBar, lv_color_hex(0x4488ff), LV_PART_INDICATOR);

  /* 进度文字 */
  g_loadProgressText = lv_label_create(g_loadingOverlay);
  lv_label_set_text(g_loadProgressText, "");
  lv_obj_set_style_text_color(g_loadProgressText, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(g_loadProgressText, &lv_font_montserrat_14, 0);
  lv_obj_align(g_loadProgressText, LV_ALIGN_TOP_MID, 0, 140);

  /* 大停止按钮 */
  g_stopBtn = lv_btn_create(g_loadingOverlay);
  lv_obj_set_size(g_stopBtn, 120, 50);
  lv_obj_align(g_stopBtn, LV_ALIGN_TOP_MID, 0, 180);
  lv_obj_set_style_bg_color(g_stopBtn, lv_color_hex(0x8B0000), 0);
  lv_obj_set_style_bg_color(g_stopBtn, lv_color_hex(0xFF0000), LV_STATE_PRESSED);
  lv_obj_set_style_radius(g_stopBtn, 8, 0);
  lv_obj_set_style_border_width(g_stopBtn, 2, 0);
  lv_obj_set_style_border_color(g_stopBtn, lv_color_hex(0xFF6666), 0);
  lv_obj_add_event_cb(g_stopBtn, stop_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* stopLbl = lv_label_create(g_stopBtn);
  lv_label_set_text(stopLbl, "停止");
  lv_obj_set_style_text_color(stopLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(stopLbl, &font_zh_16, 0);
  lv_obj_center(stopLbl);

  /* ── 进度条 + 进度文字 (410-435) ── */
  g_progressBar = lv_bar_create(scr);
  /* 进度条改为内容区顶部细条（4px），不再压住网页内容 */
  lv_obj_set_size(g_progressBar, 476, 4);
  lv_obj_align(g_progressBar, LV_ALIGN_TOP_LEFT, 2, 28);
  lv_bar_set_range(g_progressBar, 0, 100);
  lv_bar_set_value(g_progressBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_progressBar, lv_color_hex(0x222222), 0);
  lv_obj_set_style_bg_color(g_progressBar, lv_color_hex(0x4488ff), LV_PART_INDICATOR);

  g_progressLabel = lv_label_create(scr);
  lv_label_set_text(g_progressLabel, "");
  lv_obj_set_style_text_color(g_progressLabel, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(g_progressLabel, &lv_font_montserrat_14, 0);
  lv_obj_align(g_progressLabel, LV_ALIGN_TOP_LEFT, 300, 34);

  /* ── 底栏最右：组 A ↔ 组 B 切换按钮（占原「已加载」状态位，配色同组 A） ── */
  g_switchBtn = lv_btn_create(scr);
  lv_obj_set_size(g_switchBtn, 74, 36);
  lv_obj_align(g_switchBtn, LV_ALIGN_TOP_LEFT, 404, 444);
  lv_obj_set_style_bg_color(g_switchBtn, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_bg_color(g_switchBtn, lv_color_hex(0x333333), LV_STATE_PRESSED);
  lv_obj_set_style_radius(g_switchBtn, 6, 0);
  lv_obj_set_style_border_width(g_switchBtn, 1, 0);
  lv_obj_set_style_border_color(g_switchBtn, lv_color_hex(0x444444), 0);
  lv_obj_add_event_cb(g_switchBtn, switch_bar_cb, LV_EVENT_CLICKED, NULL);
  g_switchIcon = icon_create(g_switchBtn, Icon::More, kBrowserIconSize);
  lv_obj_center(g_switchIcon);

  /* ── 组 B：后退 / 前进 / 首页 / 加载 / 退出（同一行 444，默认隐藏） ──
     不用 flex：五个等宽按钮手工排布（76px 宽 + 4px 间距，2→398），
     切换按钮固定落 404，两组切换时它不挪窝，手指不会按空。 */
  g_navBar = lv_obj_create(scr);
  lv_obj_set_size(g_navBar, 396, 36);
  lv_obj_align(g_navBar, LV_ALIGN_TOP_LEFT, 2, 444);
  lv_obj_set_style_bg_opa(g_navBar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_navBar, 0, 0);
  lv_obj_set_style_pad_all(g_navBar, 0, 0);
  lv_obj_clear_flag(g_navBar, LV_OBJ_FLAG_SCROLLABLE);
  /* 常驻显示：不再有隐藏的组 B */

  auto makeNavBtn = [&](lv_obj_t** btn, Icon type, lv_event_cb_t cb, int x) {
    *btn = lv_btn_create(g_navBar);
    lv_obj_set_size(*btn, 76, 36);
    lv_obj_align(*btn, LV_ALIGN_TOP_LEFT, x, 0);
    lv_obj_set_style_bg_color(*btn, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_color(*btn, lv_color_hex(0x333333), LV_STATE_PRESSED);
    lv_obj_set_style_radius(*btn, 6, 0);
    lv_obj_set_style_border_width(*btn, 1, 0);
    lv_obj_set_style_border_color(*btn, lv_color_hex(0x444444), 0);
    lv_obj_add_event_cb(*btn, cb, LV_EVENT_CLICKED, NULL);
    /* 图标直接在按钮里居中。灰态（不可用时）走 set_nav_enabled：
       按钮压 bg_opa，图标压 obj opa —— LVGL 8 的 opa 不会级联到子对象，
       两处都要设，否则"灰掉"的按钮上图标还是纯白。 */
    lv_obj_t* ic = icon_create(*btn, type, kBrowserIconSize);
    lv_obj_center(ic);
  };

  makeNavBtn(&g_backBtn, Icon::Back,     back_nav_cb,    0);
  makeNavBtn(&g_fwdBtn,  Icon::Forward,  fwd_nav_cb,    80);
  makeNavBtn(&g_homeBtn, Icon::Home,     home_nav_cb,  160);
  /* 「加载」复用 go_cb（读 g_currentUrl）；与组 A 的 g_goBtn 是两颗独立按钮 */
  { lv_obj_t* reloadBtn = nullptr; makeNavBtn(&reloadBtn, Icon::Refresh, go_cb, 240); }
  /* 第 5 格：原「退出」→「下载」（把当前页存到 LittleFS） */
  makeNavBtn(&g_dlBtn, Icon::Download, download_cb, 320);

  applyBarMode();   /* 落到组 A，并把切换按钮标题刷成「更多」 */

  /* 提示条（下载结果等）：不带定时器，靠下次导航/回首页顺手隐藏。
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
}

/* 串口命令 dl / 底栏「下载」键共用：把当前页存进 LittleFS。
   downloadCurrentPage() 在匿名 namespace 里，这里给它一个外部入口。 */
void BrowserScreen_download() { downloadCurrentPage(); }

/* ══ 存下来的页面怎么读：板子自己当一个小 Web 服务器 ══
 * 页面存在 flash 里，板子自己没法舒服地读它（只有 480 屏）。但板子在 WiFi 里 ——
 * 起个 80 端口的服务，电脑浏览器打开 http://<板子IP>/ 就是文件列表，点开就是那一页。
 * 串口命令：serve / servestop / ls
 * ⚠️ handleClient() 必须被周期性调用，挂在 BrowserScreen_tick() 里。 */
#include <WebServer.h>
static WebServer* g_pageSrv = nullptr;

static void pageServerStart() {
  if (g_pageSrv) { Serial.println("[Serve] already running"); return; }
  if (WiFi.status() != WL_CONNECTED) { Serial.println("[Serve] WiFi 未连接"); return; }
  if (!LittleFS.begin(false)) { Serial.println("[Serve] LittleFS 挂载失败"); return; }

  g_pageSrv = new WebServer(80);
  g_pageSrv->on("/", HTTP_GET, []() {
    String html = "<html><head><meta charset='utf-8'></head><body>"
                  "<h3>geek-terminal saved pages</h3><ul>";
    File root = LittleFS.open("/");
    File f = root.openNextFile();
    int n = 0;
    while (f) {
      String nm = String(f.name());
      if (nm.endsWith(".html")) {
        html += "<li><a href='" + nm + "'>" + nm + "</a> (" + f.size() + " B)</li>";
        n++;
      }
      f = root.openNextFile();
    }
    if (n == 0) html += "<li>(还没有保存过页面，先用 dl 存一页)</li>";
    html += "</ul></body></html>";
    g_pageSrv->send(200, "text/html; charset=utf-8", html);
  });
  g_pageSrv->onNotFound([]() {
    String p = g_pageSrv->uri();
    if (!LittleFS.exists(p)) { g_pageSrv->send(404, "text/plain", "not found"); return; }
    File f = LittleFS.open(p, "r");
    g_pageSrv->streamFile(f, "text/html; charset=utf-8");
    f.close();
  });
  g_pageSrv->begin();
  Serial.printf("[Serve] 启动：http://%s/\n", WiFi.localIP().toString().c_str());
}

static void pageServerStop() {
  if (!g_pageSrv) { Serial.println("[Serve] 没在运行"); return; }
  g_pageSrv->stop();
  delete g_pageSrv;
  g_pageSrv = nullptr;
  LittleFS.end();
  Serial.println("[Serve] 已停止");
}

static void pageServerTick() {
  if (g_pageSrv) g_pageSrv->handleClient();
}

static void listSavedPages() {
  /* 服务器在跑时 LittleFS 已被 pageServerStart() 挂上：
     这里再 end() 一次会把服务器的文件访问整垮（实测 404）。 */
  bool fsOwner = (g_pageSrv == nullptr);
  if (fsOwner && !LittleFS.begin(false)) {
    Serial.println("[ls] LittleFS 挂载失败");
    return;
  }
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  int n = 0;
  while (f) {
    Serial.printf("  %-20s %u B\n", f.name(), (unsigned)f.size());
    n++;
    f = root.openNextFile();
  }

  if (n == 0) Serial.println("  (空)");
  Serial.printf("[ls] 共 %d 个，已用 %u / %u B\n", n,
                (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  if (fsOwner) LittleFS.end();
}

void BrowserScreen_serve(bool on) { on ? pageServerStart() : pageServerStop(); }
void BrowserScreen_listPages() { listSavedPages(); }

/* ── 给设置屏的「缓存/下载查看与清理」接口 ──
   2026-09-23 master 要求：内存策略是「只保留当前页 + 上一页」，
   但用户要有地方看见占了多大、能手删。这里是那个出口。 */
void BrowserScreen_cacheInfo(int* pages, size_t* bytes,
                             int* dlCount, size_t* dlBytes) {
  if (pages) *pages = 0;
  if (bytes) *bytes = 0;
  for (int i = 0; i < PAGE_CACHE_SLOTS; i++) {
    if (!g_pageCache[i].data) continue;
    if (pages) (*pages)++;
    if (bytes) *bytes += g_pageCache[i].len;
  }
  if (dlCount) *dlCount = 0;
  if (dlBytes) *dlBytes = 0;
  /* ⚠️ 挂载后不 end() */
  if (!LittleFS.begin(false)) return;
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  while (f) {
    if (String(f.name()).endsWith(".html")) {
      if (dlCount) (*dlCount)++;
      if (dlBytes) *dlBytes += f.size();
    }
    f = root.openNextFile();
  }
}

void BrowserScreen_clearCache() {
  for (int i = 0; i < PAGE_CACHE_SLOTS; i++) pageCacheFree(i);
  Serial.println("[Browser] page cache cleared");
}

int BrowserScreen_clearDownloads() { return clearSavedPages(); }

/* ── tick：状态机驱动 ── */
void BrowserScreen_tick() {
  pageServerTick();   /* 存下来的页面要能被电脑访问 */
  /* 网页里的链接点击（上一 tick 记下的）—— 在这里才真正导航，
     避开"在自己的事件回调里删自己"的 LVGL 崩溃。 */
  if (g_linkPendingSet) {
    String target = g_linkPending;
    g_linkPendingSet = false;
    g_linkPending = "";
    historyPush(target);
    setUrlText(target.c_str());
    startFetch(target);
    return;
  }

  /* 自建 UI 之间的跳转（下载列表 ↔ 搜索首页）：widget 回调里只能置标志，
     真正的重建放这里 —— 否则是在 LVGL 派发期间删掉正在回调的对象。 */
  if (g_uiPendingKind != UI_PEND_NONE) {
    int kind = g_uiPendingKind;
    g_uiPendingKind = UI_PEND_NONE;
    if (kind == UI_PEND_DOWNLOADS) showDownloadsHome();
    else showSearchHome();
    return;
  }

  /* 新闻拉完了：收掉遮罩，回搜索首页把列表画出来。
     ⚠️ 这里只是置标志 + 改状态，重建交给下一 tick 的 UI_PEND 通道 ——
        在 tick 里重建没风险，但保持一条路更好排查。 */
  if (g_fetchKind == FETCH_NEWS && g_taskDone) {
    g_taskDone = false;
    g_fetchKind = FETCH_WEB;
    g_state = BROWSER_LOADED;
    g_currentUrl = "";
    hideLoadingOverlay();
    ScreenSaver::setSuppressed(false);
    if (g_urlArea && lv_obj_is_valid(g_urlArea))
      lv_label_set_text(g_urlArea, "热点新闻");
    toast(g_newsCount > 0 ? "已更新" : "没有拿到数据");
    g_uiPendingKind = UI_PEND_SEARCH;
    return;
  }

  /* 打开某个已下载的页面：先把文件内容塞回页面缓存，再走常规加载路径，
     startFetch 会命中缓存跳过联网（离线也能看）。 */
  if (g_dlPendingPath[0]) {
    char path[64];
    snprintf(path, sizeof(path), "%s", g_dlPendingPath);
    g_dlPendingPath[0] = '\0';
    String url;
    if (savedToCache(path, url)) {
      historyPush(url);
      setUrlText(url.c_str());
      startFetch(url);
    } else {
      toast("读取失败");
    }
    return;
  }

  /* 首次进入：显示我们自己画的搜索首页，不再直接抓门户页 */
  if (g_firstLoad && g_state == BROWSER_IDLE) {
    g_firstLoad = false;
    showSearchHome();
  }

  if (g_state == BROWSER_LOADING) {
    /* 更新进度 UI（读取后台任务写入的全局变量） */
    int pct = g_progressPct;
    if (g_loadProgressBar && pct > 0)
      lv_bar_set_value(g_loadProgressBar, pct, LV_ANIM_OFF);
    if (g_loadProgressText && g_progressStage[0] != '\0') {
      char buf[48];
      snprintf(buf, sizeof(buf), "%s %d%%", (const char*)g_progressStage, pct);
      lv_label_set_text(g_loadProgressText, buf);
    }
    if (g_progressBar && pct > 0)
      lv_bar_set_value(g_progressBar, pct, LV_ANIM_OFF);
    if (g_progressLabel && g_progressStage[0] != '\0') {
      char buf[48];
      snprintf(buf, sizeof(buf), "%s %d%%", (const char*)g_progressStage, pct);
      lv_label_set_text(g_progressLabel, buf);
    }

    /* 检查后台任务是否完成 */
    if (g_taskDone) {
      g_taskDone = false;
      /* 注意：常驻任务，不要把 g_fetchTask 置空 */

      if (g_taskResult == RENDER_SUCCESS && g_layoutRoot) {
        /* Phase 2: 渲染布局树到 LVGL 控件（快速，在 UI 任务中） */
        hideLoadingOverlay();
        /* 渲染时把整棵树从视口宽压缩到内容区宽 */
        uint32_t tRender = millis();
        RenderResult r = tactilebrowser_render_layout(g_layoutRoot, g_content, CONTENT_W, CONTENT_H);
        uint32_t renderMs = millis() - tRender;
        tactilebrowser_free_layout(g_layoutRoot);
        g_layoutRoot = nullptr;
        Serial.printf("[Browser] render_layout took %u ms\n", (unsigned)renderMs);

        if (r == RENDER_SUCCESS) {
          g_state = BROWSER_LOADED;
          if (g_status) lv_label_set_text(g_status, "已加载");
          /* 诊断：内容实际有多高（可视高 + 还能往下滚多少） */
          /* ⚠️ 必须先重算布局：刚创建完 widget 时 LVGL 还没更新坐标，
             直接读会拿到陈旧值（曾恒为 -334 / total=80，误导过一次判断，
             差点以为内容被压扁了）。 */
          if (g_content) lv_obj_update_layout(g_content);
          int scrollBottom = g_content ? lv_obj_get_scroll_bottom(g_content) : 0;
          Serial.printf("[Browser] render done. visible=%d scrollable=%d total=%d\n",
            CONTENT_H, scrollBottom, CONTENT_H + scrollBottom);
          Serial.printf("[Browser] render done. DRAM free: %u, PSRAM free: %u\n",
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
          /* 主任务栈水位预警（2026-09-23 崩溃复盘加的）：
             渲染是本工程最深的调用链，loopTask 一旦见底就是整机重启 +
             触摸全失效（RGB 由 DMA 自行刷新，画面还在，极具迷惑性）。
             这里持续报数，接近 0 就该警惕递归又失控了。 */
          {
            UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
            Serial.printf("[Browser] loopTask stack: %u B left (peak used %u B)\n",
                          (unsigned)(hwm * sizeof(StackType_t)),
                          (unsigned)(ARDUINO_LOOP_STACK_SIZE - hwm * sizeof(StackType_t)));
          }
          Serial.printf("[Browser] g_urlArea=%p valid=%d nav_browser=%p\n",
            g_urlArea, g_urlArea ? (int)lv_obj_is_valid(g_urlArea) : 0, nav_browser);
        } else {
          g_state = BROWSER_ERROR;
          if (g_status) lv_label_set_text(g_status, "渲染失败");
        }
      } else if (g_stopRequested) {
        /* 用户点了停止 */
        g_state = BROWSER_STOPPED;
        hideLoadingOverlay();
        if (g_status) lv_label_set_text(g_status, "已停止");
        if (g_content) {
          contentReset();
          lv_obj_t* msg = lv_label_create(g_content);
          lv_label_set_text(msg, "加载已停止");
          lv_obj_set_style_text_color(msg, lv_color_hex(0x888888), 0);
          lv_obj_set_style_text_font(msg, &font_zh_16, 0);
        }
      } else {
        /* 加载失败 */
        g_state = BROWSER_ERROR;
        hideLoadingOverlay();
        const char* errMsg = "未知错误";
        if (g_taskResult == RENDER_ERROR_NETWORK) errMsg = "网络错误";
        else if (g_taskResult == RENDER_ERROR_PARSE) errMsg = "解析失败";
        else if (g_taskResult == RENDER_ERROR_MEMORY) errMsg = "内存不足";
        if (g_status) lv_label_set_text(g_status, errMsg);
        if (g_content) {
          contentReset();
          lv_obj_t* msg = lv_label_create(g_content);
          lv_label_set_text(msg, errMsg);
          lv_obj_set_style_text_color(msg, lv_color_hex(0xFF6666), 0);
          lv_obj_set_style_text_font(msg, &font_zh_16, 0);
        }
      }

      /* 本次加载流程结束，恢复息屏 */
      ScreenSaver::setSuppressed(false);

      g_stopRequested = false;
      lv_bar_set_value(g_progressBar, 0, LV_ANIM_OFF);
      lv_label_set_text(g_progressLabel, "");

      /* 若加载期间用户又发起了请求（已在 startFetch 排队），此刻续上 */
      if (g_hasPending) {
        String next = g_pendingUrl;
        g_hasPending = false;
        g_pendingUrl = "";
        Serial.printf("[Browser] resuming queued url: %s\n", next.c_str());
        startFetch(next);
      }
    }
  }
}

/* ── 串口调试用：外部传入 URL 触发加载 ── */
void BrowserScreen_navigate(const char* url) {
  if (!url || strlen(url) == 0) return;
  historyPush(String(url));
  setUrlText(url);
  Serial.printf("[Browser] DRAM free: %u, PSRAM free: %u\n",
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  startFetch(String(url));
  Serial.printf("[Browser] navigate: %s\n", url);
}

/* ── 退出浏览器：释放网页内容与布局树 ──
 * 屏壳（顶栏/底栏等少量控件）保留，避免下次进入重建 + 悬空指针风险；
 * 大头是 lv_obj_clean 掉的数十个内容 widget 和 free 掉的布局树。 */
void BrowserScreen_close() {
  /* toast 的消失定时器持有 g_toast 指针，屏要拆了必须先注销，
     否则 1.8s 后回调里 lv_obj_is_valid(悬空指针) —— 这类定时器泄漏在本项目
     已经崩过好几次（clock 屏那只 lv_timer）。 */
  if (s_toastTimer) {
    lv_timer_del(s_toastTimer);
    s_toastTimer = nullptr;
  }
  toastHide();
  /* 若后台任务还在跑，让它尽快退出；常驻任务不会被删除 */
  g_stopRequested = true;
  g_hasPending = false;
  g_pendingUrl = "";

  /* ⚠️ 必须等后台任务真正停下来，才能动对象树。
     2026-09-24 实机 LoadProhibited：nav_back_home -> lv_obj_del 崩在
     lv_obj_get_screen（爬 parent 链）—— 链被写坏了。
     根因：fetch 任务跑在另一个任务里，却直接操作 LVGL 对象（显示/隐藏
     g_loadingOverlay、往内容容器里塞 widget）。主线程这边一旦 contentReset()
     / lv_obj_del() 把树拆掉，它手里就全是悬空指针，再写一次就把树写坏，
     崩在"下一次"遍历时 —— 所以栈看着像 clock 崩，clock 其实是无辜的。
     这里轮询等 g_state 自己离开 LOADING；上限 300 x 10ms = 3 秒，
     网络卡死也不会把主线程锁死（超时就打 WARN 按原行为继续）。 */
  int waited = 0;
  while (g_state == BROWSER_LOADING && waited < 300) {
    /* ⚠️ 以前这里只有 delay(10) —— 那是**自己把自己锁死**：
       能让 g_state 离开 LOADING 的 BrowserScreen_tick() 就跑在这个线程里，
       delay 期间它永远得不到执行，所以每次退出必然白等满 3 秒（UI 全程卡着）。
       跟 startFetch 那条注释里踩的是同一个坑。这里顺手把状态机推一下。 */
    BrowserScreen_tick();
    delay(10);
    waited++;
  }
  if (g_state == BROWSER_LOADING) {
    Serial.println("[Browser] WARN: fetch task still running, releasing anyway");
  } else if (waited) {
    Serial.printf("[Browser] fetch task stopped after %d ms\n", waited * 10);
  }
  /* 丢弃这次加载的残留状态：否则下次进浏览器会先"补处理"上一次的结果，
     弹出一条莫名其妙的"已更新"（实测：退出时任务刚跑完就会出现）。 */
  g_taskDone = false;
  g_fetchKind = FETCH_WEB;

  contentReset();
  /* 内容被清了，下次进来必须重画搜索首页 —— 否则是一块空白
     （tick 里只有 g_firstLoad 为真时才画，而它一生只为真一次）。 */
  g_firstLoad = true;

  if (g_layoutRoot) {
    tactilebrowser_free_layout(g_layoutRoot);
    g_layoutRoot = nullptr;
  }

  hideLoadingOverlay();
  g_state = BROWSER_IDLE;
  if (g_status) lv_label_set_text(g_status,
      WiFi.status() == WL_CONNECTED ? "就绪" : "无WiFi");

  Serial.printf("[Browser] closed. DRAM free: %u, PSRAM free: %u\n",
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

/* 后台任务是否还在跑。nav 拿它当释放守卫：任务没停就拆树 = 悬空指针。 */
bool BrowserScreen_isBusy() {
  return g_state == BROWSER_LOADING;
}

/* ── 启动早期调用：一次性建好常驻后台任务 ── */
void BrowserScreen_preinit() {
  ensureFetchTask();
}

void BrowserScreen_setViewport(int width) {
  if (width != 0) {            /* 0 = 自动（meta viewport） */
    if (width < 320) width = 320;
    if (width > 2048) width = 2048;
  }
  g_browserViewportW = width;
  Serial.printf("[Browser] viewport=%d%s, content %dpx\n",
                g_browserViewportW, width == 0 ? " (auto)" : "", CONTENT_W);
}

int BrowserScreen_getViewport() {
  return g_browserViewportW;
}

/* 串口入口：控制台点不了触屏，给自动化一个触发热点加载的口子。
   ⚠️ 必须在匿名 namespace 之后：startNews 是内部链接，只能在本文件后面引用。 */
void BrowserScreen_news(const char* platform) { startNews(platform); }

/* 串口验证入口：把拼音塞进搜索框，触发候选条重建（控制台点不了触屏）。
   用法：先 nav browser，再 ime nihao */
void BrowserScreen_ime(const char* py) {
  if (!g_searchTa || !lv_obj_is_valid(g_searchTa)) {
    Serial.println("[IME] no search box (browser home not shown?)");
    return;
  }
  lv_textarea_set_text(g_searchTa, py);   /* 触发 VALUE_CHANGED -> ime_update */
  const char* han = ime_lookup(py);
  Serial.printf("[IME] py=%s -> %s\n", py, han ? han : "(no candidate)");
}