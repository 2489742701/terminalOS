#include "settings_menu.h"
#include "nav.h"
#include "status_bar.h"
#include "font_zh.h"
#include <Arduino.h>

namespace {

constexpr int ROW_H = 52;
constexpr int MAX_ROWS = 16;
constexpr int MAX_DEPTH = 4;

const SettingsPage* s_pages = nullptr;
int s_pageCount = 0;
lv_obj_t* s_container = nullptr;
lv_obj_t* s_bar = nullptr;

const char* s_stack[MAX_DEPTH];
int s_depth = 0;

/* 每行的右侧文字（值）label + 取值函数，供 refresh_values() 更新 */
lv_obj_t* s_valLabs[MAX_ROWS];
const char* (*s_valFns[MAX_ROWS])();
int s_valCount = 0;

/* 延迟渲染用的一次性定时器 */
lv_timer_t* s_renderTimer = nullptr;
const char* s_pendingPage = nullptr;

/* 前向声明：renderPage 里要给行挂 row_click_cb / 要调 requestRender */
void row_click_cb(lv_event_t* e);
void requestRender(const char* id);

/* ⚠️ 把 id 归一化成页表里那份静态字面量。
   为什么必须：串口 `setpage general` 传进来的是命令行缓冲区的指针，命令一结束
   缓冲就被下一条命令覆盖 —— 若把 arg 原样存进 s_stack，下次 back 时
   findPage 拿到的是 "setpage back"，找不到就静默返回（现象：back 什么也不做）。
   现在一律换成 s_pages[i].id（存在 Flash 里，生命周期与页表一致）。 */
const char* canonPageId(const char* id) {
  if (!id || !s_pages) return nullptr;
  for (int i = 0; i < s_pageCount; i++)
    if (strcmp(s_pages[i].id, id) == 0) return s_pages[i].id;
  return nullptr;
}

const SettingsPage* findPage(const char* id) {
  if (!id || !s_pages) return nullptr;
  for (int i = 0; i < s_pageCount; i++) {
    if (strcmp(s_pages[i].id, id) == 0) return &s_pages[i];
  }
  return nullptr;
}

void renderPage(const char* id) {
  const SettingsPage* pg = findPage(id);
  if (!pg || !s_container || !lv_obj_is_valid(s_container)) return;

  s_valCount = 0;
  lv_obj_clean(s_container);

  if (s_bar && lv_obj_is_valid(s_bar)) StatusBar_setTitle(s_bar, pg->title);

  int n = 0;
  for (const SettingsItem* it = pg->items; it && it->title; ++it) n++;
  Serial.printf("[Settings] page '%s' (%s), %d rows\n", pg->id, pg->title, n);

  int idx = 0;
  for (const SettingsItem* it = pg->items; it && it->title; ++it, ++idx) {
    if (idx >= MAX_ROWS) break;

    lv_obj_t* row = lv_obj_create(s_container);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    /* 让滑动能冒到 scr（左缘返回手势要能用）；行本身默认不可点 */
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* 分隔线：最后一行不画。用 border-bottom 而不是额外对象，省 N 个 obj */
    if (idx < n - 1) {
      lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
      lv_obj_set_style_border_width(row, 1, 0);
      lv_obj_set_style_border_color(row, lv_color_hex(0x222222), 0);
    }

    lv_obj_t* titleLab = lv_label_create(row);
    lv_label_set_text(titleLab, it->title);
    lv_obj_set_style_text_color(titleLab, lv_color_white(), 0);
    lv_obj_set_style_text_font(titleLab, &font_zh_16, 0);
    lv_obj_align(titleLab, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_add_flag(titleLab, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* ── 右侧内容 ── */
    if (it->type == SetType::Toggle) {
      lv_obj_t* sw = lv_switch_create(row);
      lv_obj_set_size(sw, 50, 26);
      lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -16, 0);
      lv_obj_add_event_cb(sw, it->cb, LV_EVENT_VALUE_CHANGED, NULL);
      lv_obj_add_flag(sw, LV_OBJ_FLAG_EVENT_BUBBLE);
      if (it->checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    } else if (it->type == SetType::Slider) {
      lv_obj_t* sl = lv_slider_create(row);
      lv_obj_set_width(sl, 230);
      lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -16, 0);
      lv_slider_set_range(sl, it->vmin, it->vmax);
      lv_slider_set_value(sl, it->vinit, LV_ANIM_OFF);
      lv_obj_set_style_bg_color(sl, lv_color_hex(0x333333), 0);
      lv_obj_set_style_bg_color(sl, lv_color_white(), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(sl, lv_color_white(), LV_PART_KNOB);
      lv_obj_add_event_cb(sl, it->cb, LV_EVENT_VALUE_CHANGED, NULL);
      lv_obj_add_flag(sl, LV_OBJ_FLAG_EVENT_BUBBLE);
    } else {
      /* Nav / ReadOnly / Action：右侧文字（+ Nav 的 >） */
      const char* v = it->valueFn ? it->valueFn() : nullptr;
      if (v && it->valueFn && s_valCount < MAX_ROWS) {
        lv_obj_t* valLab = lv_label_create(row);
        lv_label_set_text(valLab, v);
        lv_obj_set_style_text_color(valLab, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(valLab, &font_zh_16, 0);
        lv_obj_align(valLab, LV_ALIGN_RIGHT_MID,
                     (it->type == SetType::Nav) ? -34 : -16, 0);
        lv_obj_add_flag(valLab, LV_OBJ_FLAG_EVENT_BUBBLE);
        s_valLabs[s_valCount] = valLab;
        s_valFns[s_valCount] = it->valueFn;
        s_valCount++;
      }
      if (it->type == SetType::Nav) {
        /* ">" 而不是 "›"：font_zh_16 里没有 U+203A，会变豆腐块 */
        lv_obj_t* chev = lv_label_create(row);
        lv_label_set_text(chev, ">");
        lv_obj_set_style_text_color(chev, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(chev, &lv_font_montserrat_20, 0);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -14, 0);
        lv_obj_add_flag(chev, LV_OBJ_FLAG_EVENT_BUBBLE);
      }
    }

    /* ── 整行点击 ── */
    if (it->type == SetType::Nav || it->type == SetType::Action) {
      lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
      lv_obj_set_style_bg_color(row, lv_color_hex(0x1a1a1a), LV_STATE_PRESSED);
      /* user_data 传 item 指针：切页要读 pageId，跳屏要调 cb */
      lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, (void*)it);
    }
  }
}

void row_click_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const SettingsItem* it = (const SettingsItem*)lv_event_get_user_data(e);
  if (!it) return;

  if (it->cb) {
    it->cb(e);                 // Nav 覆盖了 cb => 跳别的屏，不进栈
    return;
  }
  if (it->type == SetType::Nav && it->pageId) {
    const char* pid = canonPageId(it->pageId);
    if (pid && s_depth < MAX_DEPTH - 1) { s_depth++; s_stack[s_depth] = pid; }
    if (pid) requestRender(pid);
  }
}

void render_timer_cb(lv_timer_t* t) {
  (void)t;
  s_renderTimer = nullptr;
  renderPage(s_pendingPage);
}

/* ⚠️ 必须延迟：在行点击事件里 lv_obj_clean 会删掉正在分发事件的那一行。 */
void requestRender(const char* id) {
  s_pendingPage = id;
  if (s_renderTimer) return;          // 已排队；s_pendingPage 已被更新为最新
  s_renderTimer = lv_timer_create(render_timer_cb, 1, nullptr);
  if (s_renderTimer) lv_timer_set_repeat_count(s_renderTimer, 1);
}

/* 屏销毁：注销定时器 + 断指针，杜绝定时器写悬空指针 */
void scr_delete_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  if (s_renderTimer) {
    lv_timer_del(s_renderTimer);
    s_renderTimer = nullptr;
  }
  s_container = nullptr;
  s_bar = nullptr;
  s_pages = nullptr;
  s_pageCount = 0;
  s_depth = 0;
  s_valCount = 0;
}

}  // namespace

