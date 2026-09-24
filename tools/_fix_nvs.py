# -*- coding: utf-8 -*-
import io

p = r"src/app/settings_store.cpp"
t = io.open(p, encoding="utf-8", newline="").read()

# 1) loadAll：只读模式打不开"还不存在的 namespace"（NOT_FOUND）—— 第一次开机必然失败。
#    改用读写模式（不存在则创建），这样首次开机也能读到默认值并建好 namespace。
old = """  if (!p.begin(kNs, true)) {           // true = 只读
    Serial.println("[SettingsStore] NVS begin(ro) failed, keep defaults");
    return;
  }"""
new = """  /* ⚠️ 必须读写模式（false），不能用只读（true）：
     只读模式打开**还不存在的** namespace 会 nvs_open failed: NOT_FOUND
     —— 也就是第一次开机必然失败，持久化形同虚设。读写模式不存在则创建。 */
  if (!p.begin(kNs, false)) {
    Serial.println("[SettingsStore] NVS begin failed, keep defaults");
    return;
  }"""
assert t.count(old) == 1, t.count(old)
t = t.replace(old, new)

# 2) dump：非 0 时别打空括号
old = """  Serial.printf("[SettingsStore] %s idle=%lums(%s) bright=%d autosync=%d viewport=%d\\n",
                s_hasStored ? "NVS" : "(defaults, nothing stored yet)",
                s_idleMs, s_idleMs == 0 ? "常亮" : "", s_brightness,
                (int)s_autoSync, s_viewport);"""
new = """  const char* src = s_hasStored ? "NVS" : "(defaults, nothing stored yet)";
  if (s_idleMs == 0)
    Serial.printf("[SettingsStore] %s idle=常亮 bright=%d autosync=%d viewport=%d\\n",
                  src, s_brightness, (int)s_autoSync, s_viewport);
  else
    Serial.printf("[SettingsStore] %s idle=%lums bright=%d autosync=%d viewport=%d\\n",
                  src, s_idleMs, s_brightness, (int)s_autoSync, s_viewport);"""
assert t.count(old) == 1, t.count(old)
t = t.replace(old, new)

io.open(p, "w", encoding="utf-8", newline="").write(t)
print("settings_store.cpp patched")

# 3) 串口 sleep <ms>：要走 SettingsStore，否则只改了底层、store 不知道，
#    两边值不一致（dump 出来的是旧值，重启也会丢）。
p = r"src/hal/serial_console.cpp"
t = io.open(p, encoding="utf-8", newline="").read()
old = "    if (arg && *arg) ScreenSaver::setIdleTimeout(strtoul(arg, nullptr, 10));"
new = """    if (arg && *arg) {
      unsigned long ms = strtoul(arg, nullptr, 10);
      ScreenSaver::setIdleTimeout(ms);
      SettingsStore::saveIdle(ms);   /* 同步给 store，否则 dump 是旧值 / 重启丢失 */
    }"""
assert t.count(old) == 1, t.count(old)
t = t.replace(old, new)
io.open(p, "w", encoding="utf-8", newline="").write(t)
print("serial_console.cpp patched")
