#include "browser_screen.h"
#include "icons.h"
#include "nav.h"
#include "font_zh.h"
#include <lvgl.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "lvgl_renderer.h"
#include "tactilebrowser_core.h"

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
static const char* DEFAULT_URL = "https://www.baidu.com";

/* 统一更新 URL 显示 + 真值 */
void setUrlText(const char* s) {
  g_currentUrl = s ? s : "";
  if (g_urlArea && lv_obj_is_valid(g_urlArea))
    lv_label_set_text(g_urlArea, g_currentUrl.c_str());
}
lv_obj_t* g_content = nullptr;
lv_obj_t* g_status = nullptr;
lv_obj_t* g_goBtn = nullptr;
lv_obj_t* g_progressBar = nullptr;
lv_obj_t* g_progressLabel = nullptr;
lv_obj_t* g_backBtn = nullptr;
lv_obj_t* g_fwdBtn = nullptr;
lv_obj_t* g_homeBtn = nullptr;
lv_obj_t* g_exitBtn = nullptr;

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

void updateNavButtons() {
  if (g_backBtn) lv_obj_set_style_bg_opa(g_backBtn,
    g_historyIdx > 0 ? LV_OPA_COVER : LV_OPA_50, 0);
  if (g_fwdBtn) lv_obj_set_style_bg_opa(g_fwdBtn,
    g_historyIdx < g_historySize - 1 ? LV_OPA_COVER : LV_OPA_50, 0);
  if (g_homeBtn) lv_obj_set_style_bg_opa(g_homeBtn,
    g_historySize > 0 ? LV_OPA_COVER : LV_OPA_50, 0);
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
  swipe_detect(e, g_swipe, nav_launcher, SWIPE_H, false, 40);
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

/* 后台任务栈。必须在启动早期（DRAM 未碎片化时）一次性分配，常驻不销毁。
   之前在每次加载时用 xTaskCreatePinnedToCore 现申请这块连续内存，跑到第二次
   内部 DRAM 只剩 ~93KB，凑不出 48KB 连续块 → "Failed to create fetch task"。 */
#define FETCH_STACK_BYTES 49152

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

    String url = g_fetchUrl;
    if (!url.startsWith("http://") && !url.startsWith("https://"))
      url = "http://" + url;

    Serial.printf("[Browser] fetch_task start: %s\n", url.c_str());

    g_taskResult = tactilebrowser_download_and_parse(
        url.c_str(), 460, 360, &g_stopRequested, &g_layoutRoot);

    Serial.printf("[Browser] fetch_task done: result=%d layout=%p DRAM free=%u\n",
      (int)g_taskResult, g_layoutRoot,
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

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
  tactilebrowser_set_html_downloader(arduino_download_html);
  arduino_set_progress_callback(progressCb);

  g_fetchUrl = url;
  g_stopRequested = false;
  g_taskDone = false;
  g_layoutRoot = nullptr;
  g_state = BROWSER_LOADING;

  /* 显示加载 UI */
  if (g_status) lv_label_set_text(g_status, "加载中");
  showLoadingOverlay();
  if (g_content) lv_obj_clean(g_content);
  updateNavButtons();

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
  if (g_historySize == 0) return;
  g_historyIdx = 0;
  setUrlText(g_history[0].c_str());
  startFetch(g_history[0]);
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

  /* URL 栏用 label 而非 textarea：
     textarea 是 LVGL 里最贵的对象之一（内含 label + 光标 + 游标层），而它在本项目
     根本无法输入（键盘依赖串口）。换成 label 直接省下一块 DRAM。
     当前 URL 由 g_currentUrl 维护（label 只负责显示）。 */
  g_urlArea = lv_label_create(scr);
  lv_obj_set_size(g_urlArea, 300, 36);
  lv_obj_align(g_urlArea, LV_ALIGN_TOP_LEFT, 46, 2);
  lv_label_set_long_mode(g_urlArea, LV_LABEL_LONG_DOT);  // 过长截断，不撑破布局
  setUrlText(DEFAULT_URL);
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
  lv_obj_set_flex_flow(g_content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(g_content, 2, 0);

  /* ── 加载遮罩（覆盖在内容区上，默认隐藏） ── */
  g_loadingOverlay = lv_obj_create(scr);
  lv_obj_set_size(g_loadingOverlay, 476, 366);
  lv_obj_align(g_loadingOverlay, LV_ALIGN_TOP_LEFT, 2, 42);
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

  return scr;
}

/* ── tick：状态机驱动 ── */
void BrowserScreen_tick() {
  /* 首次进入自动加载百度 */
  if (g_firstLoad && g_state == BROWSER_IDLE) {
    g_firstLoad = false;
    historyPush("https://www.baidu.com");
    startFetch("https://www.baidu.com");
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
        RenderResult r = tactilebrowser_render_layout(g_layoutRoot, g_content, 460, 360);
        tactilebrowser_free_layout(g_layoutRoot);
        g_layoutRoot = nullptr;

        if (r == RENDER_SUCCESS) {
          g_state = BROWSER_LOADED;
          if (g_status) lv_label_set_text(g_status, "已加载");
          Serial.printf("[Browser] render done. DRAM free: %u, PSRAM free: %u\n",
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
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
          lv_obj_clean(g_content);
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
          lv_obj_clean(g_content);
          lv_obj_t* msg = lv_label_create(g_content);
          lv_label_set_text(msg, errMsg);
          lv_obj_set_style_text_color(msg, lv_color_hex(0xFF6666), 0);
          lv_obj_set_style_text_font(msg, &font_zh_16, 0);
        }
      }

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
  /* 若后台任务还在跑，让它尽快退出；常驻任务不会被删除 */
  g_stopRequested = true;
  g_hasPending = false;
  g_pendingUrl = "";

  if (g_content) lv_obj_clean(g_content);

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

/* ── 启动早期调用：一次性建好常驻后台任务 ── */
void BrowserScreen_preinit() {
  ensureFetchTask();
}
