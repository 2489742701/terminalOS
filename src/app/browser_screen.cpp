#include "browser_screen.h"
#include "icons.h"
#include "nav.h"
#include "font_zh.h"
#include <lvgl.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include "lvgl_renderer.h"
#include "tactilebrowser_core.h"


/* ═══════════════════════════════════════════════════════════════════════════
 * 浏览器屏幕布局（480×480）
 *
 *   ┌──┬────────────────────────────┬──┬──────┐
 *   │退│ URL 输入框                 │加│ 状态  │  0-40  顶栏
 *   ├──┴────────────────────────────┴──┴──────┤
 *   │                                        │
 *   │           网页内容区（可滚动）           │  40-410 内容
 *   │                                        │
 *   ├────────────────────────────────────────┤
 *   │  ████████░░░░░░  下载中 45%            │  410-435 进度条
 *   ├──┬──────┬──────┬──────┬──────┬──────┤
 *   │  │ 后退 │ 前进 │ 首页 │ 退出 │      │  435-480 底栏
 *   └──┴──────┴──────┴──────┴──────┴──────┘
 *
 * 设计要点：
 *   - 去掉"浏览器"标题，把空间让给 URL 和内容
 *   - 状态栏（就绪/加载中/已加载）放顶栏右侧
 *   - 底部 4 按钮：后退/前进/首页/退出
 *   - URL 框可重复点击弹出键盘输入
 *   - 默认 URL = https://www.baidu.com，进入浏览器自动加载
 * ═══════════════════════════════════════════════════════════════════════════ */

namespace {

/* ── UI 控件 ── */
SwipeState g_swipe;
lv_obj_t* g_urlArea = nullptr;
lv_obj_t* g_content = nullptr;
lv_obj_t* g_status = nullptr;
lv_obj_t* g_goBtn = nullptr;
lv_obj_t* g_kb = nullptr;
lv_obj_t* g_progressBar = nullptr;
lv_obj_t* g_progressLabel = nullptr;
lv_obj_t* g_backBtn = nullptr;    /* 底部后退 */
lv_obj_t* g_fwdBtn = nullptr;     /* 底部前进 */
lv_obj_t* g_homeBtn = nullptr;    /* 底部首页 */
lv_obj_t* g_exitBtn = nullptr;    /* 底部退出 */

/* ── 加载状态 ── */
String g_fetchUrl;
bool g_fetching = false;
bool g_firstLoad = true;          /* 首次进入自动加载百度 */

/* ── 引擎 ── */
LvglRenderer* g_renderer = nullptr;
bool g_engineInited = false;

/* ── 导航历史栈（最多 20 条） ── */
#define MAX_HISTORY 20
String g_history[MAX_HISTORY];
int g_historySize = 0;
int g_historyIdx = -1;

void historyPush(const String& url) {
  /* 在历史中间打开新 URL 时，截断后面的记录 */
  if (g_historyIdx < g_historySize - 1)
    g_historySize = g_historyIdx + 1;
  if (g_historySize < MAX_HISTORY) {
    g_history[g_historySize] = url;
    g_historySize++;
  } else {
    /* 满了左移 */
    for (int i = 1; i < MAX_HISTORY; i++)
      g_history[i-1] = g_history[i];
    g_history[MAX_HISTORY-1] = url;
    g_historySize = MAX_HISTORY;
  }
  g_historyIdx = g_historySize - 1;
}

void updateNavButtons() {
  if (g_backBtn) lv_obj_set_style_bg_opa(g_backBtn,
    g_historyIdx > 0 ? LV_OPA_COVER : LV_OPA_50, 0);
  if (g_fwdBtn) lv_obj_set_style_bg_opa(g_fwdBtn,
    g_historyIdx < g_historySize - 1 ? LV_OPA_COVER : LV_OPA_50, 0);
  if (g_homeBtn) lv_obj_set_style_bg_opa(g_homeBtn,
    g_historySize > 0 ? LV_OPA_COVER : LV_OPA_50, 0);
}

/* ── 进度回调（下载过程中定期调用） ──
 * 注意：此处不能调 lv_timer_handler()！
 *   fetchPage() 在 BrowserScreen_tick() → App::loop() 中阻塞式执行，
 *   下载过程中 LVGL 显示状态可能不完整（内容区刚被 lv_obj_clean 清空），
 *   此时调 lv_timer_handler() 会触发渲染管线处理半成品状态导致崩溃。
 *   进度条更新会在下一个 loop() 周期的 lv_timer_handler() 中自然刷新。 */
void progressCb(int downloaded, int total, const char* stage) {
  if (g_progressBar) {
    int pct = total > 0 ? downloaded * 100 / total : 0;
    lv_bar_set_value(g_progressBar, pct, LV_ANIM_OFF);
  }
  if (g_progressLabel) {
    int pct = total > 0 ? downloaded * 100 / total : 0;
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %d%%", stage, pct);
    lv_label_set_text(g_progressLabel, buf);
  }
}

/* ── 事件回调 ── */
void exit_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    nav_go_anim(nav_launcher, LV_SCR_LOAD_ANIM_OVER_LEFT, 300);
}

