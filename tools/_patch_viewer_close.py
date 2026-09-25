# -*- coding: utf-8 -*-
"""修"看图覆盖层退不出去"（master 被卡死）

两条根因：
1. 覆盖层 root 建在 lv_layer_top() 上、全屏 CLICKABLE —— 底下那个 screen 的
   统一返回手势**收不到事件**，左右滑在这里完全失效。
2. 关闭只置 g_viewClosePending 等 BrowserScreen_tick，而 tick 只在
   "浏览器主页是当前 Activity" 时才跑（app.cpp: act == nav_browser）。
   覆盖层一旦在别的时机被打开就永远关不掉 → 点叉叉没反应、卡死。
   （以前"点保存图片崩掉才能退出"只是崩溃把覆盖层带走了，叉叉一直就是坏的。）

修法：
   · 关闭改成一次性 lv_timer —— 属于 LVGL 自己的调度，跟当前页面无关
   · 覆盖层自己接一份滑动手势（左右滑 / 上下边缘滑 → 关）
   · g_content 加 EVENT_BUBBLE：LVGL 事件默认不冒泡，内容区盖住大半屏幕时
     scr 上的手势收不到 PRESSED —— 这就是"缓存页左滑退不出去"
"""
import io
B = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp'
s = io.open(B, encoding='utf-8').read()


def rep(s, old, new, tag, cnt=1):
    n = s.count(old)
    assert n == cnt, ('anchor %s: found %d' % (tag, n))
    return s.replace(old, new, 1)


# ───────── 1) 关闭改成一次性 lv_timer + 覆盖层自己接手势 ─────────
old = """/* ⚠️ 三个回调都**不直接动手**：关覆盖层 = 删掉正在派发事件的树，
   开覆盖层 = 在自己的事件回调里往树上加节点。一律置标志，tick 里做。 */
static void viewCloseCb(lv_event_t* e) { (void)e; g_viewClosePending = true; }"""
new = """/* 真正动手关：延迟一拍（一次性 lv_timer）。
   ⛔ 为什么不再"置标志等 BrowserScreen_tick"：
      tick 只在"浏览器主页是当前 Activity"时才被 App::loop 调用
      （app.cpp: `if (act == nav_browser) BrowserScreen_tick()`）。
      覆盖层一旦在别的时机被打开，标志就永远没人处理 —— 表现就是
      「点右上角叉叉没反应，卡死在覆盖层里」。
      lv_timer 走 LVGL 自己的调度，跟当前是哪个页面无关。 */
static void viewCloseTimerCb(lv_timer_t* t) {
  lv_timer_del(t);
  closeImageViewer();
}

static void viewRequestClose(const char* why) {
  Serial.printf("[Img] viewer close: %s\\n", why);
  lv_timer_t* t = lv_timer_create(viewCloseTimerCb, 1, nullptr);
  if (t) lv_timer_set_repeat_count(t, 1);
}

/* ⚠️ 三个回调都**不直接动手**：关覆盖层 = 删掉正在派发事件的树，
   开覆盖层 = 在自己的事件回调里往树上加节点。一律延迟一拍。 */
static void viewCloseCb(lv_event_t* e) {
  (void)e;
  viewRequestClose("button/blank");
}

/* 覆盖层全屏盖在 lv_layer_top() 上 —— 底下 screen 的统一返回手势**收不到
   事件**（LVGL 事件默认不冒泡，而且 layer_top 在上层），所以左右滑返回在
   这里是失效的。覆盖层只能自己接一份手势，否则用户滑不动也退不出去。 */
static SwipeState g_viewSwipe;
static void viewSwipeCb(lv_event_t* e) {
  if (swipe_back_any(e, g_viewSwipe, true)) viewRequestClose("swipe");
}"""
s = rep(s, old, new, 'close-cb')

# ───────── 2) root 挂上手势 ─────────
old = """  /* 点空白处关闭（图片本身不可点，所以点图不会误关） */
  lv_obj_add_event_cb(root, viewCloseCb, LV_EVENT_CLICKED, nullptr);
  g_viewRoot = root;"""
new = """  /* 点空白处关闭（图片本身不可点，所以点图不会误关） */
  lv_obj_add_event_cb(root, viewCloseCb, LV_EVENT_CLICKED, nullptr);
  /* 滑动退出：layer_top 挡住了底下 screen 的手势，这里必须自己接一份 */
  lv_obj_add_event_cb(root, viewSwipeCb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(root, viewSwipeCb, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(root, viewSwipeCb, LV_EVENT_RELEASED, nullptr);
  g_viewRoot = root;"""
s = rep(s, old, new, 'root-gesture')

# ───────── 3) g_content 冒泡 ─────────
old = """  g_content = lv_obj_create(scr);"""
new = """  g_content = lv_obj_create(scr);
  /* ⚠️ LVGL 事件默认**不冒泡**：统一返回手势挂在 scr 上，而 g_content 盖住了
     大半个屏幕 —— 手指落在内容区时 scr 根本收不到 PRESSED，于是网页里、
     "下载的网站"列表里左右滑都退不出去。让它冒泡，手势就能穿过内容区。
     ⛔ 只影响 PRESSED/PRESSING/RELEASED，按钮的 CLICKED 不会被误触发。 */
  lv_obj_add_flag(g_content, LV_OBJ_FLAG_EVENT_BUBBLE);"""
s = rep(s, old, new, 'bubble')

io.open(B, 'w', encoding='utf-8', newline='').write(s)
print('ok')
