#include "lvgl_renderer.h"
#include <stdlib.h>
#include <string.h>
#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * mbedTLS PSRAM 重定向（参考 babe32-browser）
 * mbedTLS 默认从内部 DRAM 分配 ~40KB SSL 缓冲，ESP32-S3 内部 DRAM 碎片化后分配失败。
 * 把 mbedTLS 的 calloc 重定向到 PSRAM（8MB 充裕），失败再回退 DRAM。
 */
static bool s_tls_psram_inited = false;

/* 记录上次 HTML 下载是否被截断（Content-Length 超过 256KB 上限） */
static bool s_html_truncated = false;

/* 进度回调（由 browser_screen 设置，下载过程中定期调用更新进度条） */
static HtmlProgressCallback s_progressCb = nullptr;
void arduino_set_progress_callback(HtmlProgressCallback cb) { s_progressCb = cb; }

/* 协作式停止标志：外部设置为 true 后，下载函数尽快退出 */
static volatile bool *s_stopFlag = nullptr;
void arduino_set_stop_flag(volatile bool *flag) { s_stopFlag = flag; }

bool arduino_html_was_truncated() { return s_html_truncated; }
static void *tls_psram_calloc(size_t n, size_t size) {
    void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_8BIT);
    return p;
}
static void tls_psram_free(void *ptr) { free(ptr); }
static void init_tls_psram() {
    if (s_tls_psram_inited) return;
    mbedtls_platform_set_calloc_free(tls_psram_calloc, tls_psram_free);
    s_tls_psram_inited = true;
    Serial.println("[Browser] mbedTLS PSRAM redirect enabled");
}

/* 重定向递归深度（3xx 跟随） */
static int s_redirectDepth = 0;
#define MAX_REDIRECTS 5

/* 把 Location 头（可能是相对路径）解析成绝对 URL。
   2026-09-23：www.baidu.com 用 http:// 访问会返回 302 → https://，
   之前不跟随 3xx，直接 RENDER_ERROR_NETWORK，现象是"百度网络错误"。 */
static String resolve_redirect(const String &base, const String &loc) {
  if (loc.startsWith("http://") || loc.startsWith("https://"))
    return loc;

  int protoEnd = base.indexOf("://");
  if (protoEnd < 0)
    return String();
  String scheme = base.substring(0, protoEnd + 3);   /* 含 "://" */
  String rest = base.substring(protoEnd + 3);
  int slash = rest.indexOf('/');
  String host = slash < 0 ? rest : rest.substring(0, slash);

  if (loc.startsWith("/"))
    return scheme + host + loc;

  /* 相对路径：拼到当前 URL 的目录后面 */
  String dir = "/";
  if (slash >= 0) {
    int lastSlash = rest.lastIndexOf('/');
    if (lastSlash >= 0)
      dir = rest.substring(0, lastSlash + 1);
  }
  if (!dir.endsWith("/"))
    dir += "/";
  return scheme + host + dir + loc;
}

/**
 * arduino_download_html - HTTP/HTTPS GET 请求
 *
 * HTTPS 方案（参考 babe32-browser）：
 *   - mbedTLS SSL 缓冲重定向到 PSRAM（40KB → 8MB PSRAM）
 *   - WiFiClientSecure + setInsecure()（跳过证书验证）
 *   - �B实浏览器 UA，避免被百度等网站返回 400
 * 
 * 限制：最大下载 256KB（PSRAM）；支持 Content-Length 和 chunked 两种响应模式
 */
