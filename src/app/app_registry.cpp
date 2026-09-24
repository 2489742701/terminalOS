#include "app_registry.h"
#include "nav.h"
#include <Preferences.h>
#include <string.h>

namespace {

/* 整张表就是桌面能长出什么。
   「记忆」从桌面撤下，改挂在游戏栏目里（group==Game）—— 它本来就是卡牌游戏，
   跟贪吃蛇同级，不该跟「系统」「天气」这些平起平坐占一个桌面磁贴。 */
AppEntry s_apps[] = {
    {"clock",    "时钟",   Icon::Clock,    &nav_clock,    AppGroup::Desktop, true},
    {"settings", "设置",   Icon::Settings, &nav_settings, AppGroup::Desktop, true},
    {"wifi",     "无线",   Icon::Wifi,     &nav_wifi,     AppGroup::Desktop, true},
    {"games",    "游戏",   Icon::Game,     &nav_games,    AppGroup::Desktop, true},
    {"browser",  "浏览器", Icon::Browser,  &nav_browser,  AppGroup::Desktop, true},
    {"draw",     "画板",   Icon::Terminal, &nav_draw,     AppGroup::Desktop, true},
    {"sysinfo",  "系统",   Icon::Power,    &nav_sysinfo,  AppGroup::Desktop, true},
    {"weather",  "天气",   Icon::Weather,  &nav_weather,  AppGroup::Desktop, true},
    {"calendar", "日历",   Icon::Tasks,    &nav_calendar, AppGroup::Desktop, true},
    {"taskmgr",  "后台",   Icon::Switch,  &nav_taskmgr,  AppGroup::Desktop, true},

    {"snake",    "贪吃蛇", Icon::Game,     &nav_game,     AppGroup::Game,    false},
    {"memory",   "记忆卡牌", Icon::Music,  &nav_memory,   AppGroup::Game,    false},
    {"2048",     "2048",    Icon::Game,   &nav_2048,     AppGroup::Game,    false},
};

const int s_count = (int)(sizeof(s_apps) / sizeof(s_apps[0]));

/* NVS 命名空间。可见性是 1 bit，按 id 存 bool。
   注意：Preferences 每次 begin/end 都要走 flash，所以这里只在
   真正读写时开一次，不做常驻句柄。 */
const char* NVS_NS = "desktop";

bool readFlag(const char* id, bool def) {
  Preferences p;
  if (!p.begin(NVS_NS, true)) return def;   // 只读模式，命名空间不存在也不建
  bool v = p.getBool(id, def);
  p.end();
  return v;
}

void writeFlag(const char* id, bool v) {
  Preferences p;
  if (!p.begin(NVS_NS, false)) return;      // 可写；失败就只改内存态
  p.putBool(id, v);
  p.end();
}

}  // namespace

void appreg_init() {
  Preferences p;
  if (!p.begin(NVS_NS, false)) return;   // 可写：命名空间不存在时顺手建出来
  for (int i = 0; i < s_count; i++) {
    if (s_apps[i].group != AppGroup::Desktop) continue;
    if (!p.isKey(s_apps[i].id)) p.putBool(s_apps[i].id, s_apps[i].defVisible);
  }
  p.end();
}

int appreg_count() { return s_count; }

const AppEntry* appreg_at(int i) {
  if (i < 0 || i >= s_count) return nullptr;
  return &s_apps[i];
}

bool appreg_visible(int i) {
  const AppEntry* e = appreg_at(i);
  if (!e) return false;
  if (e->group != AppGroup::Desktop) return true;  // 游戏栏目内的项不受桌面开关管
  return readFlag(e->id, e->defVisible);
}

void appreg_set_visible(int i, bool v) {
  const AppEntry* e = appreg_at(i);
  if (!e || e->group != AppGroup::Desktop) return;
  writeFlag(e->id, v);
}

int appreg_desktop_visible_count() {
  int n = 0;
  for (int i = 0; i < s_count; i++) {
    if (s_apps[i].group == AppGroup::Desktop && appreg_visible(i)) n++;
  }
  return n;
}

int appreg_desktop_visible_index(int n) {
  int seen = 0;
  for (int i = 0; i < s_count; i++) {
    if (s_apps[i].group != AppGroup::Desktop) continue;
    if (!appreg_visible(i)) continue;
    if (seen == n) return i;
    seen++;
  }
  return -1;
}

void appreg_reset_visible() {
  for (int i = 0; i < s_count; i++) {
    if (s_apps[i].group == AppGroup::Desktop) writeFlag(s_apps[i].id, s_apps[i].defVisible);
  }
}

int appreg_find(const char* id) {
  if (!id) return -1;
  for (int i = 0; i < s_count; i++) {
    if (strcmp(s_apps[i].id, id) == 0) return i;
  }
  return -1;
}
