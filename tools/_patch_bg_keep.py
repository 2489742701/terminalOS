# -*- coding: utf-8 -*-
"""修「切后台后应用被自动结束」。

根因（串口实锤）：
    navlist -> running=2 (clock 前台, weather 8468B)
    back    -> release clock / release weather     ← 一次返回把**所有**都清了
    navlist -> running=0
nav_back_home() / nav_back_home_anim() 里调 nav_release_all_except(nav_launcher)，
于是不管用户在后台管理里点不点"结束"，回桌面那一刻应用就已经没了。

改法：
  1. 回桌面 = 切后台，只 lv_scr_load(nav_launcher)，**不再释放任何 Activity**；
  2. 内存由 LRU 兜底：nav_open 前若后台超过 MAX_BG 个、或 DRAM 低于阈值，
     回收最久未用的非前台 Activity（launcher 打开浏览器仍然独占，那条不动）。
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\nav.cpp"
src = io.open(P, encoding="utf-8").read()

# ── 1) ActivityEntry 加 lastUsed ──────────────────────────────────────────
old = """  OnDestroy  onDestroy;   // nullptr = 只需 lv_obj_del（对象树自带内存由 LVGL 回收）
  uint32_t   usedBytes;   // 创建时吃掉的内部 DRAM（后台管理页显示用）
};"""
new = """  OnDestroy  onDestroy;   // nullptr = 只需 lv_obj_del（对象树自带内存由 LVGL 回收）
  uint32_t   usedBytes;   // 创建时吃掉的内部 DRAM（后台管理页显示用）
  uint32_t   lastUsed;    // 最近一次进前台的 millis()，LRU 回收用（0 = 从没进过）
};"""
assert src.count(old) == 1
src = src.replace(old, new, 1)

# ── 2) 加 LRU 回收函数（放在 nav_open 之前）────────────────────────────────
anchor = "lv_obj_t* nav_open(lv_obj_t** target) {"
assert src.count(anchor) == 1

trim = '''/* ── 后台 LRU 兜底 ──────────────────────────────────────────────────────────
 * 回桌面不再清后台之后（见 nav_back_home），内存不能无限涨：
 * 超过 MAX_BG 个后台、或者 DRAM 低于 MIN_FREE 时，回收**最久没进前台**的那个。
 * ⚠️ 前台和 keep 那个绝不回收（删当前屏必崩）；canRelease 守卫不通过的也跳过。
 * ⚠️ 浏览器太重，Launcher 进它时仍然走 nav_release_all_except 独占，不走这里。 */
static void trimBackground(lv_obj_t* keep) {
  const int      MAX_BG   = 4;              /* 最多留 4 个后台 */
  const uint32_t MIN_FREE = 48 * 1024;      /* DRAM 低于 48KB 就开始收（浏览器任务栈要 8KB） */

  lv_obj_t* act = lv_scr_act();
  int loaded = 0;
  ActivityEntry* victim = nullptr;
  for (int i = 0; i < s_count; i++) {
    ActivityEntry& e = s_table[i];
    if (!*(e.screen)) continue;                                  // 没在内存里
    if (*(e.screen) == act || *(e.screen) == keep) continue;      // 前台 / 马上要开的
    loaded++;
    if (!victim || e.lastUsed < victim->lastUsed) victim = &e;
  }
  if (!victim) return;
  if (loaded <= MAX_BG &&
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= MIN_FREE) return;
  if (victim->canRelease && !victim->canRelease()) {
    Serial.printf("[Nav] trim skip %s (release guard)\\n", victim->name);
    return;
  }

  uint32_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (victim->onDestroy) victim->onDestroy();
  lv_obj_del(*(victim->screen));
  *(victim->screen) = nullptr;
  Serial.printf("[Nav] trim %s (bg=%d): DRAM %u -> %u\\n", victim->name, loaded,
                (unsigned)before,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

'''
src = src.replace(anchor, trim + anchor, 1)

# ── 3) nav_open：先 trim，创建后记 lastUsed ───────────────────────────────
old2 = """lv_obj_t* nav_open(lv_obj_t** target) {
  if (!target) return nullptr;
  if (*target) return *target;  // 已加载

  ActivityEntry* e = find(target);
  if (!e || !e->create) return nullptr;
"""
new2 = """lv_obj_t* nav_open(lv_obj_t** target) {
  if (!target) return nullptr;
  if (*target) {                       // 已加载：刷新 LRU 时间就够了
    ActivityEntry* ex = find(target);
    if (ex) ex->lastUsed = millis();
    return *target;
  }

  ActivityEntry* e = find(target);
  if (!e || !e->create) return nullptr;

  /* 开新的之前先按 LRU 腾一腾，别等到创建失败才想起来收 */
  trimBackground(*target);
"""
assert src.count(old2) == 1
src = src.replace(old2, new2, 1)

old3 = """  Serial.printf("[Nav] open %s: %s, DRAM %u -> %u\\n", e->name,
                scr ? "ok" : "FAILED", (unsigned)before, (unsigned)after);
  return scr;"""
new3 = """  Serial.printf("[Nav] open %s: %s, DRAM %u -> %u\\n", e->name,
                scr ? "ok" : "FAILED", (unsigned)before, (unsigned)after);
  if (scr) e->lastUsed = millis();
  return scr;"""
assert src.count(old3) == 1
src = src.replace(old3, new3, 1)

# ── 4) 回桌面：只切屏，不清后台 ───────────────────────────────────────────
old4 = """static void release_after_anim_cb(lv_timer_t* t) {
  (void)t;
  /* 动画已经跑完，此刻旧屏不再参与渲染 —— 可以安全销毁了。 */
  nav_release_all_except(nav_launcher);
}

void nav_back_home_anim() {
  if (!nav_launcher) { nav_back_home(); return; }
  if (!g_uiAnim) { nav_back_home(); return; }   /* 关动画：一步到位 */
  lv_scr_load_anim(nav_launcher, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 240, 0, false);
  nav_lock_until = lv_tick_get() + 240 + 300;
  lv_timer_t* t = lv_timer_create(release_after_anim_cb, 280, nullptr);
  if (t) lv_timer_set_repeat_count(t, 1);
}

void nav_back_home() {
  /* ⚠️ 顺序必须是「先切屏，再销毁」。
     lv_obj_del() 掉当前活动屏幕 = LVGL 还在用这棵对象树就把它释放了，
     实测直接 Guru Meditation (LoadProhibited)。
     也不能用带动画的切换：动画期间旧屏仍参与渲染，删了照样崩。
     所以这里用无动画 lv_scr_load 立即切换，换取能马上安全销毁。 */
  if (nav_launcher) lv_scr_load(nav_launcher);
  nav_release_all_except(nav_launcher);
}"""
new4 = """void nav_back_home_anim() {
  if (!nav_launcher) { nav_back_home(); return; }
  if (!g_uiAnim) { nav_back_home(); return; }   /* 关动画：一步到位 */
  lv_scr_load_anim(nav_launcher, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 240, 0, false);
  nav_lock_until = lv_tick_get() + 240 + 300;
  /* ⚠️ 这里**不再**延迟销毁旧屏（2026-09-25 改）：
     回桌面 = 切后台，应用要留在内存里，否则后台管理页里"点不点结束都会被结束"
     —— 实测一次 back 就把 clock + weather 一起 release 了。
     内存压力交给 nav_open() 里的 trimBackground() 按 LRU 兜底。 */
}

void nav_back_home() {
  /* ⚠️ 只切屏，不销毁（同上）。真要销毁走 nav_close() / nav_close_all()
     （后台管理页）或 nav_release_all_except()（Launcher 进浏览器时的独占）。 */
  if (nav_launcher) lv_scr_load(nav_launcher);
}"""
assert src.count(old4) == 1
src = src.replace(old4, new4, 1)

io.open(P, "w", encoding="utf-8", newline="").write(src)
print("nav.cpp ok")