RenderResult arduino_download_html(const char *url, MemoryBuffer *buffer) {
  if (!url || !buffer)
    return RENDER_ERROR_UNKNOWN;

  Serial.printf("[Browser] GET %s\n", url);

  /* --- 阶段 1: 解析 URL，拆分出 host / port / path --- */
  String urlStr(url);
  bool isHttps = urlStr.startsWith("https://");

  int protoEnd = urlStr.indexOf("://");
  if (protoEnd < 0) return RENDER_ERROR_UNKNOWN;
  String rest = urlStr.substring(protoEnd + 3);
  int slashPos = rest.indexOf('/');
  String host = slashPos < 0 ? rest : rest.substring(0, slashPos);
  String path = slashPos < 0 ? "/" : rest.substring(slashPos);

  int port = isHttps ? 443 : 80;
  int colonPos = host.indexOf(':');
  if (colonPos >= 0) {
    port = host.substring(colonPos + 1).toInt();
    host = host.substring(0, colonPos);
  }

  Serial.printf("[Browser] host=%s port=%d path=%s https=%d\n", host.c_str(), port, path.c_str(), isHttps);

  /* --- 阶段 2: TCP/TLS 连接 --- */
  WiFiClient *client = nullptr;
  WiFiClient tcpClient;
  WiFiClientSecure sslClient;

  if (isHttps) {
    init_tls_psram();
    sslClient.setInsecure();
    sslClient.setTimeout(15);
    if (!sslClient.connect(host.c_str(), port)) {
      Serial.println("[Browser] HTTPS connect failed");
      return RENDER_ERROR_NETWORK;
    }
    client = &sslClient;
  } else {
    tcpClient.setTimeout(10);
    if (!tcpClient.connect(host.c_str(), port)) {
      Serial.println("[Browser] HTTP connect failed");
      return RENDER_ERROR_NETWORK;
    }
    client = &tcpClient;
  }

  /* --- 阶段 3: 发送 HTTP 请求 --- */
  /* ── 按域名选 UA ──
     默认 KitKat（Android 4.4 / Chrome 30 移动版）：对老机器宽容，百度不会
     302 到 wappass 图形验证码；页面也小。
     ⚠️ 但必应必须换**桌面 Chrome 120**。2026-09-23 在 cn.bing.com 实测：
         移动 UA（KitKat / Android13 / iPhone）→ 只给 5 条 li.b_algo，
         HTML 里 0 个分页标记，first= 参数被完全忽略；
         桌面 UA                              → 9~10 条结果 + 「下一页」链接。
      体积从 60KB 涨到 ~100KB，但平铺模式本来就跳过外部 CSS，不会变成
      几十次 TLS。SERP 壳子（时间筛选 / 数字页码 / 顶部导航）交给
      layout_engine.cpp 的 flat_is_serp_chrome_link() 过滤。
     另注：www.bing.com 会被地域 302 到 cn.bing.com，所以匹配 bing.com
     两个都覆盖。 */
  String ua;
  if (host.indexOf("bing.com") >= 0) {
    ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
  } else {
    ua = "Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36";
  }
  String req = "GET " + path + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               /* UA 伪装成 Android 4.4 KitKat（Chrome 30 移动版）。
                  为什么不是桌面 Chrome 120：
                    - 桌面 UA 会拿到 700KB+ 的 PC 版首页（208 个节点，解析峰值 DRAM 吃紧）；
                    - 移动 UA 直接进 m.* 的轻量页，几十 KB，本机的解析/渲染扛得住；
                    - 反爬策略对"老机器"更宽容：KitKat 这种 2013 年的 UA 不会触发
                      wappass 的图形验证码（桌面 Chrome 120 + mbedTLS 指纹 = 必被拦）。
                  详见 docs/12 §5。 */
               "User-Agent: " + ua + "\r\n" +
               "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n" +
               "Accept-Language: zh-CN,zh;q=0.9\r\n" +
               "Connection: close\r\n\r\n";
  client->print(req);

  /* --- 阶段 3: 读取响应头，解析状态码 / Content-Length / chunked --- */
  String line;
  int status = 0;
  int contentLen = 0;
  bool chunked = false;
  String redirectTo = "";

  while (client->connected() || client->available()) {
    line = client->readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;  /* 空行 = 头结束 */
    if (status == 0 && line.startsWith("HTTP/")) {
      status = line.substring(9, 12).toInt();  /* "HTTP/1.1 200 OK" → 200 */
    } else if (line.startsWith("Content-Length:")) {
      contentLen = line.substring(15).toInt();
    } else if (line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
      chunked = true;
    } else if (line.length() > 9 && strncasecmp(line.c_str(), "location:", 9) == 0) {
      redirectTo = line.substring(9);
      redirectTo.trim();
    }
  }

  Serial.printf("[Browser] status=%d len=%d chunked=%d\n", status, contentLen, chunked);

  /* 跟随 3xx 重定向（301/302/303/307/308）。
     注意：此时还没分配 buffer->data，可以安全地关掉连接重来。 */
  if (status >= 300 && status <= 399 && redirectTo.length() > 0) {
    client->stop();
    if (s_redirectDepth >= MAX_REDIRECTS) {
      Serial.println("[Browser] too many redirects");
      return RENDER_ERROR_NETWORK;
    }
    String nextUrl = resolve_redirect(urlStr, redirectTo);
    if (nextUrl.length() == 0) {
      Serial.println("[Browser] bad Location header");
      return RENDER_ERROR_NETWORK;
    }
    Serial.printf("[Browser] redirect %d -> %s\n", status, nextUrl.c_str());
    s_redirectDepth++;
    RenderResult rr = arduino_download_html(nextUrl.c_str(), buffer);
    s_redirectDepth--;
    return rr;
  }

  if (status != 200) {
    client->stop();
    return RENDER_ERROR_NETWORK;
  }

  /* 限制最大下载 768KB。百度首页 730KB，前 256KB 全是 <head>，
     <body> 标签在 256KB 之后，需要完整下载才能找到 body。
     PSRAM 有 8MB，768KB 完全没问题。Lexbor 内存已重定向到 PSRAM，
     DOM 节点不争抢 DRAM。下载时每 4KB vTaskDelay(1) 让 WiFi 喘气。 */
  s_html_truncated = (contentLen > 786432);
  if (contentLen <= 0 || contentLen > 786432) contentLen = 786432;

  buffer->data = (char *)heap_caps_malloc(contentLen + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buffer->data) {
    client->stop();
    return RENDER_ERROR_MEMORY;
  }

  /* --- 阶段 4: 读取响应体（分块读取，每 4KB 让 CPU 喘口气） --- */
  int total = 0;
  int lastReport = 0;
  if (chunked) {
    /* chunked 传输：每块前一行十六进制长度，空行结束 */
    while (client->connected() || client->available()) {
      /* 协作式停止检查 */
      if (s_stopFlag && *s_stopFlag) {
        client->stop();
        heap_caps_free(buffer->data);
        buffer->data = NULL;
        Serial.println("[Browser] download stopped by user");
        return RENDER_ERROR_UNKNOWN;
      }
      String sizeLine = client->readStringUntil('\n');
      sizeLine.trim();
      int chunkSize = strtol(sizeLine.c_str(), NULL, 16);
      if (chunkSize <= 0) break;

      /* ⚠️ 必须有上限！buffer 只分配了 contentLen+1 字节，而 chunked 响应
         没有 Content-Length（contentLen 被兜底成 786432）。
         以前这里直接按 chunkSize 往 buffer->data + total 里写，从不检查越界
         —— 页面超过 768KB 就写穿 PSRAM 堆。踩坏的堆不会立刻报错，
         而是等 WiFi 驱动分配 rx buffer 时才 assert：
         "block_trim_free heap_tlsf.c:371 (block must be free)"。
         （m.baidu.com/s?word=... 这种搜索结果页就会触发。） */
      int room = contentLen - total;
      if (room <= 0) {
        Serial.printf("[Browser] download cap reached (%d bytes)\n", contentLen);
        s_html_truncated = true;
        break;
      }
      int want = chunkSize;
      if (want > room) {
        want = room;
        s_html_truncated = true;
      }

      int got = 0;
      while (got < want && (client->connected() || client->available())) {
        int r = client->read((uint8_t*)(buffer->data + total), want - got);
        if (r > 0) { got += r; total += r; }
        else delay(1);
        /* 每 4KB 报告进度 + 让 Core1 WiFi 任务喘气 */
        if (total - lastReport >= 4096) {
          lastReport = total;
          if (s_progressCb) s_progressCb(total, contentLen, "下载中");
          vTaskDelay(1);
        }
      }
      /* 本块没读完（撞了上限）→ 结束，剩下的字节不再消费（马上要 stop） */
      if (got < chunkSize) {
        Serial.printf("[Browser] download cap hit mid-chunk (%d/%d)\n", got, chunkSize);
        s_html_truncated = true;
        break;
      }
      client->readStringUntil('\n');
    }
  } else {
    /* 普通传输：按 Content-Length 读取 */
    while (total < contentLen && (client->connected() || client->available())) {
      /* 协作式停止检查 */
      if (s_stopFlag && *s_stopFlag) {
        client->stop();
        heap_caps_free(buffer->data);
        buffer->data = NULL;
        Serial.println("[Browser] download stopped by user");
        return RENDER_ERROR_UNKNOWN;
      }
      int r = client->read((uint8_t*)(buffer->data + total), contentLen - total);
      if (r > 0) total += r;
      else delay(1);
      /* 每 4KB 报告进度 + 让 Core1 WiFi 任务喘气 */
      if (total - lastReport >= 4096) {
        lastReport = total;
        if (s_progressCb) s_progressCb(total, contentLen, "下载中");
        vTaskDelay(1);
      }
    }
  }

  client->stop();

  Serial.printf("[Browser] read %d bytes\n", total);

  if (total <= 0) {
    heap_caps_free(buffer->data);
    buffer->data = NULL;
    return RENDER_ERROR_NETWORK;
  }

  buffer->data[total] = 0;

  /* 截断的 HTML 可能半截在标签中间，补上闭合标签帮 Lexbor 安全收尾 */
  if (s_html_truncated && total + 32 < contentLen + 1) {
    const char* closing = "</body></html>";
    int cl = strlen(closing);
    memcpy(buffer->data + total, closing, cl + 1);
    total += cl;
    Serial.println("[Browser] appended closing tags for truncated HTML");
  }
  buffer->size = total;

  return RENDER_SUCCESS;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 图片（缩略图）2026-09-25
 *
 * 为什么要有字节/尺寸上限：LVGL 的 SJPG 解码器（lv_sjpg.c，底层是 tjpgd）在
 * decoder_open 里**一次性**分配 w*h*3 的 RGB888 中间缓冲，再按行转成 RGB565。
 * 也就是说一张 1920x1080 的 JPEG 会直接吃掉 6MB —— 不设闸，PSRAM 瞬间见底。
 * 所以：下载有字节上限，下载完先用 tb_image_peek_size 看宽高，超预算直接丢，
 * 通过的才包成 lv_img_dsc_t 交给渲染层。
 *
 * ⚠️ 这些函数跑在后台 fetch 任务（Core 0）里，**一律不许碰 LVGL**。
 *    只有 tb_image_dsc_free 例外（它要失效图片缓存），必须在 UI 线程调。
 * ═══════════════════════════════════════════════════════════════════════ */

/* 二进制下载（与 arduino_download_html 同构，但更短、有上限、带 Referer） */
static int download_binary_inner(const char *url, uint8_t **out, size_t *outLen,
                                 size_t maxBytes, const char *referer,
                                 int depth) {
  if (!url || !out || !outLen || maxBytes == 0) return -1;
  if (depth > 3) return -1;

  String urlStr(url);
  bool isHttps = urlStr.startsWith("https://");
  int protoEnd = urlStr.indexOf("://");
  if (protoEnd < 0) return -1;
  String rest = urlStr.substring(protoEnd + 3);
  int slashPos = rest.indexOf('/');
  String host = slashPos < 0 ? rest : rest.substring(0, slashPos);
  String path = slashPos < 0 ? "/" : rest.substring(slashPos);
  int port = isHttps ? 443 : 80;
  int colonPos = host.indexOf(':');
  if (colonPos >= 0) {
    port = host.substring(colonPos + 1).toInt();
    host = host.substring(0, colonPos);
  }

  WiFiClient *client = nullptr;
  WiFiClient tcpClient;
  WiFiClientSecure sslClient;
  if (isHttps) {
    init_tls_psram();
    sslClient.setInsecure();
    sslClient.setTimeout(8);
    if (!sslClient.connect(host.c_str(), port)) return -1;
    client = &sslClient;
  } else {
    tcpClient.setTimeout(8);
    if (!tcpClient.connect(host.c_str(), port)) return -1;
    client = &tcpClient;
  }

  String req = "GET " + path + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 "
               "Build/KOT49H) AppleWebKit/537.36 (KHTML, like Gecko) "
               "Chrome/30.0.0.0 Mobile Safari/537.36\r\n" +
               "Accept: image/jpeg,image/png,image/*,*/*;q=0.8\r\n" +
               "Accept-Language: zh-CN,zh;q=0.9\r\n" +
               (referer && referer[0] ? (String("Referer: ") + referer + "\r\n")
                                      : String("")) +
               "Connection: close\r\n\r\n";
  client->print(req);

  String line;
  int status = 0;
  int contentLen = 0;
  bool chunked = false;
  String redirectTo = "";
  uint32_t t0 = millis();
  while (client->connected() || client->available()) {
    if (millis() - t0 > 8000) { client->stop(); return -1; }
    line = client->readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;
    if (status == 0 && line.startsWith("HTTP/")) {
      status = line.substring(9, 12).toInt();
    } else if (line.startsWith("Content-Length:")) {
      contentLen = line.substring(15).toInt();
    } else if (line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
      chunked = true;
    } else if (line.length() > 9 &&
               strncasecmp(line.c_str(), "location:", 9) == 0) {
      redirectTo = line.substring(9);
      redirectTo.trim();
    }
  }

  if (status >= 300 && status <= 399 && redirectTo.length() > 0) {
    client->stop();
    String nextUrl = resolve_redirect(urlStr, redirectTo);
    if (nextUrl.length() == 0) return -1;
    return download_binary_inner(nextUrl.c_str(), out, outLen, maxBytes,
                                 referer, depth + 1);
  }
  if (status != 200) { client->stop(); return -3; }
  if (contentLen > (int)maxBytes) { client->stop(); return -3; }
  if (contentLen <= 0) contentLen = (int)maxBytes;

  uint8_t *buf = (uint8_t *)heap_caps_malloc(contentLen + 8,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) { client->stop(); return -2; }
  Serial.printf("[Img] dl host=%s len=%d chunked=%d\n", host.c_str(),
                contentLen, (int)chunked);

  int total = 0;
  t0 = millis();
  if (chunked) {
    while (client->connected() || client->available()) {
      if (millis() - t0 > 10000) break;
      String sl = client->readStringUntil('\n');
      sl.trim();
      int cs = strtol(sl.c_str(), NULL, 16);
      if (cs <= 0) break;
      int room = contentLen - total;
      if (room <= 0) break;
      int want = cs > room ? room : cs;
      int got = 0;
      while (got < want && (client->connected() || client->available())) {
        int r = client->read(buf + total, want - got);
        if (r > 0) { got += r; total += r; }
        else delay(1);
      }
      client->readStringUntil('\n');
      if (got < cs) break;
      vTaskDelay(1);
    }
  } else {
    while (total < contentLen && (client->connected() || client->available())) {
      if (millis() - t0 > 10000) break;
      int r = client->read(buf + total, contentLen - total);
      if (r > 0) { total += r; if (total % 4096 < 64) vTaskDelay(1); }
      else delay(1);
    }
  }
  client->stop();

  if (total <= 0) { heap_caps_free(buf); return -1; }
  *out = buf;
  *outLen = (size_t)total;
  return 0;
}

int arduino_download_binary(const char *url, uint8_t **out, size_t *outLen,
                            size_t maxBytes, const char *referer) {
  return download_binary_inner(url, out, outLen, maxBytes, referer, 0);
}

bool tb_image_peek_size(const uint8_t *data, size_t len, int *w, int *h) {
  if (!data || !w || !h) return false;
  *w = 0;
  *h = 0;
  /* JPEG：从 SOI 开始顺着段链找 SOFn（FFC0~FFCF，排除 DHT/C4、JPG/C8、DAC/CC） */
  if (len > 24 && data[0] == 0xFF && data[1] == 0xD8) {
    size_t i = 2;
    while (i + 9 < len) {
      if (data[i] != 0xFF) { i++; continue; }
      uint8_t m = data[i + 1];
      if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7) || m == 0xFF) {
        i += 2;
        continue;
      }
      if (m == 0xC0 || m == 0xC1 || m == 0xC2 || m == 0xC3 || m == 0xC5 ||
          m == 0xC6 || m == 0xC7 || m == 0xC9 || m == 0xCA || m == 0xCB ||
          m == 0xCD || m == 0xCE || m == 0xCF) {
        *h = (data[i + 5] << 8) | data[i + 6];
        *w = (data[i + 7] << 8) | data[i + 8];
        return (*w > 0 && *h > 0);
      }
      size_t seg = ((size_t)data[i + 2] << 8) | data[i + 3];
      if (seg < 2) return false;
      i += 2 + seg;
    }
    return false;
  }
  /* PNG：IHDR 里第 16~23 字节是大端宽高 */
  if (len > 24 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E &&
      data[3] == 0x47) {
    *w = (int)(((uint32_t)data[16] << 24) | ((uint32_t)data[17] << 16) |
               ((uint32_t)data[18] << 8) | (uint32_t)data[19]);
    *h = (int)(((uint32_t)data[20] << 24) | ((uint32_t)data[21] << 16) |
               ((uint32_t)data[22] << 8) | (uint32_t)data[23]);
    return (*w > 0 && *h > 0);
  }
  /* GIF：逻辑屏幕描述符，小端 */
  if (len > 14 && data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46) {
    *w = (int)(data[6] | (data[7] << 8));
    *h = (int)(data[8] | (data[9] << 8));
    return (*w > 0 && *h > 0);
  }
  return false;
}

