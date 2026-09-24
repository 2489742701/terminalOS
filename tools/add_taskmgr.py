#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""后台管理（任务管理器）接线：nav 加运行列表/关闭接口 + 注册 Activity + 桌面图标。"""
import io
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(rel):
    with io.open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def write(rel, s):
    with io.open(os.path.join(ROOT, rel), "w", encoding="utf-8", newline="") as f:
        f.write(s)


def edit(rel, pairs):
    s = read(rel)
    nl = "\r\n" if "\r\n" in s else "\n"
    orig = s
    for old, new, tag in pairs:
        old = old.replace("\n", nl)
        new = new.replace("\n", nl)
        if new in s:
            print("SKIP: " + tag)
            continue
        if old not in s:
            print("MISS: " + tag + "  <-- 没匹配上！")
            continue
        s = s.replace(old, new, 1)
        print("OK: " + tag)
    if s != orig:
        write(rel, s)


# ── nav.h：声明 ──
edit("src/app/nav.h", [
    ("extern lv_obj_t* nav_desktop;  // 桌面图标管理（设置里进入）\n",
     "extern lv_obj_t* nav_desktop;  // 桌面图标管理（设置里进入）\n"
     "extern lv_obj_t* nav_taskmgr;  // 后台管理（任务管理器）\n",
     "nav.h decl"),

    ("// 回到 Launcher：先销毁其他所有 Activity，再切屏（唯一的\"退出应用\"入口）\nvoid nav_back_home();\n",
     """// 回到 Launcher：先销毁其他所有 Activity，再切屏（唯一的"退出应用"入口）
void nav_back_home();

/* ── 后台管理（任务管理器）接口 ──────────────────────────────────────────────
   应用本来就是按需创建的，但切屏过程中仍会有几棵对象树留在内存里。
   后台页要把它们列出来让用户能关掉 —— 所以 nav 得能回答"谁还在"。 */
struct NavRunningInfo {
  const char* id;      // Activity 名（"browser"），静态字符串
  uint32_t bytes;      // 创建时吃掉的内部 DRAM（0 = 未计量）
  bool current;        // 是否当前前台（前台不可关：删当前屏必崩）
};

// 列出所有仍在内存里的 Activity（Launcher 不算），返回条数
int nav_running_list(NavRunningInfo* out, int max);

// 关掉指定 Activity。前台 / Launcher / 释放守卫不通过 -> 返回 false
bool nav_close(const char* id);

// 关掉除 Launcher 和前台之外的所有 Activity，返回实际关掉的个数
int nav_close_all();
""",
     "nav.h api"),
])

# ── nav.cpp：计量 + 注册 + 实现 ──
edit("src/app/nav.cpp", [
    ('#include "desktop_screen.h"\n',
     '#include "desktop_screen.h"\n#include "taskmgr_screen.h"\n',
     "nav.cpp include"),

    ("""struct ActivityEntry {
  const char* name;
  lv_obj_t** screen;
  Creator create;
  CanRelease canRelease;  // nullptr = 总是可释放
  OnDestroy  onDestroy;   // nullptr = 只需 lv_obj_del（对象树自带内存由 LVGL 回收）
};""",
     """struct ActivityEntry {
  const char* name;
  lv_obj_t** screen;
  Creator create;
  CanRelease canRelease;  // nullptr = 总是可释放
  OnDestroy  onDestroy;   // nullptr = 只需 lv_obj_del（对象树自带内存由 LVGL 回收）
  uint32_t   usedBytes;   // 创建时吃掉的内部 DRAM（后台管理页显示用）
};""",
     "nav.cpp struct"),

    ('    {"desktop",  &nav_desktop,  DesktopScreen_create,    nullptr,         nullptr},\n',
     '    {"desktop",  &nav_desktop,  DesktopScreen_create,    nullptr,         nullptr},\n'
     '    {"taskmgr",  &nav_taskmgr,  TaskMgrScreen_create,    nullptr,         nullptr},\n',
     "nav.cpp table"),

    ("""  uint32_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  lv_obj_t* scr = e->create();
  *target = scr;
  Serial.printf("[Nav] open %s: %s, DRAM %u -> %u\\n", e->name,
                scr ? "ok" : "FAILED", (unsigned)before,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  return scr;""",
     """  uint32_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  lv_obj_t* scr = e->create();
  *target = scr;
  uint32_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  /* 后台管理页要显示"这个应用占了多大"。这里记的是创建瞬间的 DRAM 差值，
     之后应用自己还会再申请（浏览器布局树等），所以是个下界，不是精确值。 */
  e->usedBytes = (after < before) ? (before - after) : 0;
  Serial.printf("[Nav] open %s: %s, DRAM %u -> %u\\n", e->name,
                scr ? "ok" : "FAILED", (unsigned)before, (unsigned)after);
  return scr;""",
     "nav.cpp measure"),
])

