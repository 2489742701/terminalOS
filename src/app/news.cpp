#include "news.h"

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include "font_zh.h"
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * 实现说明（2026-09-24）
 *
 * 为什么不复用 browser 的 arduino_download_html：那边会把整页灌进页面缓存、
 * 还顺带做 302 追踪，而这里只要 6~19KB 的 JSON。自己写一遍 GET 反而更省事，
 * 也免得新闻请求污染网页缓存（只有 2 槽）。
 *
 * 两条硬约束（都是这个项目踩出来的）：
 *   1. 读 chunked 时必须钳 room/want —— 早期写穿过 PSRAM 堆，表现为 WiFi 驱动
 *      报 block_trim_free。
 *   2. 响应缓冲放 PSRAM（64KB），内部 DRAM 要留给浏览器。
 * ═══════════════════════════════════════════════════════════════════════════ */

#define NEWS_HOST "news.orz.ai"
#define NEWS_BUF_BYTES 65536

const NewsPlatform kNewsPlatforms[] = {
    {"baidu", "百度"},   {"weibo", "微博"},   {"zhihu", "知乎"},
    {"36kr", "36氪"},    {"bilibili", "B站"}, {"juejin", "掘金"},
    {"github", "GitHub"}, {"hackernews", "HN"}, {"douban", "豆瓣"},
};
const int kNewsPlatformCount =
    (int)(sizeof(kNewsPlatforms) / sizeof(kNewsPlatforms[0]));

namespace {

/* 读一个 JSON 字符串（到未转义的 '"' 为止），做最小反转义。
   ⚠️ 两点：
     · \uXXXX 直接丢掉（转 UTF-8 要码点表，不值得；丢的多半是 & 之类符号）。
     · 截断必须落在 UTF-8 字符边界上，否则最后一个汉字变半个 -> LVGL 画豆腐块。 */
int copyEscaped(const char* s, char* out, int cap) {
  int i = 0, o = 0;
  while (s[i] && o < cap - 1) {
    char c = s[i];
    if (c == '"') break;
    if (c == '\\') {
      char n = s[i + 1];
      if (n == 'n' || n == 't' || n == 'r' || n == 'b' || n == 'f') {
        out[o++] = ' '; i += 2; continue;
      }
      if (n == 'u') {           // \uXXXX：跳过 6 个字符
        i += 2;
        int k = 0;
        while (s[i] && k < 4 && ((s[i] >= '0' && s[i] <= '9') ||
                                 (s[i] >= 'a' && s[i] <= 'f') ||
                                 (s[i] >= 'A' && s[i] <= 'F'))) { i++; k++; }
        continue;
      }
      if (!n) break;
      out[o++] = n; i += 2; continue;   // \" \\ \/ 等：取后一个字符
    }
    out[o++] = c; i++;
  }
  /* 回退到字符边界。⚠️ 两个坑都踩过，别只修一半：
     2026-09-24 之一：无条件回退会把末尾**完整**汉字的续字节也删掉
       —— 每条标题都少最后一个字（串口实测）。所以只在真截断时回退。
     2026-09-25 之二：只回退续字节**不够** —— 剩下那个孤立的首字节
       （如 0xE8）照样是半个字，LVGL 画出来就是乱码字符。实测新闻标题
       末尾全是"…亚运六<?>"，就是这个。正确做法是判断最后一个字是否完整，
       不完整就**整个字**丢掉（首字节一起）。 */
  bool hitCap = (o >= cap - 1) && s[i] && s[i] != '"';
  if (hitCap) {
    int start = o - 1;
    while (start > 0 && ((unsigned char)out[start] & 0xC0) == 0x80) start--;
    unsigned char lead = (unsigned char)out[start];
    int need = 0;
    if ((lead & 0xE0) == 0xC0) need = 2;
    else if ((lead & 0xF0) == 0xE0) need = 3;
    else if ((lead & 0xF8) == 0xF0) need = 4;
    if (need && (o - start) < need) o = start;   /* 半个字 -> 整字丢掉 */
  }
  out[o] = '\0';
  return i;   // 返回源串消耗长度，调用方据此继续往后找
}

/* ── 缺字过滤（消灭"方格子"）──────────────────────────────────────────────
 * 新闻标题的字是任意的，我们的 font_zh_16 只有 3927 个常用字 ——
 * 超出的会画成方框（豆腐块）。这里在**入库前**把字库里没有的字符换成空格，
 * 屏幕上就再也看不到方框了。
 * 用 lv_font_get_glyph_dsc() 探测：返回 false = 这个字体没有这个码位。
 * ⚠️ 就地写：多字节 -> 1 字节空格，只会变短，写前面的下标是安全的。 */
static uint32_t utf8Next(const char* s, int& i) {
  unsigned char c = (unsigned char)s[i];
  if (c < 0x80) { i++; return c; }
  uint32_t cp;
  int need;
  if ((c & 0xE0) == 0xC0)      { need = 1; cp = (uint32_t)(c & 0x1F); }
  else if ((c & 0xF0) == 0xE0) { need = 2; cp = (uint32_t)(c & 0x0F); }
  else if ((c & 0xF8) == 0xF0) { need = 3; cp = (uint32_t)(c & 0x07); }
  else { i++; return 0; }                 /* 非法字节 */
  i++;
  for (int k = 0; k < need; k++) {
    if (((unsigned char)s[i] & 0xC0) != 0x80) break;   /* 序列被截断 */
    cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
    i++;
  }
  return cp;
}

static void stripMissingGlyphs(char* s) {
  if (!s) return;
  lv_font_glyph_dsc_t dsc;
  int i = 0, w = 0;
  while (s[i]) {
    int start = i;
    uint32_t cp = utf8Next(s, i);
    if (cp == 0) { i = start + 1; continue; }          /* 丢掉非法字节 */
    if (cp >= 0x20 && !lv_font_get_glyph_dsc(&font_zh_16, &dsc, cp, 0)) {
      s[w++] = ' ';                                    /* 字库没有 -> 空格 */
      continue;
    }
    for (int k = start; k < i; k++) s[w++] = s[k];     /* 原样保留 */
  }
  s[w] = '\0';
}

/* chunked 传输编码就地解块（解完长度只会变短，安全） */
size_t dechunk(char* s, size_t n) {
  char* d = s;
  char* p = s;
  char* end = s + n;
  while (p < end) {
    char* e = p;
    while (e < end - 1 && !(e[0] == '\r' && e[1] == '\n')) e++;
    if (e >= end - 1) break;
    long sz = strtol(p, NULL, 16);
    if (sz <= 0) break;
    p = e + 2;
    if (p + sz > end) sz = end - p;
    memmove(d, p, (size_t)sz);
    d += sz;
    p += sz + 2;   // 数据后面还有 CRLF
  }
  return (size_t)(d - s);
}

bool httpGet(const char* path, char* buf, size_t cap, size_t& outLen) {
  WiFiClientSecure cli;
  cli.setInsecure();
  cli.setTimeout(12);
  if (!cli.connect(NEWS_HOST, 443)) {
    Serial.println("[News] connect failed");
    return false;
  }
  cli.printf(
      "GET %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      /* ⚠️ UA 不能为空：实测空 UA 会被 403（有 UA 校验）。
         用跟浏览器一样的 KitKat，老机器 UA 一般不会被反爬盯上。 */
      "User-Agent: Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) "
      "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile "
      "Safari/537.36\r\n"
      "Accept: application/json\r\n"
      "Connection: close\r\n\r\n",
      path, NEWS_HOST);

  size_t n = 0;
  uint32_t t0 = millis();
  while (n < cap - 1 && millis() - t0 < 15000) {
    int avail = cli.available();
    if (avail > 0) {
      /* ⚠️ 钳 want：不钳会写穿缓冲（项目里写穿过 PSRAM 堆）。 */
      size_t room = cap - 1 - n;
      int want = (avail < (int)room) ? avail : (int)room;
      int got = cli.read((uint8_t*)buf + n, (size_t)want);
      if (got <= 0) break;
      n += (size_t)got;
    } else if (!cli.connected()) {
      break;
    } else {
      delay(5);
    }
  }
  buf[n] = '\0';
  outLen = n;
  cli.stop();
  return n > 0;
}

int parseJson(const char* body, NewsItem* out, int max) {
  int n = 0;
  const char* p = body;
  while (*p && n < max) {
    const char* t = strstr(p, "\"title\":\"");
    if (!t) break;
    t += 9;
    char title[NEWS_TITLE_LEN];
    int adv = copyEscaped(t, title, sizeof(title));
    if (adv <= 0) break;

    const char* u = strstr(t + adv, "\"url\":\"");
    if (!u) break;
    u += 7;
    char url[NEWS_URL_LEN];
    int adv2 = copyEscaped(u, url, sizeof(url));
    if (adv2 <= 0) break;

    if (title[0] && url[0]) {
      stripMissingGlyphs(title);
      snprintf(out[n].title, NEWS_TITLE_LEN, "%s", title);
      snprintf(out[n].url, NEWS_URL_LEN, "%s", url);
      n++;
    }
    p = u + adv2;
  }
  return n;
}

}  // namespace