void *tb_image_dsc_create(uint8_t *data, size_t len, int w, int h,
                          int max_w, long max_px, int *scale_out) {
  if (scale_out) *scale_out = 0;
  if (!data || len < 16 || w <= 0 || h <= 0) return NULL;

  bool is_png = (len > 24 && data[0] == 0x89 && data[1] == 0x50 &&
                 data[2] == 0x4E && data[3] == 0x47);
  bool is_jpg = (data[0] == 0xFF && data[1] == 0xD8);
  if (!is_png && !is_jpg) return NULL;

  /* JPEG 能在**解码阶段**降采样（tjpgd 的 1/2 / 1/4 / 1/8，档位走
     lv_img_header_t.reserved，见 tools/patch_lvgl_jpeg_scale.py），
     所以再大的图也能当缩略图显示；PNG 走 lodepng，只能原尺寸解，
     超预算就放弃 —— 宁可不显示，也别把 PSRAM 吃穿。 */
  int scale = 0;
  if (is_jpg) {
    for (scale = 0; scale <= 3; scale++) {
      long sw = w >> scale;
      long sh = h >> scale;
      if (sw <= max_w && sw * sh <= max_px) break;
    }
    if (scale > 3) return NULL;   /* 1/8 都还塞不下 */
  } else if (w > max_w || (long)w * (long)h > max_px) {
    return NULL;
  }

  lv_img_dsc_t *d = (lv_img_dsc_t *)tb_alloc(sizeof(lv_img_dsc_t));
  if (!d) return NULL;
  memset(d, 0, sizeof(*d));
  d->header.always_zero = 0;
  d->header.w = w >> scale;
  d->header.h = h >> scale;
  /* ⚠️⚠️ 这里的 cf 是**给解码器看的输入**，不是"解码后的格式"，填错会出乱码：
       · PNG 必须填 RAW_ALPHA。填 TRUE_COLOR_ALPHA(5) 会踩一个大坑 ——
         内建解码器受理 4~11 这一段，PNG 解不出来时它会**接管**，而内建对
         VARIABLE 源的做法是把 data（压缩的 PNG 原文）当像素交出来。
         结果是：尺寸看着对、像素全是乱码，还不报错。
         RAW_ALPHA(2) 落在内建受理范围外，只有 PNG 解码器能认领，
         解不了就干净地失败。
       · JPEG 填 RAW，交给 SJPG 逐行 read_line。 */
  d->header.cf = is_png ? LV_IMG_CF_RAW_ALPHA : LV_IMG_CF_RAW;
  d->header.reserved = (uint32_t)scale;   /* JPEG 降采样档位 */
  d->data = data;
  d->data_size = len;
  if (scale_out) *scale_out = scale;
  return d;
}