void swipe_cb(lv_event_t* e) {
  swipe_detect(e, g_swipe, nav_launcher, SWIPE_H, false, 40);
}

/* URL 框被点击 → 弹出键盘 */
void url_focus_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED && g_kb) {
    lv_obj_clear_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(g_kb, g_urlArea);
  }
}

void go_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char* url = lv_textarea_get_text(g_urlArea);
  if (!url || strlen(url) == 0) return;
  g_fetchUrl = String(url);
  historyPush(g_fetchUrl);
  g_fetching = true;
  g_firstLoad = false;
  if (g_kb) lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(g_status, "加载中");
  lv_obj_clean(g_content);
  updateNavButtons();
}

void back_nav_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_historyIdx <= 0) return;
  g_historyIdx--;
  g_fetchUrl = g_history[g_historyIdx];
  g_fetching = true;
  lv_textarea_set_text(g_urlArea, g_fetchUrl.c_str());
  lv_label_set_text(g_status, "加载中");
  lv_obj_clean(g_content);
  updateNavButtons();
}

void fwd_nav_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_historyIdx >= g_historySize - 1) return;
  g_historyIdx++;
  g_fetchUrl = g_history[g_historyIdx];
  g_fetching = true;
  lv_textarea_set_text(g_urlArea, g_fetchUrl.c_str());
  lv_label_set_text(g_status, "加载中");
  lv_obj_clean(g_content);
  updateNavButtons();
}

void home_nav_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_historySize == 0) return;
  g_historyIdx = 0;
  g_fetchUrl = g_history[0];
  g_fetching = true;
  lv_textarea_set_text(g_urlArea, g_fetchUrl.c_str());
  lv_label_set_text(g_status, "加载中");
  lv_obj_clean(g_content);
  updateNavButtons();
}

/* ── 引擎管理 ── */
void ensureEngineInit() {
  if (g_engineInited) return;
  tactilebrowser_core_init();
  g_renderer = lvgl_renderer_create();
  tactilebrowser_set_renderer(&g_renderer->base);
  tactilebrowser_set_html_downloader(arduino_download_html);
  arduino_set_progress_callback(progressCb);
  g_engineInited = true;
}

/* 每次加载前完整重建引擎，清除 Lexbor 内部脏状态 */
void resetEngine() {
  if (!g_engineInited) return;
  tactilebrowser_core_cleanup();
  tactilebrowser_core_init();
  tactilebrowser_set_renderer(&g_renderer->base);
  tactilebrowser_set_html_downloader(arduino_download_html);
  arduino_set_progress_callback(progressCb);
}

