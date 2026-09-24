# -*- coding: utf-8 -*-
"""状态栏加两个能力（二级菜单需要）：
   1) 自定义返回动作 —— 设置页的二级菜单里"返回"要回到上一级，不是回桌面
   2) 事后改标题     —— 切子菜单时顶栏要跟着变
"""
import io
GT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
p = GT + r"\src\app\status_bar.cpp"
t = io.open(p, encoding="utf-8", newline=None).read()


def rep(old, new, tag):
    global t
    assert t.count(old) == 1, "%s: count=%d" % (tag, t.count(old))
    t = t.replace(old, new)
    print("  ok:", tag)


# 1) Bar 结构里存返回回调
rep("""struct Bar {
  lv_obj_t* bar;""",
    """struct Bar {
  lv_obj_t* bar;
  lv_event_cb_t backCb;   /* nullptr = 用默认的 nav_back_home（回桌面） */""", "Bar struct")

rep("""      s_bars[i].wifiLevel = -1;   /* -1 = 强制首帧画一次 */
      return &s_bars[i];""",
    """      s_bars[i].wifiLevel = -1;   /* -1 = 强制首帧画一次 */
      s_bars[i].backCb = nullptr;
      return &s_bars[i];""", "allocBar")

# 2) 返回动作走 Bar 里存的回调
rep("""static void back_async_cb(lv_timer_t* t) {
  (void)t;
  nav_back_home();
}""",
    """static void back_async_cb(lv_timer_t* t) {
  /* 回调由 user_data 带进来：设置页的二级菜单要"回上一级"，
     不能一律回桌面。为 nullptr 时保持原行为。 */
  lv_event_cb_t cb = (lv_event_cb_t)lv_timer_get_user_data(t);
  if (cb) {
    lv_timer_t* dummy = nullptr;
    (void)dummy;
    /* 复用同一个签名：设置页的 handler 忽略 e 即可 */
    cb(nullptr);
  } else {
    nav_back_home();
  }
}""", "back_async_cb")

rep("""static void back_click_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_timer_t* t = lv_timer_create(back_async_cb, 1, nullptr);
  if (t) lv_timer_set_repeat_count(t, 1);   /* 执行一次后 LVGL 自动删除 */
}""",
    """static void back_click_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_event_cb_t cb = (lv_event_cb_t)lv_event_get_user_data(e);
  lv_timer_t* t = lv_timer_create(back_async_cb, 1, (void*)cb);
  if (t) lv_timer_set_repeat_count(t, 1);   /* 执行一次后 LVGL 自动删除 */
}""", "back_click_cb")

rep("""    lv_obj_add_event_cb(backBox, back_click_cb, LV_EVENT_CLICKED, nullptr);""",
    """    lv_obj_add_event_cb(backBox, back_click_cb, LV_EVENT_CLICKED, (void*)b->backCb);""",
    "register back cb")

# 3) create 签名加一个可选参数
rep("""lv_obj_t* StatusBar_create(lv_obj_t* parent, const char* title) {
  Bar* b = allocBar();""",
    """lv_obj_t* StatusBar_createEx(lv_obj_t* parent, const char* title, lv_event_cb_t backCb) {
  Bar* b = allocBar();""", "create signature")

rep("""  lv_obj_t* bar = lv_obj_create(parent);
  b->bar = bar;""",
    """  b->backCb = backCb;
  lv_obj_t* bar = lv_obj_create(parent);
  b->bar = bar;""", "store backCb")

# 4) 尾部加 StatusBar_create 与 setTitle 的实现
anchor = "int StatusBar_batteryPercent() {"
assert t.count(anchor) == 1
t = t.replace(anchor, """lv_obj_t* StatusBar_create(lv_obj_t* parent, const char* title) {
  return StatusBar_createEx(parent, title, nullptr);
}

/* 事后改标题（切二级菜单时用）。
   布局：bar 的第一个孩子是 backBox；backBox 里 [0]=返回三角、[1]=标题 label。
   ⚠️ 必须校验第二个孩子确实是 label —— root 顶栏（title==nullptr）那块是
      电池容器，孩子是 canvas，直接 set_text 会把文本写到 canvas 上。 */
bool StatusBar_setTitle(lv_obj_t* bar, const char* title) {
  if (!bar || !title) return false;
  lv_obj_t* box = lv_obj_get_child(bar, 0);
  if (!box) return false;
  lv_obj_t* lab = lv_obj_get_child(box, 1);
  if (!lab || !lv_obj_check_type(lab, &lv_label_class)) return false;
  lv_label_set_text(lab, title);
  return true;
}

""" + anchor)

io.open(p, "w", encoding="utf-8", newline="").write(t)

# ── header ─────────────────────────────────────────────────────────────
p = GT + r"\src\app\status_bar.h"
t = io.open(p, encoding="utf-8", newline=None).read()
old = "lv_obj_t* StatusBar_create(lv_obj_t* parent, const char* title);"
new = ("""lv_obj_t* StatusBar_create(lv_obj_t* parent, const char* title);

/* 自定义返回动作版。二级菜单要"回上一级"而不是"回桌面"，就得用它。
   ⚠️ backCb 在**延迟一拍的 lv_timer** 里被调用（不在事件回调里），
      所以里面 nav_go / lv_obj_del 都安全；参数 e 恒为 nullptr，别去读它。 */
lv_obj_t* StatusBar_createEx(lv_obj_t* parent, const char* title, lv_event_cb_t backCb);

/* 切二级菜单时改顶栏标题。返回 false = 没改成（bar 为 root 顶栏或结构不符）。 */
bool StatusBar_setTitle(lv_obj_t* bar, const char* title);""")
assert t.count(old) == 1
io.open(p, "w", encoding="utf-8", newline="").write(t.replace(old, new))
print("status_bar patched")