void tb_image_dsc_discard(void *dsc) {
  if (!dsc) return;
  lv_img_dsc_t *d = (lv_img_dsc_t *)dsc;
  if (d->data) heap_caps_free((void *)d->data);
  heap_caps_free(d);
}

void tb_image_dsc_free(void *dsc) {
  if (!dsc) return;
  lv_img_dsc_t *d = (lv_img_dsc_t *)dsc;
  /* LVGL 的图片缓存按 src 指针索引。不让它失效的话，缓存里那条还指着我们
     马上要 free 的 dsc —— 下次命中就是野指针，而且是延后很久才炸的那种。 */
  lv_img_cache_invalidate_src(d);
  if (d->data) heap_caps_free((void *)d->data);
  heap_caps_free(d);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 缩略图 / 全屏大图：把原始 JPEG/PNG 字节重采样成指定尺寸的 RGB565 缓冲
 *
 * 为什么不用 lv_img_set_zoom（两条都踩过）：
 *   1) JPEG 在 LVGL 里走的是 RAW + 逐行 read_line（decoder_open 故意把
 *      img_data 留成 NULL）。LVGL 缩放时会拿**每一行**当整张图单独变换，
 *      画出来是上下错位的；
 *   2) zoom 本身是最近邻，缩到 1/5 就是马赛克 —— 而我们要的是"糊但看得清"。
 * 所以这里自己把像素抠出来做**区域平均**（box filter）：每个目标像素取它覆盖
 * 的那些源像素求平均，缩略图上还能认出这是什么。
 *
 * 取像素的两条路：
 *   · JPEG（img_data 为空）→ lv_img_decoder_read_line 逐行取（已经是 RGB565）；
 *   · PNG（解码器一次性给整块）→ 直接从 img_data 取（RGB565+alpha，3 字节/像素）。
 *
 * ⚠️ 只能在 UI 线程调（碰 LVGL 解码器）。
 * ═══════════════════════════════════════════════════════════════════════ */

