#include "status_bar.h"
#include "icons.h"
#include "nav.h"
#include "font_zh.h"
#include "../hal/battery.h"
#include "../config/pins.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

namespace {

constexpr int BAR_H = 28;

/* 右上角图标尺寸。22 的 WiFi 放在 28 高的栏里还有 3px 上下留白，正好。 */
constexpr int WIFI_ICON_SIZE = 22;
constexpr int BT_ICON_SIZE = 18;

/* ── 状态栏注册表（不再用单组全局指针）───────────────────────────────────
 * 以前是一组 g_bar/g_fpsLabel/... 全局变量 + 每次 StatusBar_create() 都
 * lv_timer_create() 一个新的 1s 定时器。两个致命后果：
 *   1) 建几个屏就有几个定时器，全都写同一组全局指针（Launcher + Browser = 2 个）；
 *   2) 屏被销毁（nav_release_all_except 的 lv_obj_del，或者 lv_scr_load_anim
 *      的 auto_del）之后，全局指针变成悬空指针，定时器照跑 →
 *      lv_label_set_text(野指针) → Guru Meditation (LoadProhibited)。
 * 改成：每条 bar 一个槽位，注销靠 LV_EVENT_DELETE 回调，定时器全局只建一次，
 * 刷新前用 lv_obj_is_valid() 验活。这样屏被谁删、什么时候删都不会再崩。 */
/* 槽位数必须 >= 同时存在的屏数。给 12：11 个 Activity 全开也够，
   每个槽只有 6 个指针（24B），12 个也才 288B，不值得为省这点内存
   冒"顶栏建不出来"的风险 —— 槽位耗尽时 StatusBar_create 返回 nullptr，
   屏不崩，但会**没有返回按钮**，用户就困在里面了。 */
constexpr int MAX_BARS = 12;

struct Bar {
  lv_obj_t* bar;
  lv_obj_t* power;    /* 电池右侧的供电/电量文本 */
  lv_obj_t* time;
  lv_obj_t* wifi;
  lv_obj_t* bt;
  lv_obj_t* tasks;    /* 后台运行指示（只在桌面顶栏有） */
  lv_obj_t* taskCnt;  /* 后台数量文本 */
  int wifiLevel;      /* -1 = 还没画过，强制首帧画一次 */
};

Bar s_bars[MAX_BARS];

Bar* allocBar() {
  for (int i = 0; i < MAX_BARS; i++) {
    if (s_bars[i].bar == nullptr) {
      s_bars[i].power = nullptr;
      s_bars[i].time = nullptr;
      s_bars[i].wifi = nullptr;
      s_bars[i].bt = nullptr;
      s_bars[i].tasks = nullptr;
      s_bars[i].taskCnt = nullptr;
      s_bars[i].wifiLevel = -1;   /* -1 = 强制首帧画一次 */
      return &s_bars[i];
    }
  }
  return nullptr;
}

void bar_delete_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  lv_obj_t* obj = lv_event_get_target(e);
  for (int i = 0; i < MAX_BARS; i++) {
    if (s_bars[i].bar == obj) {
      /* 聚合初始化只给到 wifiLevel 之前的字段，新增字段（tasks/taskCnt）
         会值初始化为 nullptr —— 但聚合列表必须跟着字段顺序写全，漏一个就
         会把 -1 塞进指针里（编译器会报 invalid conversion）。 */
      s_bars[i].bar = nullptr;
      s_bars[i].power = nullptr;
      s_bars[i].time = nullptr;
      s_bars[i].wifi = nullptr;
      s_bars[i].bt = nullptr;
      s_bars[i].tasks = nullptr;
      s_bars[i].taskCnt = nullptr;
      s_bars[i].wifiLevel = -1;
      return;
    }
  }
}

