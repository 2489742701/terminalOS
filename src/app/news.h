#pragma once

/* ═══════════════════════════════════════════════════════════════════════════
 * 每日热点新闻（news.orz.ai）
 *
 * GET https://news.orz.ai/api/v1/dailynews/?platform=<code>
 *   -> {"status":"200","data":[{"title":..,"url":..,"score":..,"desc":..}, ..]}
 *
 * 2026-09-24 实测（PC 端）：
 *   · 必须带 UA：空 UA 直接 403，任何非空 UA 都能过（设备用的 KitKat 也能过）。
 *   · 单平台 6~19 KB，20~52 条，半小时刷新一次 —— 对 786KB 下载上限毫无压力。
 *   · sspai 当时返回 0 条（平台侧空），所以列表可能为空，UI 要能显示"暂无"。
 *
 * ⚠️ news_fetch() 是**阻塞**的（含 TLS 握手 + 读取），只能在后台任务里调用，
 *    别在 UI 线程直接调 —— LVGL 会僵住几秒。
 * ═══════════════════════════════════════════════════════════════════════════ */

#define NEWS_MAX_ITEMS 12
#define NEWS_TITLE_LEN 72
#define NEWS_URL_LEN 256

struct NewsItem {
  char title[NEWS_TITLE_LEN];
  char url[NEWS_URL_LEN];
};

/* 平台代码 + 中文名 + 是否已验证。
   UI 按这个顺序画 chip —— **能正常打开看内容的排前面**（master 2026-09-25）。
   verified=true 的源，名字旁边画一颗金色小星星：
     选中态（白底黑字）时星星**仍然是金色**，只有文字变黑。
   ⚠️ verified 不是"猜"的，是逐条点开前 4 条链接实测出来的，改之前先测。 */
struct NewsPlatform {
  const char* code;
  const char* name;
  bool verified;
};
extern const NewsPlatform kNewsPlatforms[];
extern const int kNewsPlatformCount;

/* 阻塞拉取某个平台的热点。返回条数；0 = 空，-1 = 网络/内存失败，-2 = 解析失败。 */
int news_fetch(const char* platform, NewsItem* out, int max);