#define TB_DECODE_PIXEL_CAP 400000   /* 解码中间缓冲的像素上限：w*h*3 ≤ 1.2MB */

static void tb_unpack565(uint16_t c, int *r, int *g, int *b) {
  *r = (c >> 11) & 0x1F;
  *g = (c >> 5) & 0x3F;
  *b = c & 0x1F;
}

void *tb_image_resample(void *src_dsc, int box_w, int box_h,
                        int *out_w, int *out_h) {
  if (out_w) *out_w = 0;
  if (out_h) *out_h = 0;
  if (!src_dsc || box_w <= 0 || box_h <= 0) return NULL;

  lv_img_dsc_t *sd = (lv_img_dsc_t *)src_dsc;
  if (!sd->data || sd->header.w <= 0 || sd->header.h <= 0) return NULL;

  const uint8_t *raw = sd->data;
  bool is_png = (sd->data_size > 8 && raw[0] == 0x89 && raw[1] == 0x50);

  /* JPEG 挑降采样档位：最小的那一档，让解码出来的图既塞得进 box*2
     （留点余量，区域平均才有东西可平均），又不超过解码预算。 */
  int scale = is_png ? 0 : 3;
  if (!is_png) {
    int pw = sd->header.w, ph = sd->header.h;
    for (int s = 0; s <= 3; s++) {
      long dw = pw >> s, dh = ph >> s;
      if (dw <= (long)box_w * 2 && dh <= (long)box_h * 2 &&
          dw * dh <= TB_DECODE_PIXEL_CAP) {
        scale = s;
        break;
      }
    }
  }

  lv_img_dsc_t tmp;                 /* 一份副本：档位只影响这次的解码 */
  lv_img_decoder_dsc_t dec;
  uint8_t *dst = NULL;
  int32_t *acc = NULL;
  uint8_t *row = NULL;
  int sw = 0, sh = 0, dw = 0, dh = 0, bpp = 2, has_alpha = 0;
  int ret = 0;

  tmp = *sd;
  tmp.header.reserved = (uint32_t)scale;

  if (lv_img_decoder_open(&dec, is_png ? (const void *)sd : (const void *)&tmp,
                          lv_color_white(), 0) != LV_RES_OK) {
    Serial.println("[Img] resample: decoder open failed");
    return NULL;
  }
  sw = (int)dec.header.w;
  sh = (int)dec.header.h;
  if (sw <= 0 || sh <= 0) ret = 1;
  has_alpha = lv_img_cf_has_alpha(dec.header.cf) ? 1 : 0;
  bpp = has_alpha ? 3 : 2;

  /* ⚠️ 兜底：img_data 如果就是 data 本身，说明根本没有解码器解它，
     是内建解码器把**压缩原文**原样递回来了 —— 当像素读就是彩色乱码。 */
  if (dec.img_data && (const uint8_t *)dec.img_data == sd->data) {
    Serial.println("[Img] resample: built-in handed back raw bytes, refuse");
    lv_img_decoder_close(&dec);
    return NULL;
  }
  Serial.printf("[Img] resample in: cf=%u %dx%d img_data=%s src=%u B\n",
                (unsigned)dec.header.cf, sw, sh,
                dec.img_data ? "decoded" : "line-by-line",
                (unsigned)sd->data_size);

  /* 目标尺寸：等比缩到能塞进 box（只缩不放） */
  if (!ret) {
    dw = sw;
    dh = sh;
    if (dw > box_w) {
      dh = (int)((long)dh * box_w / dw);
      dw = box_w;
    }
    if (dh > box_h) {
      dw = (int)((long)dw * box_h / dh);
      dh = box_h;
    }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    dst = (uint8_t *)tb_alloc((size_t)dw * dh * bpp);
    acc = (int32_t *)tb_alloc(sizeof(int32_t) * (size_t)dw * 4);
    if (!dst || !acc) ret = 2;
  }

  const uint8_t *mem = (const uint8_t *)dec.img_data;
  if (!ret && !mem) {
    row = (uint8_t *)tb_alloc((size_t)sw * 2);
    if (!row) ret = 3;
  }

  for (int dy = 0; !ret && dy < dh; dy++) {
    int y0 = (int)((long)dy * sh / dh);
    int y1 = (int)((long)(dy + 1) * sh / dh);
    if (y1 <= y0) y1 = y0 + 1;
    if (y1 > sh) y1 = sh;

    memset(acc, 0, sizeof(int32_t) * (size_t)dw * 4);
    for (int y = y0; y < y1; y++) {
      if (!mem) {
        /* JPEG 逐行取；read_line 已经是 RGB565，只认 y 递增，不能回头 */
        if (lv_img_decoder_read_line(&dec, 0, y, sw, row) != LV_RES_OK) {
          ret = 4;
          break;
        }
      }
      const uint8_t *srcp = mem ? (mem + (size_t)y * sw * bpp) : row;
      for (int dx = 0; dx < dw; dx++) {
        int x0 = (int)((long)dx * sw / dw);
        int x1 = (int)((long)(dx + 1) * sw / dw);
        if (x1 <= x0) x1 = x0 + 1;
        if (x1 > sw) x1 = sw;
        int r = 0, g = 0, b = 0, a = 0;
        for (int x = x0; x < x1; x++) {
          const uint8_t *q = srcp + (size_t)x * bpp;
          int rr, gg, bb;
          tb_unpack565((uint16_t)(q[0] | (q[1] << 8)), &rr, &gg, &bb);
          r += rr;
          g += gg;
          b += bb;
          if (has_alpha) a += q[2];
        }
        int32_t *o = acc + dx * 4;
        int cnt = x1 - x0;
        o[0] += r;
        o[1] += g;
        o[2] += b;
        o[3] += has_alpha ? a : (cnt * 255);
      }
    }
    if (ret) break;

    int rows = y1 - y0;
    uint8_t *dp = dst + (size_t)dy * dw * bpp;
    for (int dx = 0; dx < dw; dx++) {
      int x0 = (int)((long)dx * sw / dw);
      int x1 = (int)((long)(dx + 1) * sw / dw);
      if (x1 <= x0) x1 = x0 + 1;
      if (x1 > sw) x1 = sw;
      int cnt = rows * (x1 - x0);
      const int32_t *o = acc + dx * 4;
      int r = (int)(o[0] / cnt) & 0x1F;
      int g = (int)(o[1] / cnt) & 0x3F;
      int b = (int)(o[2] / cnt) & 0x1F;
      uint16_t c = (uint16_t)((r << 11) | (g << 5) | b);
      dp[0] = (uint8_t)(c & 0xFF);
      dp[1] = (uint8_t)(c >> 8);
      if (has_alpha) dp[2] = (uint8_t)(o[3] / cnt);
      dp += bpp;
    }
  }

  lv_img_decoder_close(&dec);
  if (row) heap_caps_free(row);
  if (acc) heap_caps_free(acc);

  if (ret) {
    if (dst) heap_caps_free(dst);
    Serial.printf("[Img] resample failed: ret=%d %dx%d scale=%d\n", ret, sw, sh,
                  scale);
    return NULL;
  }

  lv_img_dsc_t *out = (lv_img_dsc_t *)tb_alloc(sizeof(lv_img_dsc_t));
  if (!out) {
    heap_caps_free(dst);
    return NULL;
  }
  memset(out, 0, sizeof(*out));
  out->header.always_zero = 0;
  out->header.w = (uint32_t)dw;
  out->header.h = (uint32_t)dh;
  out->header.cf = has_alpha ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_TRUE_COLOR;
  out->data = dst;
  out->data_size = (uint32_t)((size_t)dw * dh * bpp);
  if (out_w) *out_w = dw;
  if (out_h) *out_h = dh;
  Serial.printf("[Img] resample %dx%d(scale %d) -> %dx%d %s\n", sw, sh, scale,
                dw, dh, has_alpha ? "RGB565A" : "RGB565");
  return out;
}