/* ── 真实 FPS ──
 * 之前用「lv_timer_handler 被调用的次数」当帧数，那是主循环迭代次数，不是屏幕刷新次数，
 * 显示出来会有几百 fps 的虚高值（主循环 delay(5) 只是让 CPU 喘气，跟刷新无关）。
 * 真实刷新次数只能从显示驱动的 monitor_cb 拿：LVGL 只在真的重绘了脏区域之后才调它
 * （lv_refr.c: `if(disp_refr->inv_p != 0)`），没重绘就不算。
 * 屏幕静止时这个数就是 0 —— 那是对的，因为画面本来就没变。 */
volatile uint32_t g_refreshes = 0;
volatile uint32_t g_renderMs = 0;
uint32_t g_lastUpdate = 0;

void monitor_cb(lv_disp_drv_t* drv, uint32_t ms, uint32_t px) {
  (void)drv; (void)px;
  g_refreshes++;
  g_renderMs += ms;
}

/* RSSI → 信号格数 0..3（0 表示极弱/未连接，格子仍以暗色画出） */
int rssiToLevel(int rssi) {
  if (rssi >= -55) return 3;
  if (rssi >= -67) return 2;
  if (rssi >= -78) return 1;
  return 0;
}

void update_cb(lv_timer_t* t) {
  (void)t;
  uint32_t now = millis();
  uint32_t elapsed = now - g_lastUpdate;
  if (elapsed == 0) elapsed = 1;

  /* 真实刷新帧率（不是主循环次数）*/
  uint32_t fps = (uint32_t)((uint64_t)g_refreshes * 1000 / elapsed);
  uint32_t avgMs = g_refreshes ? (uint32_t)(g_renderMs / g_refreshes) : 0;
  g_refreshes = 0;
  g_renderMs = 0;
  g_lastUpdate = now;

  /* 每 10s 往串口打一次真实帧率（LVGL pool 挪到 PSRAM 之后要用它验证掉没掉帧）。
     屏幕静止时 fps=0 是正常的：没脏区就没重绘。 */
  static uint8_t s_perfTick = 0;
  if ((++s_perfTick % 10) == 0) {
    Serial.printf("[Perf] fps=%u avgRender=%ums DRAM free=%u\n",
                  (unsigned)fps, (unsigned)avgMs,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  }

  int pct = StatusBar_batteryPercent();
  char buf[32];
  if (pct >= 0) {
    snprintf(buf, sizeof(buf), "%d%%  %ufps", pct, (unsigned)fps);
  } else {
    /* 无电量检测硬件：电池图标按满格（USB 供电）画，文字只标供电来源 + FPS。
       ⚠️ 这里必须用 ASCII：状态栏字体是 lv_font_montserrat_16（纯拉丁），
       写中文会渲染成两个豆腐块（之前写的 "USB 静止" 就是这个毛病）。 */
    snprintf(buf, sizeof(buf), "USB %ufps", (unsigned)fps);
  }

  /* 时间：12 小时制 + AM/PM（字体同上，只能用 ASCII）。
     时钟源是离线软时钟 or NTP —— 见 NtpTime。 */
  time_t now_t = time(nullptr);
  struct tm tmv;
  localtime_r(&now_t, &tmv);
  int h12 = tmv.tm_hour % 12;
  if (h12 == 0) h12 = 12;
  char tbuf[16];
  snprintf(tbuf, sizeof(tbuf), "%d:%02d %s", h12, tmv.tm_min,
           (tmv.tm_hour < 12) ? "AM" : "PM");

  /* WiFi：按 RSSI 点亮格子，未点亮的格子是暗色（仍可见）。
     不再整图标压 40% 透明度 —— 那样弱信号时连格子都看不清了。 */
  bool wifiOn = (WiFi.status() == WL_CONNECTED);
  int level = wifiOn ? rssiToLevel(WiFi.RSSI()) : 0;

  /* 蓝牙：尚未接入 BLE，恒为半透明（接入后改 StatusBar_bluetoothOn 即可） */
  lv_opa_t btOpa = StatusBar_bluetoothOn() ? LV_OPA_COVER : LV_OPA_40;

  for (int i = 0; i < MAX_BARS; i++) {
    Bar& b = s_bars[i];
    if (!b.bar) continue;
    /* 屏被 delete 时 bar_delete_cb 会清槽位；这里再验一次，防 LVGL 未回调的
       极端路径（例如整棵对象树被 lv_obj_del 时事件被裁剪）。 */
    if (!lv_obj_is_valid(b.bar)) { b.bar = nullptr; continue; }

    if (b.power && lv_obj_is_valid(b.power)) lv_label_set_text(b.power, buf);
    if (b.time && lv_obj_is_valid(b.time)) lv_label_set_text(b.time, tbuf);
    if (b.wifi && lv_obj_is_valid(b.wifi) && level != b.wifiLevel) {
      b.wifiLevel = level;
      /* ⚠️ 尺寸必须和创建时一致！画布是 WIFI_ICON_SIZE(22)×22，
         这里以前写死 16 → 按 16 的坐标系往 22 的画布里画，图标缩在左上角
         看着"偏上"。尺寸常量必须同源。 */
      icon_wifi_set_level(b.wifi, WIFI_ICON_SIZE, level);
    }
    if (b.bt && lv_obj_is_valid(b.bt)) lv_obj_set_style_opa(b.bt, btOpa, 0);

    /* 后台指示：只在桌面顶栏（有 tasks 槽位）更新。
       没有后台应用时整个指示隐藏 —— master 2026-09-24 定的语义。 */
    if (b.tasks && lv_obj_is_valid(b.tasks)) {
      NavRunningInfo infos[16];
      int running = nav_running_list(infos, 16);
      bool show = running > 0;
      if (show) {
        char cb[8];
        snprintf(cb, sizeof(cb), "%d", running);
        if (b.taskCnt && lv_obj_is_valid(b.taskCnt)) {
          lv_label_set_text(b.taskCnt, cb);
          lv_obj_clear_flag(b.taskCnt, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(b.tasks, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(b.tasks, LV_OBJ_FLAG_HIDDEN);
        if (b.taskCnt && lv_obj_is_valid(b.taskCnt))
          lv_obj_add_flag(b.taskCnt, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
}

}  // namespace

int StatusBar_batteryPercent() {
  /* 之前写死返回 -1（"本板没有电量检测硬件"）。这个结论证据不足：
     厂家 IO 表里 IO35/36/37 无功能标注，板上也焊了八角芯片。
     改成实际去 I2C 上探电源 IC：探到就用它，探不到才回退 -1（行为不变）。
     诊断：串口 `bat`。详见 docs/02 §1.5。 */
  return Battery::percent();
}

bool StatusBar_bluetoothOn() {
  /* TODO：接入 BLE 后改为真实状态（如 NimBLEDevice::getInitialized()）。 */
  return false;
}

/* ── 顶栏返回 ──
 * ⚠️ 绝不能在点击事件里直接 nav_back_home()：那会在 LVGL 事件分发过程中
 *    lv_obj_del() 掉当前屏（也就是回调所属的那棵对象树），分发返回后 LVGL
 *    仍要访问它 → LoadProhibited。浏览器链接跳转踩过同一个坑（那边用的是
 *    g_linkPending 延迟通道）。
 *    这里改用一次性 lv_timer：延迟到下一个 tick 才真正执行，此刻事件分发
 *    早已结束，删除是安全的。period=1ms 保证几乎无感。 */
static void back_async_cb(lv_timer_t* t) {
  (void)t;
  nav_back_home();
}

/* 点电池区 -> 打开后台管理。同样是延迟一拍：
   在点击事件里 nav_open() 会新建一棵对象树，虽然不删当前屏，但 LVGL 事件
   分发还没结束就动全局屏指针不安全 —— 跟返回键走同一条路。 */
static void taskmgr_async_cb(lv_timer_t* t) {
  (void)t;
  if (!nav_taskmgr) nav_open(&nav_taskmgr);
  if (nav_taskmgr) lv_scr_load(nav_taskmgr);
}

static void bat_click_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_timer_t* t = lv_timer_create(taskmgr_async_cb, 1, nullptr);
  if (t) lv_timer_set_repeat_count(t, 1);
}

static void back_click_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_timer_t* t = lv_timer_create(back_async_cb, 1, nullptr);
  if (t) lv_timer_set_repeat_count(t, 1);   /* 执行一次后 LVGL 自动删除 */
}

lv_obj_t* StatusBar_create(lv_obj_t* parent, const char* title) {
  Bar* b = allocBar();
  if (!b) {
    Serial.println("[StatusBar] no free slot (MAX_BARS reached)");
    return nullptr;
  }

  lv_obj_t* bar = lv_obj_create(parent);
  b->bar = bar;
  lv_obj_set_size(bar, SCREEN_WIDTH, BAR_H);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  /* 屏销毁时自动注销槽位，杜绝定时器写悬空指针 */
  lv_obj_add_event_cb(bar, bar_delete_cb, LV_EVENT_DELETE, nullptr);

  /* ── 左：桌面=电池 / APP=返回三角 + 当前 APP 名称（iOS 风格导航栏）──
     两个状态互斥，占的是同一个位置（master 2026-09-24：进软件后隐藏电池，
     换成返回按钮）。APP 状态下整块是一个可点击容器，三角形和文字都加
     EVENT_BUBBLE，所以点三角、点标题都能返回。
     title 为 nullptr 表示根节点（Launcher）：已经在桌面，没有"上一级"可退，
     这个位置就让给电池。 */
  if (title) {
    lv_obj_t* backBox = lv_obj_create(bar);
    lv_obj_set_size(backBox, LV_SIZE_CONTENT, BAR_H);
    lv_obj_align(backBox, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_bg_opa(backBox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(backBox, 0, 0);
    lv_obj_set_style_pad_left(backBox, 6, 0);
    lv_obj_set_style_pad_right(backBox, 10, 0);
    lv_obj_set_style_pad_top(backBox, 0, 0);
    lv_obj_set_style_pad_bottom(backBox, 0, 0);
    lv_obj_set_flex_flow(backBox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(backBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(backBox, 6, 0);
    lv_obj_clear_flag(backBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(backBox, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(backBox, back_click_cb, LV_EVENT_CLICKED, nullptr);

    /* 24：以前给 18，三角形缩成一丁点。BAR_H=28，24 的图标还有 2px 上下留白，
       配合实心三角，远看也能一眼认出是返回键。 */
    lv_obj_t* arrow = icon_create(backBox, Icon::Back, 24);
    lv_obj_add_flag(arrow, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* 标题是中文，必须用 font_zh_16。montserrat 是纯拉丁字体，
       塞汉字会渲染成豆腐块（状态栏以前踩过）。 */
    lv_obj_t* titleLab = lv_label_create(backBox);
    lv_label_set_text(titleLab, title);
    lv_obj_set_style_text_color(titleLab, lv_color_white(), 0);
    lv_obj_set_style_text_font(titleLab, &font_zh_16, 0);
    lv_obj_add_flag(titleLab, LV_OBJ_FLAG_EVENT_BUBBLE);
  } else {
    /* 桌面（根节点）：左边这个位置放电池。
       进入 APP 之后电池隐藏 —— 同一个位置让给返回按钮（master 2026-09-24：
       「电池隐藏起来，换成返回按钮」）。电量其实没丢：右侧供电文本里
       本来就带百分比（"78%  60fps"），所以 App 里照样看得到电量。 */
    /* 电池 + 后台指示包进一个可点容器：点整块 = 打开后台管理。
       ⚠️ 电池图标本身是 canvas（不可点），所以必须有这个容器来接事件。 */
    lv_obj_t* batBox = lv_obj_create(bar);
    lv_obj_set_size(batBox, LV_SIZE_CONTENT, BAR_H);
    lv_obj_align(batBox, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_flex_flow(batBox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(batBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(batBox, 6, 0);
    lv_obj_set_style_pad_left(batBox, 6, 0);
    lv_obj_set_style_pad_right(batBox, 8, 0);
    lv_obj_set_style_bg_opa(batBox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(batBox, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(batBox, lv_color_hex(0x161616), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(batBox, 0, 0);
    lv_obj_clear_flag(batBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(batBox, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(batBox, bat_click_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* batRoot = icon_create(batBox, Icon::Battery, 22);
    lv_obj_add_flag(batRoot, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* 后台运行指示：有 Activity 在内存里才显示（update_cb 每秒更新可见性） */
    b->tasks = icon_create(batBox, Icon::Tasks, 20);
    lv_obj_add_flag(b->tasks, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(b->tasks, LV_OBJ_FLAG_HIDDEN);

    b->taskCnt = lv_label_create(batBox);
    lv_label_set_text(b->taskCnt, "");
    lv_obj_set_style_text_color(b->taskCnt, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(b->taskCnt, &lv_font_montserrat_14, 0);
    lv_obj_add_flag(b->taskCnt, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(b->taskCnt, LV_OBJ_FLAG_HIDDEN);
  }

  /* 中：时间 */
  b->time = lv_label_create(bar);
  lv_label_set_text(b->time, "--:--");
  lv_obj_set_style_text_color(b->time, lv_color_white(), 0);
  lv_obj_set_style_text_font(b->time, &lv_font_montserrat_16, 0);
  lv_obj_align(b->time, LV_ALIGN_TOP_MID, 0, 4);

  /* ── 右：从右往左 蓝牙 → WiFi → 供电文本 ──
     供电文本固定宽度 76px + 右对齐：宽度不随内容变化，
     WiFi/蓝牙的位置就稳定，不会因为 60fps → 0fps 而左右跳。
     电池不在这里：它只在桌面顶栏的左边出现（根节点分支）。 */
  b->bt = icon_create(bar, Icon::Bluetooth, BT_ICON_SIZE);
  lv_obj_align(b->bt, LV_ALIGN_RIGHT_MID, -10, 0);
  b->wifi = icon_create_wifi(bar, WIFI_ICON_SIZE, 0);
  /* y 偏移 +2：三道弧的开口朝上，视觉重心比几何中心偏高，往下压一点才平衡。 */
  lv_obj_align(b->wifi, LV_ALIGN_RIGHT_MID, -40, 2);

  b->power = lv_label_create(bar);
  lv_label_set_text(b->power, "--");
  lv_obj_set_width(b->power, 76);
  lv_obj_set_style_text_align(b->power, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(b->power, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_font(b->power, &lv_font_montserrat_14, 0);
  lv_obj_align_to(b->power, b->wifi, LV_ALIGN_OUT_LEFT_MID, -8, 0);

  /* 接管显示驱动的 monitor_cb，才能拿到「真实刷新次数」。
     注意别覆盖已有回调（本项目 display 初始化没设，所以为空）。 */
  lv_disp_t* disp = lv_disp_get_default();
  if (disp && disp->driver && !disp->driver->monitor_cb)
    disp->driver->monitor_cb = monitor_cb;

  /* 定时器全局只建一次：以前每建一条 bar 就多一个定时器，全都刷新同一组
     全局指针，屏销毁后必然踩到已释放内存。 */
  static bool timerCreated = false;
  if (!timerCreated) {
    g_lastUpdate = millis();
    lv_timer_create(update_cb, 1000, nullptr);
    timerCreated = true;
  }
  update_cb(nullptr);  // 立即填一次，避免首秒空白

  return bar;
}
