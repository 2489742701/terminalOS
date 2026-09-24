"""按域名分 UA（行级替换，兼容 CRLF）。"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\lvgl_renderer.cpp"

t = io.open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in t
lines = t.replace("\r\n", "\n").split("\n")
log = []

if "Android 13; Pixel 7" in "\n".join(lines):
    log.append("ALREADY")
else:
    # 1) 找到 "GET " + path 那一行，在它前面插入 UA 选择
    i_req = -1
    for i, l in enumerate(lines):
        if 'String req = "GET " + path' in l:
            i_req = i
            break
    if i_req < 0:
        log.append("*** req line not found ***")
    else:
        block = [
            '  /* ── 按域名选 UA ──',
            '     默认 KitKat（Android 4.4 / Chrome 30 移动版）：对"老机器"宽容，百度不会',
            '     302 到 wappass 图形验证码；页面也小。',
            '     ⚠️ 但必应对 KitKat 返回的是**阉割版**：没有分页 UI，且 first= / page=',
            '     参数被直接忽略（实测 first=11 与第 1 页结果完全相同）→ 屏幕上根本没有',
            '     "下一页"。换成 Android 13 Chrome 后 b_pag / 下一页 都出现，翻页真的有效，',
            '     体积也只有 86KB（外部 CSS 在平铺模式下本来就跳过，不会变成 95 次 TLS）。 */',
            '  String ua;',
            '  if (host.indexOf("bing.com") >= 0) {',
            '    ua = "Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36";',
            '  } else {',
            '    ua = "Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36";',
            '  }',
        ]
        lines[i_req:i_req] = block
        log.append("ua block inserted at %d" % (i_req + 1))

    # 2) 把写死的 UA 那行换成 ua 变量
    done = False
    for i, l in enumerate(lines):
        if '"User-Agent: Mozilla/5.0 (Linux; Android 4.4.2' in l:
            lines[i] = '               "User-Agent: " + ua + "\\r\\n" +'
            done = True
            break
    log.append("ua line replaced: %s" % done)

out = "\n".join(lines)
if crlf:
    out = out.replace("\n", "\r\n")
io.open(P, "w", encoding="utf-8", newline="").write(out)

v = io.open(P, encoding="utf-8").read()
log.append("verify bing:   %s" % ("Android 13; Pixel 7" in v))
log.append("verify kitkat: %s" % ("Android 4.4.2; Nexus 5" in v))
log.append("verify ua var: %s" % ('"User-Agent: " + ua +' in v))
print("\n".join(log))
