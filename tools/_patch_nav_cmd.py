# -*- coding: utf-8 -*-
"""给串口控制台加 back / navlist 两个诊断命令，用来实锤
「回桌面（nav_back_home）会不会把后台 Activity 全清掉」。"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\hal\serial_console.cpp"
src = io.open(P, encoding="utf-8").read()

anchor = '} else if (strcmp(cmd, "sstore") == 0) {'
assert src.count(anchor) == 1, "anchor not unique"

new = '''} else if (strcmp(cmd, "back") == 0) {
    /* 走真实的「返回桌面」路径（带动画版）—— 就是左滑退出 / 顶栏返回走的那条，
       用来复现"应用切到后台后是不是被自动结束了"。 */
    nav_back_home_anim();
    Serial.println("[Console] nav_back_home_anim() returned");
  } else if (strcmp(cmd, "navlist") == 0) {
    NavRunningInfo list[16];
    int n = nav_running_list(list, 16);
    Serial.printf("[Nav] running = %d\\n", n);
    for (int i = 0; i < n; i++)
      Serial.printf("   %-12s %6u B %s\\n", list[i].id, (unsigned)list[i].bytes,
                    list[i].current ? "<- foreground" : "");
    if (n == 0) Serial.println("   (empty: launcher only)");
  ''' + anchor

src = src.replace(anchor, new, 1)

# help 里也列出来
h = '  Serial.println("nav <screen>      - navigate to screen");\n'
assert src.count(h) == 1
src = src.replace(h, h +
    '  Serial.println("back              - 走真实返回桌面路径(诊断后台是否被清)");\n'
    '  Serial.println("navlist           - 列出仍在内存里的 Activity(后台列表)");\n', 1)

io.open(P, "w", encoding="utf-8", newline="").write(src)
print("ok")
