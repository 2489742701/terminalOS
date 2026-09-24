"""给存下来的页面加一个内置 HTTP 服务：电脑打开板子 IP 就能看/下载。

"下载到 flash 之后怎么阅读" —— 板子上有网（WiFi），最省事的办法是让它自己当
一个小 Web 服务器：串口 `serve` 启动，电脑浏览器打开 http://<板子IP>/ 就是文件列表，
点开就是那一页的 HTML（必应搜索结果页在电脑上是完整可点的）。
`servestop` 关闭。另有 `ls` 列文件。
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"
H = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.h"
S = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\hal\serial_console.cpp"

CODE = r'''
/* ══ 存下来的页面怎么读：板子自己当一个小 Web 服务器 ══
 * 页面存在 flash 里，板子自己没法舒服地读它（只有 480 屏）。但板子在 WiFi 里 ——
 * 起个 80 端口的服务，电脑浏览器打开 http://<板子IP>/ 就是文件列表，点开就是那一页。
 * 串口命令：serve / servestop / ls
 * ⚠️ handleClient() 必须被周期性调用，挂在 BrowserScreen_tick() 里。 */
#include <WebServer.h>
static WebServer* g_pageSrv = nullptr;

static void pageServerStart() {
  if (g_pageSrv) { Serial.println("[Serve] already running"); return; }
  if (WiFi.status() != WL_CONNECTED) { Serial.println("[Serve] WiFi 未连接"); return; }
  if (!LittleFS.begin(false)) { Serial.println("[Serve] LittleFS 挂载失败"); return; }

  g_pageSrv = new WebServer(80);
  g_pageSrv->on("/", HTTP_GET, []() {
    String html = "<html><head><meta charset='utf-8'></head><body>"
                  "<h3>geek-terminal saved pages</h3><ul>";
    File root = LittleFS.open("/");
    File f = root.openNextFile();
    int n = 0;
    while (f) {
      String nm = String(f.name());
      if (nm.endsWith(".html")) {
        html += "<li><a href='" + nm + "'>" + nm + "</a> (" + f.size() + " B)</li>";
        n++;
      }
      f = root.openNextFile();
    }
    if (n == 0) html += "<li>(还没有保存过页面，先用 dl 存一页)</li>";
    html += "</ul></body></html>";
    g_pageSrv->send(200, "text/html; charset=utf-8", html);
  });
  g_pageSrv->onNotFound([]() {
    String p = g_pageSrv->uri();
    if (!LittleFS.exists(p)) { g_pageSrv->send(404, "text/plain", "not found"); return; }
    File f = LittleFS.open(p, "r");
    g_pageSrv->streamFile(f, "text/html; charset=utf-8");
    f.close();
  });
  g_pageSrv->begin();
  Serial.printf("[Serve] 启动：http://%s/\n", WiFi.localIP().toString().c_str());
}

static void pageServerStop() {
  if (!g_pageSrv) { Serial.println("[Serve] 没在运行"); return; }
  g_pageSrv->stop();
  delete g_pageSrv;
  g_pageSrv = nullptr;
  LittleFS.end();
  Serial.println("[Serve] 已停止");
}

static void pageServerTick() {
  if (g_pageSrv) g_pageSrv->handleClient();
}

static void listSavedPages() {
  if (!LittleFS.begin(false)) { Serial.println("[ls] LittleFS 挂载失败"); return; }
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  int n = 0;
  while (f) {
    Serial.printf("  %-20s %u B\n", f.name(), (unsigned)f.size());
    n++;
    f = root.openNextFile();
  }
  if (n == 0) Serial.println("  (空)");
  Serial.printf("[ls] 共 %d 个，已用 %u / %u B\n", n,
                (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  LittleFS.end();
}
'''

log = []
t = io.open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in t
lines = t.replace("\r\n", "\n").split("\n")

if "pageServerStart" in "\n".join(lines):
    log.append("cpp: ALREADY")
else:
    A = "/* ── tick：状态机驱动 ── */"
    i = -1
    for k, l in enumerate(lines):
        if A in l:
            i = k
            break
    if i < 0:
        log.append("cpp: *** anchor missing ***")
    else:
        lines[i:i] = CODE.strip("\n").split("\n") + [""]
        # tick 里挂 handleClient
        txt = "\n".join(lines)
        if "pageServerTick();" not in txt:
            if "void BrowserScreen_tick() {" in txt:
                txt = txt.replace("void BrowserScreen_tick() {",
                                  "void BrowserScreen_tick() {\n  pageServerTick();   /* 存下来的页面要能被电脑访问 */", 1)
        lines = txt.split("\n")
        log.append("cpp: ok")

out = "\n".join(lines)
if crlf:
    out = out.replace("\n", "\r\n")
io.open(P, "w", encoding="utf-8", newline="").write(out)

v = io.open(P, encoding="utf-8").read()
log.append("cpp verify server: %s" % ("pageServerStart" in v))
log.append("cpp verify tick:   %s" % ("pageServerTick();" in v))

# header 声明
h = io.open(H, encoding="utf-8", newline="").read()
if "BrowserScreen_serve" in h:
    log.append("hdr: ALREADY")
else:
    A = "void BrowserScreen_download();"
    if A not in h:
        log.append("hdr: *** anchor missing ***")
    else:
        h = h.replace(A,
                      A + "\n\n/* 内置页面服务：serve / servestop / ls（串口命令） */\n"
                          "void BrowserScreen_serve(bool on);\n"
                          "void BrowserScreen_listPages();", 1)
        io.open(H, "w", encoding="utf-8", newline="").write(h)
        log.append("hdr: ok")

# 公开包装（在 cpp 里）
if "void BrowserScreen_serve(bool on)" in io.open(P, encoding="utf-8").read():
    log.append("wrap: ALREADY")
else:
    t2 = io.open(P, encoding="utf-8", newline="").read()
    crlf2 = "\r\n" in t2
    l2 = t2.replace("\r\n", "\n").split("\n")
    A = "void BrowserScreen_download() { downloadCurrentPage(); }"
    i = -1
    for k, s in enumerate(l2):
        if A in s:
            i = k
            break
    if i < 0:
        log.append("wrap: *** anchor missing ***")
    else:
        l2[i:i] = ["void BrowserScreen_serve(bool on) { on ? pageServerStart() : pageServerStop(); }",
                   "void BrowserScreen_listPages() { listSavedPages(); }"]
        o2 = "\n".join(l2)
        if crlf2:
            o2 = o2.replace("\n", "\r\n")
        io.open(P, "w", encoding="utf-8", newline="").write(o2)
        log.append("wrap: ok")

# 串口命令
s = io.open(S, encoding="utf-8", newline="").read()
if 'strcmp(cmd, "serve")' in s:
    log.append("console: ALREADY")
else:
    A = '  } else if (strcmp(cmd, "dl") == 0) {'
    if A not in s:
        log.append("console: *** anchor missing ***")
    else:
        NEW = ('  } else if (strcmp(cmd, "serve") == 0 || strcmp(cmd, "servestop") == 0) {\n'
               '    /* 起/停内置页面服务：电脑浏览器打开 http://<板子IP>/ 看存下来的页面 */\n'
               '    BrowserScreen_serve(strcmp(cmd, "serve") == 0);\n'
               '  } else if (strcmp(cmd, "ls") == 0) {\n'
               '    BrowserScreen_listPages();\n')
        s = s.replace(A, NEW + A, 1)
        io.open(S, "w", encoding="utf-8", newline="").write(s)
        log.append("console: ok")

v2 = io.open(S, encoding="utf-8").read()
log.append("console verify: %s" % ('strcmp(cmd, "serve")' in v2))
print("\n".join(log))