int news_fetch(const char* platform, NewsItem* out, int max) {
  if (!platform || !out || max <= 0) return -1;

  char* buf = (char*)heap_caps_malloc(NEWS_BUF_BYTES, MALLOC_CAP_SPIRAM);
  if (!buf) {
    Serial.println("[News] PSRAM alloc failed");
    return -1;
  }

  char path[64];
  snprintf(path, sizeof(path), "/api/v1/dailynews/?platform=%s", platform);

  size_t len = 0;
  if (!httpGet(path, buf, NEWS_BUF_BYTES, len)) {
    heap_caps_free(buf);
    return -1;
  }
  Serial.printf("[News] %s: %u B\n", platform, (unsigned)len);

  /* 响应头里找状态行 + 是否 chunked */
  char* body = strstr(buf, "\r\n\r\n");
  size_t headLen = body ? (size_t)(body - buf) : 0;
  if (!body || headLen > len) {
    heap_caps_free(buf);
    return -2;
  }
  body += 4;
  size_t bodyLen = len - (headLen + 4);

  if (strstr(buf, " 200 ") == NULL && strstr(buf, "HTTP/1.1 200") == NULL) {
    Serial.printf("[News] non-200: %.40s\n", buf);
    heap_caps_free(buf);
    return -2;
  }

  /* 头部判断 chunked（只扫头部那一小段，别扫整个 body） */
  buf[headLen] = '\0';
  bool chunked = (strstr(buf, "chunked") != NULL) ||
                 (strstr(buf, "Chunked") != NULL);
  buf[headLen] = '\r';

  if (chunked) bodyLen = dechunk(body, bodyLen);

  int n = parseJson(body, out, max);
  heap_caps_free(buf);
  Serial.printf("[News] parsed %d items\n", n);
  return n;
}