# ── nav.cpp：追加实现（文件尾）──
nav = read("src/app/nav.cpp")
if "int nav_running_list(" in nav:
    print("SKIP: nav.cpp impl")
else:
    nl = "\r\n" if "\r\n" in nav else "\n"
    impl = """
/* ── 后台管理接口实现 ────────────────────────────────────────────────────── */

int nav_running_list(NavRunningInfo* out, int max) {
  if (!out || max <= 0) return 0;
  lv_obj_t* act = lv_scr_act();
  int n = 0;
  for (int i = 0; i < s_count && n < max; i++) {
    ActivityEntry& e = s_table[i];
    if (!*(e.screen)) continue;                    // 没在内存里
    out[n].id = e.name;
    out[n].bytes = e.usedBytes;
    out[n].current = (*(e.screen) == act);
    n++;
  }
  return n;
}

/* 关掉一个 Activity。三条拒绝线，都是踩过坑的：
     1. 前台（当前正在显示的屏）—— lv_obj_del 掉它 = 删掉 LVGL 正在渲染的树；
     2. Launcher —— 唯一的家，删了就回不去了；
     3. 释放守卫（连接中 / 后台 fetch 还在跑）—— 宁可这次不关，也别写坏树。 */
bool nav_close(const char* id) {
  if (!id || !id[0]) return false;
  lv_obj_t* act = lv_scr_act();
  for (int i = 0; i < s_count; i++) {
    ActivityEntry& e = s_table[i];
    if (strcmp(e.name, id) != 0) continue;
    if (!*(e.screen)) return false;                // 本来就没开
    if (*(e.screen) == act) return false;          // 前台，不关
    if (e.canRelease && !e.canRelease()) return false;
    uint32_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (e.onDestroy) e.onDestroy();
    lv_obj_del(*(e.screen));
    *(e.screen) = nullptr;
    e.usedBytes = 0;
    Serial.printf("[Nav] close %s: DRAM %u -> %u\\n", id, (unsigned)before,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
  }
  return false;
}

int nav_close_all() {
  lv_obj_t* act = lv_scr_act();
  int n = 0;
  for (int i = 0; i < s_count; i++) {
    ActivityEntry& e = s_table[i];
    if (!*(e.screen)) continue;
    if (*(e.screen) == act) continue;              // 前台留着
    if (e.canRelease && !e.canRelease()) continue;
    if (nav_close(e.name)) n++;
  }
  return n;
}
""".replace("\n", nl)
    write("src/app/nav.cpp", nav.rstrip("\r\n") + nl + impl)
    print("OK: nav.cpp impl")

# ── app.cpp：全局指针 ──
edit("src/app/app.cpp", [
    ("lv_obj_t* nav_desktop = nullptr;",
     "lv_obj_t* nav_desktop = nullptr;\nlv_obj_t* nav_taskmgr = nullptr;",
     "app.cpp global"),
])

# ── app_registry.cpp：桌面图标 ──
edit("src/app/app_registry.cpp", [
    ('{"weather",  "天气",   Icon::Weather, &nav_weather,  AppGroup::Desktop, true},\n',
     '{"weather",  "天气",   Icon::Weather, &nav_weather,  AppGroup::Desktop, true},\n'
     '{"taskmgr",  "后台",   Icon::Switch,  &nav_taskmgr,  AppGroup::Desktop, true},\n',
     "registry entry"),
])
