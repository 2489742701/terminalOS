# -*- coding: utf-8 -*-
"""加串口 `imgclose`：不用点屏也能验覆盖层能不能关掉。"""
import io
BASE = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal'


def w(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


def rep(s, old, new, tag, cnt=1):
    n = s.count(old)
    assert n == cnt, ('anchor %s: found %d' % (tag, n))
    return s.replace(old, new, 1)


B = BASE + r'\src\app\browser_screen.cpp'
s = io.open(B, encoding='utf-8').read()
old = """void BrowserScreen_imgScan() {"""
new = """/* 串口 `imgclose`：关掉看图覆盖层。
   专治"点叉叉退不出去"—— 不用点屏也能验那条关闭链路通不通。 */
void BrowserScreen_imgClose() {
  if (!g_viewRoot) { Serial.println("[Img] viewer not open"); return; }
  viewRequestClose("serial");
}

void BrowserScreen_imgScan() {"""
s = rep(s, old, new, 'app')
w(B, s)

BH = BASE + r'\src\app\browser_screen.h'
s = io.open(BH, encoding='utf-8').read()
old = """void BrowserScreen_imgScan();"""
new = """void BrowserScreen_imgScan();
/* 串口 `imgclose`：关掉看图覆盖层 */
void BrowserScreen_imgClose();"""
s = rep(s, old, new, 'hdr')
w(BH, s)

S = BASE + r'\src\hal\serial_console.cpp'
s = io.open(S, encoding='utf-8').read()
old = """  } else if (strcmp(cmd, "imgview") == 0) {"""
new = """  } else if (strcmp(cmd, "imgclose") == 0) {
    /* 关掉看图覆盖层（验"点叉叉退不出去"那条链路） */
    BrowserScreen_imgClose();
  } else if (strcmp(cmd, "imgview") == 0) {"""
s = rep(s, old, new, 'console')
w(S, s)
print('ok')
