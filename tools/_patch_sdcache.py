# -*- coding: utf-8 -*-
"""页面缓存到 SD 卡（2026-09-25）"""
import io, sys

ROOT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
P = ROOT + r"\src\app\browser_screen.cpp"
s = io.open(P, encoding="utf-8", newline="").read()


def sub1(old, new, tag):
    global s
    if old not in s:
        print("MISS: " + tag); sys.exit(1)
    if s.count(old) != 1:
        print("DUP(%d): %s" % (s.count(old), tag)); sys.exit(1)
    s = s.replace(old, new, 1)
    print("OK: " + tag)


# ── 1. include ──
sub1('''#include <FS.h>
#include <LittleFS.h>''',
'''#include <FS.h>
#include <LittleFS.h>
#include "sd_card.h"''', "1 include")

# ── 2. SD 页面缓存实现（插在 cache_download_html 之前）──
sub1('''/* 下载器包装：真下载完之后顺手塞一份进缓存。
   引擎拿到 buffer 后会自己 free，所以这里必须拷一份。 */
static RenderResult cache_download_html(const char* url, MemoryBuffer* buffer) {
  RenderResult r = arduino_download_html(url, buffer);
  if (r == RENDER_SUCCESS && buffer && buffer->data && buffer->size > 0)
    pageCachePut(String(url), (const uint8_t*)buffer->data, buffer->size);
  return r;
}''',
'''/* ── SD 卡页面缓存（master 2026-09-25：「然后缓存到sd卡」）──────────────
 * PSRAM 那份只留 2 页（当前页 + 上一页），退出浏览器就没了；SD 这份是
 * **持久化**的：同一个 URL 下次再打开（哪怕重启过）直接从卡里读回 HTML，
 * 不联网、不走 TLS。
 *
 * ⚠️ 只在 SD **已经挂载**时生效。挂载要占用与 LCD 共用的那条 SPI，
 *    绝不在浏览过程中偷偷 mount —— 串口 `sd` 命令才挂载。没挂载一律静默跳过。
 * 文件格式与 LittleFS 那份一致：`<!--URL:<url> TS:<millis>-->\\n<html>`
 *    —— 文件名只有 hash，靠头部这行才能在离线时把 URL 认回来。 */
static const uint32_t SD_PAGE_TTL_MS = 30 * 60 * 1000UL;

static void sdPagePath(const String& url, char* out, size_t n) {
  snprintf(out, n, "/gt/pages/p%08x.html", (unsigned)url_hash(url));
}

static bool sdPageSave(const String& url, const uint8_t* data, size_t len) {
  if (!SDCard::mounted() || !data || len == 0) return false;
  if (len > PAGE_CACHE_MAX_ENTRY) return false;   /* 超大页不占卡 */
  char path[48];
  sdPagePath(url, path, sizeof(path));
  /* 头部和正文拼成一块再写：只写一次，不会出现"只有头没有正文"的半截文件。
     ⚠️ concat(cstr, len) 而不是 String(cstr) —— 后者遇到 NUL 会截断 HTML。 */
  String out;
  out.reserve(len + url.length() + 64);
  out += "<!--URL:";
  out += url;
  out += " TS:";
  out += String((unsigned long)millis());
  out += "-->\\n";
  out.concat((const char*)data, (unsigned int)len);
  bool ok = SDCard::writeFile(path, out);
  Serial.printf("[Browser] SD cache %s: %s (%u B)\\n", ok ? "put" : "FAIL",
                path, (unsigned)len);
  return ok;
}

/* 命中且未过期返回 true，HTML 正文放 out。hash 撞了 / 过期 / 没挂载都返回 false。 */
static bool sdPageLoad(const String& url, String& out) {
  if (!SDCard::mounted()) return false;
  char path[48];
  sdPagePath(url, path, sizeof(path));
  String raw;
  if (!SDCard::readFile(path, raw)) return false;
  if (!raw.startsWith("<!--URL:")) return false;
  int e = raw.indexOf("-->");
  if (e < 0) return false;
  String head = raw.substring(8, e);
  int tp = head.lastIndexOf(" TS:");
  if (tp < 0) return false;
  if (head.substring(0, tp) != url) return false;      /* hash 碰撞 */
  unsigned long ts = strtoul(head.substring(tp + 4).c_str(), nullptr, 10);
  /* millis() 会回绕，无符号减法天然正确 */
  if (ts && (unsigned long)(millis() - ts) > SD_PAGE_TTL_MS) {
    Serial.printf("[Browser] SD cache expired: %s\\n", url.c_str());
    return false;
  }
  out = raw.substring(e + 3);
  while (out.length() && (out[0] == '\\n' || out[0] == '\\r')) out.remove(0, 1);
  return out.length() > 0;
}

/* 下载器包装：真下载完之后顺手塞一份进缓存。
   引擎拿到 buffer 后会自己 free，所以这里必须拷一份。 */
static RenderResult cache_download_html(const char* url, MemoryBuffer* buffer) {
  RenderResult r = arduino_download_html(url, buffer);
  if (r == RENDER_SUCCESS && buffer && buffer->data && buffer->size > 0) {
    pageCachePut(String(url), (const uint8_t*)buffer->data, buffer->size);
    sdPageSave(String(url), (const uint8_t*)buffer->data, buffer->size);
  }
  return r;
}''', "2 sdcache impl")

# ── 3. fetch_task：PSRAM 没命中就退到 SD 卡 ──
sub1('''    int ci = pageCacheFind(url);
    if (ci >= 0) {''',
'''    int ci = pageCacheFind(url);
    if (ci < 0) {
      /* PSRAM 里没有 → 退到 SD 卡那份（跨会话/重启仍然有效）。
         读回来塞进 PSRAM 缓存，下面走的就是同一条"缓存命中"路径。 */
      String html;
      if (sdPageLoad(url, html)) {
        pageCachePut(url, (const uint8_t*)html.c_str(), html.length());
        ci = pageCacheFind(url);
        if (ci >= 0)
          Serial.printf("[Browser] SD cache hit: %s (%u B)\\n", url.c_str(),
                        (unsigned)g_pageCache[ci].len);
      }
    }
    if (ci >= 0) {''', "3 fetch sd fallback")

# ── 4. downloadCurrentPage：优先写 SD 卡 ──
sub1('''static void downloadCurrentPage() {
  if (!g_currentUrl.length()) { toast("还没有页面"); return; }
  int ci = pageCacheFind(g_currentUrl);
  if (ci < 0) { toast("页面已释放，请刷新后再下载"); return; }

  if (!LittleFS.begin(false)) {''',
'''static void downloadCurrentPage() {
  if (!g_currentUrl.length()) { toast("还没有页面"); return; }
  int ci = pageCacheFind(g_currentUrl);
  if (ci < 0) { toast("页面已释放，请刷新后再下载"); return; }

  /* SD 卡优先：容量比片内 LittleFS 大得多，插了卡就没必要占 Flash。
     没插卡（未挂载）就退回原来的 LittleFS 路径。 */
  if (SDCard::mounted()) {
    if (sdPageSave(g_currentUrl, g_pageCache[ci].data, g_pageCache[ci].len)) {
      char msg[80];
      snprintf(msg, sizeof(msg), "已存到SD卡 %u KB",
               (unsigned)(g_pageCache[ci].len / 1024));
      toast(msg);
    } else {
      toast("写SD卡失败");
    }
    return;
  }

  if (!LittleFS.begin(false)) {''', "4 download sd first")

io.open(P, "w", encoding="utf-8", newline="").write(s)
print("WROTE", len(s))
