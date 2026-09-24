# -*- coding: utf-8 -*-
"""热点新闻：多源缓存（PSRAM）+ 30 分钟节流 + 进页面自动拉 + 底部"更新"按钮。

master 原话：
  · "热点新闻排序把 360 放第一位" → 360 不是新闻源（API 0 条），排序按能打开排
  · "进入程序之后自动刷新出一次，然后每天会自动刷新"
  · "各个新闻源应该再记录一下，半小时内更新，或者在底部留一个更新按钮，
     点击之后才会主动更新这个新闻源"
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"
src = io.open(P, encoding="utf-8").read()

# ── 1) 缓存结构 + 全局 ────────────────────────────────────────────────────
old = """static NewsItem g_news[NEWS_MAX_ITEMS];
static int g_newsCount = 0;
static char g_newsPlatform[16] = "baidu";"""
new = """static NewsItem g_news[NEWS_MAX_ITEMS];
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
  Serial.printf("[Browser] news cache: %s (%u B in PSRAM)\\n",
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
  Serial.printf("[Browser] news cache hit %s (%d items, age %us)\\n",
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
static uint32_t g_newsAutoDay = 0;"""
assert src.count(old) == 1
src = src.replace(old, new, 1)

# ── 2) startNews：命中新鲜缓存就不联网 ────────────────────────────────────
old2 = """  snprintf(g_newsPlatform, sizeof(g_newsPlatform), "%s", platform);
  /* ⚠️ 必须清停止标志"""
new2 = """  snprintf(g_newsPlatform, sizeof(g_newsPlatform), "%s", platform);

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

  /* ⚠️ 必须清停止标志"""
assert src.count(old2) == 1
src = src.replace(old2, new2, 1)

old3 = """static void startNews(const char* platform) {
  if (g_state == BROWSER_LOADING) { toast("正在加载，请稍等"); return; }"""
new3 = """static void startNews(const char* platform, bool force = false) {
  if (g_state == BROWSER_LOADING) { toast("正在加载，请稍等"); return; }"""
assert src.count(old3) == 1
src = src.replace(old3, new3, 1)

# ── 3) 拉完写回缓存 ───────────────────────────────────────────────────────
old4 = """      g_newsCount = news_fetch(g_newsPlatform, g_news, NEWS_MAX_ITEMS);
      Serial.printf("[Browser] news %s -> %d items, DRAM free=%u\\n",
                    g_newsPlatform, g_newsCount,
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));"""
new4 = """      g_newsCount = news_fetch(g_newsPlatform, g_news, NEWS_MAX_ITEMS);
      /* 拉完立刻写回该源的缓存槽，下次 30 分钟内就不用联网了 */
      if (g_newsCount > 0) newsSaveToCache(g_newsPlatform);
      Serial.printf("[Browser] news %s -> %d items, DRAM free=%u\\n",
                    g_newsPlatform, g_newsCount,
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));"""
assert src.count(old4) == 1
src = src.replace(old4, new4, 1)

io.open(P, "w", encoding="utf-8", newline="").write(src)
print("news cache ok")