static bool lvgl_renderer_init(Renderer *renderer) {
  (void)renderer;
  return true;
}

static void lvgl_renderer_cleanup(Renderer *renderer) {
  (void)renderer;
}

static void *lvgl_renderer_create_image(Renderer *renderer, void *img_dsc,
                                         int max_w) {
  if (!renderer || !img_dsc) return NULL;
  lv_obj_t *parent = (lv_obj_t *)renderer->platform_data;
  if (!parent) return NULL;

  /* 外面套一层带边框的容器再放图。
     为什么不直接给 lv_img 加边框：lv_img 是拿**对象整体坐标**画图的
     （不是内容区），边框会被图盖住 —— 等于白设。 */
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(box, 2, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0x888888), 0);
  lv_obj_set_style_radius(box, 4, 0);

  lv_obj_t *img = lv_img_create(box);
  lv_img_set_src(img, (const lv_img_dsc_t *)img_dsc);
  lv_obj_center(img);

  /* 缩略图早就重采样到 <= THUMB 了，这里不需要再缩放。
     ⚠️ 别想着用 lv_img_set_zoom 收尾：LVGL 对未解码完的图按行变换，
     一 zoom 就错位（详见 tb_image_resample 上面的注释）。 */
  (void)max_w;
  return box;
}

static void *lvgl_renderer_create_label(Renderer *renderer, const char *text,
                                         int x, int y) {
  lv_obj_t *parent = (lv_obj_t *)renderer->platform_data;
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  /* 不设 pos，让 flex 布局自动排列 */
  (void)x; (void)y;
  /* 行布局中 label 不占满宽度，让多个 label 水平排列；
     列布局中 label 占满宽度，文本自动换行 */
  lv_flex_flow_t parent_flow = lv_obj_get_style_flex_flow(parent, 0);
  if (parent_flow == LV_FLEX_FLOW_ROW || parent_flow == LV_FLEX_FLOW_ROW_WRAP ||
      parent_flow == LV_FLEX_FLOW_ROW_REVERSE) {
    lv_obj_set_width(label, LV_SIZE_CONTENT);
  } else {
    lv_obj_set_width(label, lv_pct(100));
  }
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  return label;
}

static void *lvgl_renderer_create_button(Renderer *renderer, const char *text,
                                          int x, int y) {
  lv_obj_t *parent = (lv_obj_t *)renderer->platform_data;
  lv_obj_t *btn = lv_btn_create(parent);
  (void)x; (void)y;
  /* 行布局中按钮用内容自适应宽度 */
  lv_flex_flow_t parent_flow = lv_obj_get_style_flex_flow(parent, 0);
  if (parent_flow == LV_FLEX_FLOW_ROW || parent_flow == LV_FLEX_FLOW_ROW_WRAP ||
      parent_flow == LV_FLEX_FLOW_ROW_REVERSE) {
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 35);
  } else {
    lv_obj_set_size(btn, 70, 35);
  }

  lv_obj_t *btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, text);
  lv_obj_center(btn_label);

  return btn;
}

