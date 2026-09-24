# -*- coding: utf-8 -*-
"""补：1) delete_cb 清掉新指针  2) 串口命令 weather auto on|off / wxauto"""
import io

W = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\weather_screen.cpp"
s = io.open(W, encoding="utf-8").read()

old = """  for (int i = 0; i < HOUR_N; i++) g_hourLab[i] = nullptr;"""
new = """  for (int i = 0; i < HOUR_N; i++) {
    g_hourLab[i] = nullptr;
    g_hourCard[i] = nullptr;
  }
  for (int i = 0; i < DAY_N; i++) {
    g_dayBar[i] = nullptr;
    g_dayBarBg[i] = nullptr;
  }
  g_wxIcon = nullptr;"""
assert s.count(old) == 1
s = s.replace(old, new, 1)
io.open(W, "w", encoding="utf-8", newline="").write(s)

C = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\hal\serial_console.cpp"
s = io.open(C, encoding="utf-8").read()

old2 = """} else if (strcmp(cmd, "geo") == 0) {"""
new2 = """} else if (strcmp(cmd, "wxauto") == 0) {
    /* 天气后台自动更新开关。off = 完全停止（任务用 portMAX_DELAY 睡，不轮询） */
    if (!arg || !arg[0]) {
      Serial.printf("[Weather] auto refresh = %s\\n",
                    WeatherScreen_auto() ? "on" : "off");
    } else if (strcmp(arg, "on") == 0 || strcmp(arg, "1") == 0) {
      WeatherScreen_setAuto(true);
    } else if (strcmp(arg, "off") == 0 || strcmp(arg, "0") == 0) {
      WeatherScreen_setAuto(false);
    } else {
      Serial.println("Usage: wxauto [on|off]");
    }
  } else if (strcmp(cmd, "geo") == 0) {"""
assert s.count(old2) == 1
s = s.replace(old2, new2, 1)

h = '  Serial.println("weather           - 建天气屏 + 拉一次, 打印 HTTP 码和返回体");\n'
assert s.count(h) == 1
s = s.replace(h, h + '  Serial.println("wxauto [on|off]   - 天气后台每小时自动更新开关(off=完全停止)");\n', 1)
io.open(C, "w", encoding="utf-8", newline="").write(s)
print("ok")
