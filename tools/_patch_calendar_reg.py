# -*- coding: utf-8 -*-
"""把日历注册进系统：nav 表 / nav.h 声明 / 桌面磁贴 / 串口 screens 表。"""
import io

D = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src"

# ── nav.h：声明 ──
p = D + r"\app\nav.h"
s = io.open(p, encoding="utf-8").read()
old = "extern lv_obj_t* nav_weather;"
if s.count(old) == 1:
    s = s.replace(old, old + "\nextern lv_obj_t* nav_calendar;", 1)
    io.open(p, "w", encoding="utf-8", newline="").write(s)
    print("nav.h ok")
else:
    print("!! nav.h anchor missing")

# ── nav.cpp：定义 + 注册 ──
p = D + r"\app\nav.cpp"
s = io.open(p, encoding="utf-8").read()
s = s.replace('#include "touchtest_screen.h"',
              '#include "touchtest_screen.h"\n#include "calendar_screen.h"', 1)
old2 = '    {"weather",  &nav_weather,  WeatherScreen_create,  nullptr,         nullptr},'
assert s.count(old2) == 1
s = s.replace(old2, old2 + '\n    {"calendar", &nav_calendar, CalendarScreen_create, nullptr,         nullptr},', 1)
io.open(p, "w", encoding="utf-8", newline="").write(s)
print("nav.cpp ok")

# nav_calendar 的实体定义在哪个文件？找 nav_weather 的定义
p2 = D + r"\app\nav.cpp"
s2 = io.open(p2, encoding="utf-8").read()
if "lv_obj_t* nav_calendar" not in s2:
    # 找 nav_weather 定义处
    import re
    m = re.search(r"^lv_obj_t\* nav_weather\s*=\s*nullptr;", s2, re.M)
    if m:
        s2 = s2[:m.end()] + "\nlv_obj_t* nav_calendar = nullptr;" + s2[m.end():]
        io.open(p2, "w", encoding="utf-8", newline="").write(s2)
        print("nav_calendar defined")
    else:
        print("!! nav_weather def not found in nav.cpp")

# ── app_registry.cpp：桌面磁贴 ──
p = D + r"\app\app_registry.cpp"
s = io.open(p, encoding="utf-8").read()
old3 = '    {"weather",  "天气",   Icon::Weather,  &nav_weather,  AppGroup::Desktop, true},'
assert s.count(old3) == 1
s = s.replace(old3, old3 + '\n    {"calendar", "日历",   Icon::Tasks,    &nav_calendar, AppGroup::Desktop, true},', 1)
io.open(p, "w", encoding="utf-8", newline="").write(s)
print("registry ok")

# ── serial_console.cpp：nav calendar ──
p = D + r"\hal\serial_console.cpp"
s = io.open(p, encoding="utf-8").read()
old4 = '  {"weather",   &nav_weather},'
assert s.count(old4) == 1
s = s.replace(old4, old4 + '\n  {"calendar",  &nav_calendar},', 1)
io.open(p, "w", encoding="utf-8", newline="").write(s)
print("serial ok")