static lv_obj_t *lvgl_renderer_create_text_widget(Renderer *renderer,
                                                  const char *value,
                                                  const char *placeholder,
                                                  int x, int y, int width,
                                                  int height, bool multiline) {
  if (!renderer || !renderer->platform_data)
    return NULL;
  lv_obj_t *parent = (lv_obj_t *)renderer->platform_data;
  lv_obj_t *textarea = lv_textarea_create(parent);
  lv_obj_set_pos(textarea, x, y);
  lv_obj_set_size(textarea, width > 0 ? width : (multiline ? 300 : 220),
                  height > 0 ? height : (multiline ? 120 : 40));
  lv_textarea_set_one_line(textarea, !multiline);
  lv_textarea_set_text(textarea, value ? value : "");
  if (placeholder && placeholder[0] != '\0') {
    lv_textarea_set_placeholder_text(textarea, placeholder);
  }
  lv_obj_set_scrollbar_mode(textarea, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_pad_all(textarea, 6, 0);
  lv_obj_set_style_bg_color(textarea, lv_color_hex(0x141414), 0);
  lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(textarea, lv_color_hex(0x30363D), 0);
  lv_obj_set_style_border_width(textarea, 1, 0);
  /* 诊断：表单控件（搜索框/输入框）是浏览器最容易被 DOM 裁剪吞掉的部件。
     每建一个就报一次，串口里看不到 input 就说明这棵树又被吃掉了。
     （docs/10 记过根因：图标字体 glyph 让父节点被当成纯文本叶子，不再递归。） */
  Serial.printf("[Browser] input: ph=\"%s\" val=\"%s\" %dx%d @(%d,%d)%s\n",
                placeholder ? placeholder : "", value ? value : "",
                width > 0 ? width : (multiline ? 300 : 220),
                height > 0 ? height : (multiline ? 120 : 40),
                x, y, multiline ? " (multiline)" : "");
  return textarea;
}

static void *lvgl_renderer_create_text_input(Renderer *renderer,
                                             const char *value,
                                             const char *placeholder, int x,
                                             int y, int width, int height) {
  return lvgl_renderer_create_text_widget(renderer, value, placeholder, x, y,
                                          width, height, false);
}

static void *lvgl_renderer_create_text_area(Renderer *renderer,
                                            const char *value, int x, int y,
                                            int width, int height) {
  return lvgl_renderer_create_text_widget(renderer, value, NULL, x, y, width,
                                          height, true);
}

static void *lvgl_renderer_create_container(Renderer *renderer, int x, int y,
                                            int width, int height) {
  lv_obj_t *container = lv_obj_create((lv_obj_t *)renderer->platform_data);
  lv_obj_set_pos(container, x, y);
  /* 布局引擎对 auto 尺寸的 div 传 0，用内容自适应避免 0x0 不可见 */
  if (width > 0) {
    lv_obj_set_width(container, width);
  } else {
    lv_obj_set_width(container, lv_pct(100));
  }
  if (height > 0) {
    lv_obj_set_height(container, height);
  } else {
    lv_obj_set_height(container, LV_SIZE_CONTENT);
  }
  lv_obj_set_scroll_dir(container, LV_DIR_VER);
  /* 去掉 LVGL 默认的边框/圆角/padding，避免嵌套容器出现一堆空框 */
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_style_radius(container, 0, 0);
  lv_obj_set_style_pad_all(container, 0, 0);
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
  /* 默认 flex column 布局：子节点垂直排列 */
  lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(container, 2, 0);
  return container;
}

static void lvgl_renderer_set_text_color(Renderer *renderer, void *widget,
                                         uint32_t color) {
  (void)renderer;
  lv_obj_set_style_text_color((lv_obj_t *)widget, lv_color_hex(color), 0);
}

static void lvgl_renderer_set_bg_color(Renderer *renderer, void *widget,
                                       uint32_t color) {
  (void)renderer;
  lv_obj_set_style_bg_color((lv_obj_t *)widget, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa((lv_obj_t *)widget, LV_OPA_COVER, 0);
}

static lv_grad_dir_t lvgl_renderer_gradient_dir(float angle_deg) {
  float normalized = angle_deg;
  while (normalized < 0.0f)
    normalized += 360.0f;
  while (normalized >= 360.0f)
    normalized -= 360.0f;

  if ((normalized >= 45.0f && normalized < 135.0f) ||
      (normalized >= 225.0f && normalized < 315.0f)) {
    return LV_GRAD_DIR_HOR;
  }
  return LV_GRAD_DIR_VER;
}

static void lvgl_renderer_set_bg_gradient(Renderer *renderer, void *widget,
                                          const LinearGradientFill *gradient) {
  (void)renderer;
  if (!widget || !gradient || gradient->stop_count == 0)
    return;

  lv_obj_t *obj = (lv_obj_t *)widget;
  uint32_t start_color = gradient->stops[0].color;
  uint32_t end_color = gradient->stops[gradient->stop_count - 1].color;
  lv_obj_set_style_bg_color(obj, lv_color_hex(start_color), 0);
  lv_obj_set_style_bg_grad_color(obj, lv_color_hex(end_color), 0);
  lv_obj_set_style_bg_grad_dir(
      obj, lvgl_renderer_gradient_dir(gradient->angle_deg), 0);
  lv_obj_set_style_bg_main_stop(obj, 0, 0);
  lv_obj_set_style_bg_grad_stop(obj, 255, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

static void lvgl_renderer_set_text_align(Renderer *renderer, void *widget,
                                         int align) {
  (void)renderer;
  lv_text_align_t lv_align = LV_TEXT_ALIGN_LEFT;
  if (align == 1)
    lv_align = LV_TEXT_ALIGN_CENTER;
  else if (align == 2)
    lv_align = LV_TEXT_ALIGN_RIGHT;
  lv_obj_set_style_text_align((lv_obj_t *)widget, lv_align, 0);
}

static void lvgl_renderer_set_flex_direction(Renderer *renderer, void *widget,
                                             int direction) {
  (void)renderer;
  if (!widget)
    return;
  lv_obj_t *obj = (lv_obj_t *)widget;
  if (direction == 2) {
    /* row：子节点水平排列 */
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(obj, 4, 0);
  } else if (direction == 1) {
    /* column：子节点垂直排列 */
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(obj, 2, 0);
  }
}

static void lvgl_renderer_clear_container(Renderer *renderer, void *container) {
  (void)renderer;
  lv_obj_clean((lv_obj_t *)container);
}

static int lvgl_renderer_get_height(Renderer *renderer, void *widget) {
  (void)renderer;
  return lv_obj_get_height((lv_obj_t *)widget);
}

/* 平铺：一行可换行的容器。只用 flex 排，**绝不用 lv_obj_set_pos** ——
   CSS 算出来的 x/y 在本引擎里大量是 0 或离谱值，set_pos 就是版面崩掉的根因。 */
static void *lvgl_renderer_create_row_wrap(Renderer *renderer, int width) {
  if (!renderer || !renderer->platform_data)
    return NULL;
  lv_obj_t *parent = (lv_obj_t *)renderer->platform_data;
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, width > 0 ? width : lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_gap(row, 6, 0);
  lv_obj_set_style_pad_row(row, 6, 0);
  /* 容器本身必须完全隐形，否则平铺就又变成一堆空框 */
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_radius(row, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  return row;
}

/* 平铺：可点击小胶囊。边框 + 圆角 + 内边距，宽度自适应文字，超宽自动折行。 */
static void *lvgl_renderer_create_chip(Renderer *renderer, const char *text,
                                       int max_width, uint32_t color) {
  if (!renderer || !renderer->platform_data)
    return NULL;
  lv_obj_t *parent = (lv_obj_t *)renderer->platform_data;
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text ? text : "");
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, LV_SIZE_CONTENT);
  if (max_width > 0)
    lv_obj_set_style_max_width(label, max_width, 0);

  lv_obj_set_style_pad_left(label, 8, 0);
  lv_obj_set_style_pad_right(label, 8, 0);
  lv_obj_set_style_pad_top(label, 5, 0);
  lv_obj_set_style_pad_bottom(label, 5, 0);
  lv_obj_set_style_radius(label, 8, 0);
  lv_obj_set_style_border_width(label, 1, 0);
  lv_obj_set_style_border_color(label, lv_color_hex(color), 0);
  /* 同色极淡底：让"这是个可点的块"更明确，又不至于花 */
  lv_obj_set_style_bg_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(label, LV_OPA_20, 0);
  return label;
}

/* ── 链接点击 ────────────────────────────────────────────────────────────────
 * 引擎把 URL strdup 一份挂到 widget 上，点击时回传给 app 层。
 * ⚠️ 必须 strdup：布局树 node->href* 在渲染结束后就被 tactilebrowser_free_layout()
 *    free 掉了，直接存指针必然悬空。
 * 释放挂在 LV_EVENT_DELETE 上：lv_obj_clean(g_content) 会逐个 child 触发 DELETE，
 * 不会漏，也不用上层记账。 */
static LvglLinkCallback s_linkCb = NULL;
/* 诊断：本次渲染挂上了多少个可点链接。串口里 links=0 说明要么页面没链接，
   要么 href 全被判成不可跳（#/javascript:/mailto:/tel:），而不是触摸坏了。 */
static int s_linkCount = 0;

void lvgl_renderer_set_link_callback(LvglLinkCallback cb) { s_linkCb = cb; }

/* ── 缩略图点击 ──
   user_data 直接存**布局节点指针**。为什么敢存：节点和 widget 同生共死
   （换页时 freeLayoutTree + contentReset 一起做），而且这里只处理 CLICKED，
   销毁阶段不会再派发给它。 */
static LvglImageCallback s_imageCb = NULL;
void lvgl_renderer_set_image_callback(LvglImageCallback cb) { s_imageCb = cb; }

static void image_clicked_cb(lv_event_t *e) {
  if (!s_imageCb) return;
  void *node = lv_event_get_user_data(e);
  if (node) s_imageCb(node);
}

static void lvgl_renderer_register_image_handler(Renderer *renderer,
                                                 void *widget, void *node) {
  (void)renderer;
  if (!widget || !node) return;
  lv_obj_add_flag((lv_obj_t *)widget, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb((lv_obj_t *)widget, image_clicked_cb, LV_EVENT_CLICKED,
                      node);
}

void lvgl_renderer_reset_link_count(void) { s_linkCount = 0; }
int lvgl_renderer_link_count(void) { return s_linkCount; }

/* 只在这个 widget 被销毁时跑：回收给它 strdup 的那份 URL */
static void link_delete_cb(lv_event_t *e) {
  void *ud = lv_event_get_user_data(e);
  if (ud) free(ud);
}

static void link_clicked_cb(lv_event_t *e) {
  const char *url = (const char *)lv_event_get_user_data(e);
  if (!url || !url[0]) return;
  Serial.printf("[Browser] link clicked: %s\n", url);
  if (s_linkCb) s_linkCb(url);
}

static void lvgl_renderer_register_link_handler(Renderer *renderer, void *widget,
                                                const char *url) {
  (void)renderer;
  if (!widget || !url || !url[0]) return;
  /* 点了也做不了事的协议，不挂：省内存，也避免误触跳转 */
  if (url[0] == '#') return;
  if (strncmp(url, "javascript:", 11) == 0) return;
  if (strncmp(url, "mailto:", 7) == 0) return;
  if (strncmp(url, "tel:", 4) == 0) return;

  size_t n = strlen(url) + 1;
  if (n > 512) return; /* 异常长的 URL 八成是脏数据 */
  char *dup = (char *)malloc(n);
  if (!dup) return;
  memcpy(dup, url, n);

  lv_obj_t *obj = (lv_obj_t *)widget;
  /* label 默认不可点，必须显式加；加了才有 PRESSED 态 */
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  /* 按下反馈：底色加深，让"点中了"在小屏上看得见 */
  lv_obj_set_style_bg_opa(obj, LV_OPA_60, LV_STATE_PRESSED);
  lv_obj_add_event_cb(obj, link_delete_cb, LV_EVENT_DELETE, dup);
  lv_obj_add_event_cb(obj, link_clicked_cb, LV_EVENT_CLICKED, dup);
  s_linkCount++;
}

/* 平铺：一条搜索结果 = 底部留白 + 一条分隔线。
   留白让条目之间"空一格"，分隔线让"这是一块"在小屏上肉眼可辨。 */
static void lvgl_renderer_style_result_item(Renderer *renderer, void *widget) {
  (void)renderer;
  if (!widget)
    return;
  lv_obj_t *obj = (lv_obj_t *)widget;
  lv_obj_set_style_pad_bottom(obj, 12, 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(0x333333), 0);
}

LvglRenderer *lvgl_renderer_create(void) {
  LvglRenderer *renderer = (LvglRenderer *)malloc(sizeof(LvglRenderer));
  if (!renderer)
    return NULL;

  renderer->base.init = lvgl_renderer_init;
  renderer->base.cleanup = lvgl_renderer_cleanup;
  renderer->base.create_label = lvgl_renderer_create_label;
  renderer->base.create_button = lvgl_renderer_create_button;
  renderer->base.create_text_input = lvgl_renderer_create_text_input;
  renderer->base.create_text_area = lvgl_renderer_create_text_area;
  renderer->base.register_link_handler = lvgl_renderer_register_link_handler;
  renderer->base.create_container = lvgl_renderer_create_container;
  renderer->base.set_text_color = lvgl_renderer_set_text_color;
  renderer->base.set_bg_color = lvgl_renderer_set_bg_color;
  renderer->base.set_bg_gradient = lvgl_renderer_set_bg_gradient;
  renderer->base.set_text_align = lvgl_renderer_set_text_align;
  renderer->base.set_flex_direction = lvgl_renderer_set_flex_direction;
  renderer->base.clear_container = lvgl_renderer_clear_container;
  renderer->base.create_row_wrap = lvgl_renderer_create_row_wrap;
  renderer->base.create_chip = lvgl_renderer_create_chip;
  renderer->base.create_image = lvgl_renderer_create_image;
  renderer->base.register_image_handler = lvgl_renderer_register_image_handler;
  renderer->base.style_result_item = lvgl_renderer_style_result_item;
  renderer->base.get_height = lvgl_renderer_get_height;
  renderer->base.platform_data = NULL;

  return renderer;
}

void lvgl_renderer_destroy(LvglRenderer *renderer) {
  if (renderer) {
    free(renderer);
  }
}