void settings_menu_begin(lv_obj_t* scr, lv_obj_t* bar,
                         const SettingsPage* pages, int pageCount,
                         const char* rootId) {
  s_pages = pages;
  s_pageCount = pageCount;
  s_bar = bar;
  s_depth = 0;
  s_valCount = 0;
  /* 栈底就是 root：s_stack[s_depth] 恒等于"当前页"，back 只要 -- 再取即可 */
  s_stack[0] = rootId;

  s_container = lv_obj_create(scr);
  lv_obj_set_size(s_container, 456, 380);
  lv_obj_set_pos(s_container, 12, 40);
  lv_obj_set_style_bg_color(s_container, lv_color_hex(0x101010), 0);
  lv_obj_set_style_bg_opa(s_container, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_container, 0, 0);
  lv_obj_set_style_radius(s_container, 12, 0);
  lv_obj_set_style_pad_all(s_container, 0, 0);
  lv_obj_set_flex_flow(s_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_container, LV_DIR_VER);
  /* ⚠️ 不关就是一条浅色滚动条（浏览器那条"白线"就是这个） */
  lv_obj_set_scrollbar_mode(s_container, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(s_container, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_add_event_cb(scr, scr_delete_cb, LV_EVENT_DELETE, NULL);

  renderPage(rootId);
}

void settings_menu_refresh_values() {
  for (int i = 0; i < s_valCount; i++) {
    if (!s_valLabs[i] || !lv_obj_is_valid(s_valLabs[i])) continue;
    if (!s_valFns[i]) continue;
    const char* v = s_valFns[i]();
    if (v) lv_label_set_text(s_valLabs[i], v);
  }
}

/* ⚠️ 签名必须是 lv_event_cb_t：StatusBar_createEx 把返回值当事件回调，
   在延迟一拍的 lv_timer 里调用（e 恒为 nullptr，别去读它）。 */
void settings_menu_back(lv_event_t* e) {
  (void)e;
  if (!s_container || !lv_obj_is_valid(s_container)) {
    nav_back_home();
    return;
  }
  if (s_depth > 0) {
    s_depth--;
    Serial.printf("[Settings] back -> '%s' (depth %d)\n", s_stack[s_depth], s_depth);
    requestRender(s_stack[s_depth]);
  } else {
    nav_back_home();
  }
}

/* 当前深度（0 = 在根页）。给外部判断"左缘滑动该回桌面还是上一级"。 */
int settings_menu_depth() { return s_depth; }

/* 串口诊断用：直接跳到某个页（等价于点那一行）。用法：setpage general
   ⚠️ s_pages/s_stack/requestRender 都在匿名 namespace 里，但同一个 TU 内
      匿名 namespace 的成员在命名空间外依然可见，所以写在这里没问题。 */
void settings_menu_goto(const char* id) {
  if (!id || !*id) { Serial.println("[Settings] usage: setpage <id>"); return; }
  if (!s_pages || !s_container) { Serial.println("[Settings] 设置页未打开"); return; }
  const char* pid = canonPageId(id);
  if (!pid) { Serial.printf("[Settings] no page '%s'\n", id); return; }
  if (s_depth < MAX_DEPTH - 1) { s_depth++; s_stack[s_depth] = pid; }
  requestRender(pid);
}