/* ── 加载网页（在 tick 中调用，阻塞式） ── */
void fetchPage() {
  ensureEngineInit();

  if (WiFi.status() != WL_CONNECTED) {
    lv_label_set_text(g_status, "等WiFi");
    for (int i = 0; i < 50 && WiFi.status() != WL_CONNECTED; i++)
      delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    lv_label_set_text(g_status, "无WiFi");
    g_fetching = false;
    return;
  }

  String url = g_fetchUrl;
  if (!url.startsWith("http://") && !url.startsWith("https://"))
    url = "http://" + url;

  Serial.printf("[Browser] fetching: %s\n", url.c_str());
  lv_label_set_text(g_status, "下载中");

  resetEngine();
  lv_obj_clean(g_content);

  /* 诊断：渲染前打印内存情况 */
  Serial.printf("[Diag] before render: DRAM free=%u, PSRAM free=%u\n",
    (unsigned)xPortGetFreeHeapSize(),
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  /* 诊断：渲染前打印 g_content 信息 */
  if (g_content) {
    int cc = 0;
    lv_obj_t* ch = lv_obj_get_child(g_content, 0);
    while (ch) { cc++; ch = lv_obj_get_child(g_content, cc); }
    Serial.printf("[Diag] BEFORE render: g_content w=%d h=%d x=%d y=%d children=%d\n",
      lv_obj_get_width(g_content), lv_obj_get_height(g_content),
      lv_obj_get_x(g_content), lv_obj_get_y(g_content), cc);
  } else {
    Serial.println("[Diag] BEFORE render: g_content is NULL!");
  }

  /* 内容区高度 = 410-40 = 370，宽度 = 480 */
  RenderResult result = tactilebrowser_render_url(url.c_str(), g_content, 460, 360);

  Serial.printf("[Browser] render result: %d\n", (int)result);

  /* 诊断：渲染后打印 g_content 信息 */
  if (g_content) {
    int cc = 0;
    lv_obj_t* ch = lv_obj_get_child(g_content, 0);
    while (ch) { cc++; ch = lv_obj_get_child(g_content, cc); }
    Serial.printf("[Diag] AFTER render: g_content w=%d h=%d x=%d y=%d children=%d\n",
      lv_obj_get_width(g_content), lv_obj_get_height(g_content),
      lv_obj_get_x(g_content), lv_obj_get_y(g_content), cc);
  } else {
    Serial.println("[Diag] AFTER render: g_content is NULL!");
  }

  /* 诊断：打印 g_content 的子对象信息 */
  if (g_content) {
    int childCount = 0;
    lv_obj_t* child = lv_obj_get_child(g_content, 0);
    while (child) {
      childCount++;
      child = lv_obj_get_child(g_content, childCount);
    }
    Serial.printf("[Browser] g_content children=%d w=%d h=%d x=%d y=%d\n",
      childCount, lv_obj_get_width(g_content), lv_obj_get_height(g_content),
      lv_obj_get_x(g_content), lv_obj_get_y(g_content));
    /* 打印前 3 个子对象的信息 */
    for (int i = 0; i < 3 && i < childCount; i++) {
      lv_obj_t* c = lv_obj_get_child(g_content, i);
      const char* txt = "";
      if (lv_obj_check_type(c, &lv_label_class)) txt = lv_label_get_text(c);
      Serial.printf("[Browser] child[%d]: x=%d y=%d w=%d h=%d txt='%.30s'\n",
        i, lv_obj_get_x(c), lv_obj_get_y(c),
        lv_obj_get_width(c), lv_obj_get_height(c), txt);
    }
  }

  if (result == RENDER_SUCCESS)         lv_label_set_text(g_status, "已加载");
  else if (result == RENDER_ERROR_NETWORK) lv_label_set_text(g_status, "网络错误");
  else if (result == RENDER_ERROR_PARSE)   lv_label_set_text(g_status, "解析失败");
  else if (result == RENDER_ERROR_MEMORY)  lv_label_set_text(g_status, "内存不足");
  else                                     lv_label_set_text(g_status, "未知错误");

  lv_bar_set_value(g_progressBar, 0, LV_ANIM_OFF);
  lv_label_set_text(g_progressLabel, "");
  g_fetching = false;
}

}  // namespace

/* ═══════════════════════════════════════════════════════════════════════════
 * 创建浏览器屏幕
 * ═══════════════════════════════════════════════════════════════════════════ */
