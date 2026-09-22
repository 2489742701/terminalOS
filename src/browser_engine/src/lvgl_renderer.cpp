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
  String req = "GET " + path + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n" +
               "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n" +
               "Accept-Language: zh-CN,zh;q=0.9\r\n" +
               "Connection: close\r\n\r\n";
  client->print(req);

  /* --- 阶段 3: 读取响应头，解析状态码 / Content-Length / chunked --- */
  String line;
  int status = 0;
  int contentLen = 0;
  bool chunked = false;

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
    }
  }

  Serial.printf("[Browser] status=%d len=%d chunked=%d\n", status, contentLen, chunked);

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
      String sizeLine = client->readStringUntil('\n');
      sizeLine.trim();
      int chunkSize = strtol(sizeLine.c_str(), NULL, 16);
      if (chunkSize <= 0) break;
      int got = 0;
      while (got < chunkSize && (client->connected() || client->available())) {
        int r = client->read((uint8_t*)(buffer->data + total), chunkSize - got);
        if (r > 0) { got += r; total += r; }
        else delay(1);
        /* 每 4KB 报告进度 + 让 Core1 WiFi 任务喘气 */
        if (total - lastReport >= 4096) {
          lastReport = total;
          if (s_progressCb) s_progressCb(total, contentLen, "下载中");
          vTaskDelay(1);
        }
      }
      client->readStringUntil('\n');
    }
  } else {
    /* 普通传输：按 Content-Length 读取 */
    while (total < contentLen && (client->connected() || client->available())) {
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
  renderer->base.register_link_handler = NULL;
  renderer->base.create_container = lvgl_renderer_create_container;
  renderer->base.set_text_color = lvgl_renderer_set_text_color;
  renderer->base.set_bg_color = lvgl_renderer_set_bg_color;
  renderer->base.set_bg_gradient = lvgl_renderer_set_bg_gradient;
  renderer->base.set_text_align = lvgl_renderer_set_text_align;
  renderer->base.set_flex_direction = lvgl_renderer_set_flex_direction;
  renderer->base.clear_container = lvgl_renderer_clear_container;
  renderer->base.get_height = lvgl_renderer_get_height;
  renderer->base.platform_data = NULL;

  return renderer;
}

void lvgl_renderer_destroy(LvglRenderer *renderer) {
  if (renderer) {
    free(renderer);
  }
}