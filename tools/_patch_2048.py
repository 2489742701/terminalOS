# -*- coding: utf-8 -*-
"""把 2048 接进 Activity 注册表 / 游戏栏目 / 串口 nav。"""
import io

GT = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"


def patch(rel, pairs, crlf):
    p = GT + "\\" + rel
    t = io.open(p, encoding="utf-8", newline=None).read()
    for old, new in pairs:
        assert t.count(old) == 1, "%s: anchor count=%d\n%s" % (rel, t.count(old), old[:80])
        t = t.replace(old, new)
    if crlf:
        t = t.replace("\n", "\r\n")
    io.open(p, "w", encoding="utf-8", newline="").write(t)
    print("patched", rel)


# ── nav.h：extern 声明 ─────────────────────────────────────────────────
patch(r"src\app\nav.h", [
    ("extern lv_obj_t* nav_memory;",
     "extern lv_obj_t* nav_memory;\nextern lv_obj_t* nav_2048;     // 2048（游戏栏目）"),
    ("extern lv_obj_t* nav_games;    // 游戏栏目：贪吃蛇 / 记忆卡牌",
     "extern lv_obj_t* nav_games;    // 游戏栏目：贪吃蛇 / 记忆卡牌 / 2048"),
], False)

# ── nav.cpp：include + 注册表条目 ──────────────────────────────────────
patch(r"src\app\nav.cpp", [
    ('#include "memory_screen.h"',
     '#include "memory_screen.h"\n#include "game2048_screen.h"'),
    ('    {"memory",   &nav_memory,   MemoryScreen_create,   nullptr,         nullptr},',
     '    {"memory",   &nav_memory,   MemoryScreen_create,   nullptr,         nullptr},\n'
     '    /* 2048：纯回合制（滑动才走一步），没有 tick，不用在 App::loop 里挂东西 */\n'
     '    {"2048",     &nav_2048,     Game2048Screen_create, nullptr,         nullptr},'),
], False)

# ── app_registry.cpp：游戏栏目里多一个磁贴 ─────────────────────────────
patch(r"src\app\app_registry.cpp", [
    ('    {"memory",   "记忆卡牌", Icon::Music,  &nav_memory,   AppGroup::Game,    false},',
     '    {"memory",   "记忆卡牌", Icon::Music,  &nav_memory,   AppGroup::Game,    false},\n'
     '    {"2048",     "2048",    Icon::Game,   &nav_2048,     AppGroup::Game,    false},'),
], False)

# ── serial_console.cpp：nav 表 + help ──────────────────────────────────
patch(r"src\hal\serial_console.cpp", [
    ('  {"memory",    &nav_memory},',
     '  {"memory",    &nav_memory},\n  {"2048",      &nav_2048},'),
    ('  Serial.println("  screens: launcher clock settings wifi game browser draw memory sysinfo weather games desktop taskmgr");',
     '  Serial.println("  screens: launcher clock settings wifi game 2048 browser draw memory sysinfo weather games desktop taskmgr");'),
], False)
print("ALL OK")