lv_obj_t* BrowserScreen_create() {
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

  /* ── 顶栏 (0-40) ── */
  /* 退出按钮（左上角，替代原来的返回箭头） */
  lv_obj_t* exitTop = lv_btn_create(scr);
  lv_obj_set_size(exitTop, 40, 36);
  lv_obj_align(exitTop, LV_ALIGN_TOP_LEFT, 2, 2);
  lv_obj_set_style_bg_color(exitTop, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_bg_color(exitTop, lv_color_hex(0x333333), LV_STATE_PRESSED);
  lv_obj_set_style_radius(exitTop, 6, 0);
  lv_obj_set_style_border_width(exitTop, 1, 0);
  lv_obj_set_style_border_color(exitTop, lv_color_hex(0x444444), 0);
  lv_obj_add_event_cb(exitTop, exit_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* exitLbl = lv_label_create(exitTop);
  lv_label_set_text(exitLbl, "退出");
  lv_obj_set_style_text_color(exitLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(exitLbl, &font_zh_16, 0);
  lv_obj_center(exitLbl);

  /* URL 输入框 */
  g_urlArea = lv_textarea_create(scr);
  lv_obj_set_size(g_urlArea, 300, 36);
  lv_obj_align(g_urlArea, LV_ALIGN_TOP_LEFT, 46, 2);
  lv_textarea_set_placeholder_text(g_urlArea, "输入网址");
  lv_textarea_set_text(g_urlArea, "https://www.baidu.com");
  lv_textarea_set_one_line(g_urlArea, true);
  lv_textarea_set_cursor_click_pos(g_urlArea, true);
  lv_obj_set_style_text_font(g_urlArea, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(g_urlArea, lv_color_white(), 0);
  lv_obj_set_style_border_color(g_urlArea, lv_color_hex(0x444444), 0);
  lv_obj_set_style_border_width(g_urlArea, 1, 0);
  lv_obj_set_style_radius(g_urlArea, 6, 0);
  lv_obj_set_style_bg_color(g_urlArea, lv_color_hex(0x111111), 0);
  /* 点击 URL 框弹出键盘 */
  lv_obj_add_event_cb(g_urlArea, url_focus_cb, LV_EVENT_CLICKED, NULL);

  /* 加载按钮 */
  g_goBtn = lv_btn_create(scr);
  lv_obj_set_size(g_goBtn, 50, 36);
  lv_obj_align(g_goBtn, LV_ALIGN_TOP_LEFT, 350, 2);
  lv_obj_set_style_bg_color(g_goBtn, lv_color_hex(0x1a1a1a), 0);
  lv_obj_set_style_bg_color(g_goBtn, lv_color_hex(0x333333), LV_STATE_PRESSED);
  lv_obj_set_style_radius(g_goBtn, 6, 0);
  lv_obj_set_style_border_width(g_goBtn, 1, 0);
  lv_obj_set_style_border_color(g_goBtn, lv_color_hex(0x444444), 0);
  lv_obj_add_event_cb(g_goBtn, go_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* goLbl = lv_label_create(g_goBtn);
  lv_label_set_text(goLbl, "加载");
  lv_obj_set_style_text_color(goLbl, lv_color_white(), 0);
  lv_obj_set_style_text_font(goLbl, &font_zh_16, 0);
  lv_obj_center(goLbl);

  /* 状态标签（顶栏右侧） */
  g_status = lv_label_create(scr);
  lv_label_set_text(g_status, WiFi.status() == WL_CONNECTED ? "就绪" : "无WiFi");
  lv_obj_set_style_text_color(g_status, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(g_status, &font_zh_16, 0);
  lv_obj_align(g_status, LV_ALIGN_TOP_LEFT, 406, 10);

  /* ── 内容区 (40-410) ── */
  g_content = lv_obj_create(scr);
  lv_obj_set_size(g_content, 476, 366);
  lv_obj_align(g_content, LV_ALIGN_TOP_LEFT, 2, 42);
  lv_obj_set_style_bg_color(g_content, lv_color_hex(0x0a0a0a), 0);
  lv_obj_set_style_bg_opa(g_content, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_content, 0, 0);
  lv_obj_set_style_pad_all(g_content, 6, 0);
  lv_obj_set_scroll_dir(g_content, LV_DIR_VER);
  lv_obj_set_style_text_color(g_content, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_text_font(g_content, &font_zh_16, 0);

  /* ── 进度条 + 进度文字 (410-435) ── */
  g_progressBar = lv_bar_create(scr);
  lv_obj_set_size(g_progressBar, 340, 10);
  lv_obj_align(g_progressBar, LV_ALIGN_TOP_LEFT, 10, 414);
  lv_bar_set_range(g_progressBar, 0, 100);
  lv_bar_set_value(g_progressBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_progressBar, lv_color_hex(0x222222), 0);
  lv_obj_set_style_bg_color(g_progressBar, lv_color_hex(0x4488ff), LV_PART_INDICATOR);

  g_progressLabel = lv_label_create(scr);
  lv_label_set_text(g_progressLabel, "");
  lv_obj_set_style_text_color(g_progressLabel, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(g_progressLabel, &lv_font_montserrat_14, 0);
  lv_obj_align(g_progressLabel, LV_ALIGN_TOP_LEFT, 360, 412);

  /* ── 底栏四按钮 (435-480) ── */
  /* 每个按钮 110×40，间距 8，起始 x=10 */
  #define BTN_W 110
  #define BTN_H 40
  #define BTN_GAP 8
  #define BTN_Y 438
  #define BTN_X(i) (10 + i * (BTN_W + BTN_GAP))

  auto makeBtn = [&](lv_obj_t** btn, const char* text, lv_event_cb_t cb, int x) {
    *btn = lv_btn_create(scr);
    lv_obj_set_size(*btn, BTN_W, BTN_H);
    lv_obj_align(*btn, LV_ALIGN_TOP_LEFT, x, BTN_Y);
    lv_obj_set_style_bg_color(*btn, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_color(*btn, lv_color_hex(0x333333), LV_STATE_PRESSED);
    lv_obj_set_style_radius(*btn, 6, 0);
    lv_obj_set_style_border_width(*btn, 1, 0);
    lv_obj_set_style_border_color(*btn, lv_color_hex(0x444444), 0);
    lv_obj_add_event_cb(*btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl = lv_label_create(*btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &font_zh_16, 0);
    lv_obj_center(lbl);
  };

  makeBtn(&g_backBtn, "后退", back_nav_cb, BTN_X(0));
  makeBtn(&g_fwdBtn,  "前进", fwd_nav_cb,  BTN_X(1));
  makeBtn(&g_homeBtn, "首页", home_nav_cb, BTN_X(2));
  makeBtn(&g_exitBtn, "退出", exit_event_cb, BTN_X(3));

  updateNavButtons();

  /* ── 键盘（弹出式，覆盖底部） ──
   * 暂时禁用键盘：lv_keyboard_create 会创建约 100 个 LVGL 对象，
   * 占用大量 DRAM 导致浏览器渲染时内存不足崩溃。
   * URL 输入暂时通过串口命令 browser <url> 实现。 */
  /* lv_obj_t* kb = lv_keyboard_create(scr); */
  /* lv_obj_set_size(kb, 480, 200); */
  /* lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0); */
  /* lv_keyboard_set_textarea(kb, g_urlArea); */
  /* lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN); */
  /* g_kb = kb; */

  /* 诊断：确认 g_content 创建后的尺寸 */
  Serial.printf("[Diag] BrowserScreen_create: g_content=%p w=%d h=%d x=%d y=%d\n",
    g_content, g_content ? lv_obj_get_width(g_content) : -1,
    g_content ? lv_obj_get_height(g_content) : -1,
    g_content ? lv_obj_get_x(g_content) : -1,
    g_content ? lv_obj_get_y(g_content) : -1);

  return scr;
}

/* ── tick：首次自动加载百度 + 处理加载请求 ── */
void BrowserScreen_tick() {
  /* 诊断：tick 开头打印 g_content 尺寸 */
  if (g_firstLoad || g_fetching) {
    Serial.printf("[Diag] tick: g_content=%p w=%d h=%d\n",
      g_content, g_content ? lv_obj_get_width(g_content) : -1,
      g_content ? lv_obj_get_height(g_content) : -1);
  }
  /* 首次进入浏览器自动加载百度 */
  if (g_firstLoad && !g_fetching) {
    g_fetchUrl = "https://www.baidu.com";
    historyPush(g_fetchUrl);
    g_fetching = true;
    g_firstLoad = false;
    lv_label_set_text(g_status, "加载中");
    updateNavButtons();
  }
  if (g_fetching) fetchPage();
}

/* ── 串口调试用：外部传入 URL 触发加载 ── */
void BrowserScreen_navigate(const char* url) {
  if (!url || strlen(url) == 0) return;
  g_fetchUrl = String(url);
  historyPush(g_fetchUrl);
  g_fetching = true;
  g_firstLoad = false;
  if (g_status) lv_label_set_text(g_status, "加载中");
  if (g_content) lv_obj_clean(g_content);
  if (g_kb) lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
  if (g_urlArea) lv_textarea_set_text(g_urlArea, url);
  updateNavButtons();
  Serial.printf("[Browser] navigate: %s\n", url);
}
