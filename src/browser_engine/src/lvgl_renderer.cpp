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

static bool lvgl_renderer_init(Renderer *renderer) {
  (void)renderer;
  return true;
}

static void lvgl_renderer_cleanup(Renderer *renderer) {
  (void)renderer;
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