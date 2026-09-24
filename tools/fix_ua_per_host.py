"""按域名分 UA：必应用 Android 13 Chrome（有分页），其余继续 KitKat（躲百度验证码）。

实测（2026-09-23 深夜，cn.bing.com/search?q=esp32）：
| UA | 页面大小 | b_pag | 下一页 | first=11 翻页 |
|---|---|---|---|---|
| KitKat(原) | 59KB | 0 | 0 | **无效**（结果和第1页一样） |
| Android13-Chrome | 86KB | 2 | 3 | **有效** |
| Win-Chrome120 | 98KB | 7 | 3 | 无效 |
| iPhone-Safari | 180KB | 11 | 0 | 无效 |

KitKat 下必应给的是**阉割版**：没有分页 UI，`first=`/`page=` 参数直接被忽略 ——
所以屏幕上根本没有"下一页"可渲染，不是我们漏了。
Android13 UA 才有真分页，且体积仍是 86KB（外部 CSS 在平铺模式下本来就跳过）。

百度继续用 KitKat：桌面 UA 会被 302 到 wappass 图形验证码。
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\lvgl_renderer.cpp"

OLD = '''  String req = "GET " + path + " HTTP/1.1\\r\\n" +
               "Host: " + host + "\\r\\n" +
               /* UA 伪装成 Android 4.4 KitKat（Chrome 30 移动版）。
                  为什么不是桌面 Chrome 120：
                    - 桌面 UA 会拿到 700KB+ 的 PC 版首页（208 个节点，解析峰值 DRAM 吃紧）；
                    - 移动 UA 直接进 m.* 的轻量页，几十 KB，本机的解析/渲染扛得住；
                    - 反爬策略对"老机器"更宽容：KitKat 这种 2013 年的 UA 不会触发
                      wappass 的图形验证码（桌面 Chrome 120 + mbedTLS 指纹 = 必被拦）。
                  详见 docs/12 §5。 */
               "User-Agent: Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36\\r\\n" +'''

NEW = '''  /* ── 按域名选 UA ──
     默认 KitKat（Android 4.4 / Chrome 30 移动版）：对"老机器"宽容，百度不会
     302 到 wappass 图形验证码；页面也小。
     ⚠️ 但必应对 KitKat 返回的是**阉割版**：没有分页 UI，且 first= / page=
     参数被直接忽略（实测 first=11 与第 1 页结果完全相同）→ 屏幕上根本没有
     "下一页"。换成 Android 13 Chrome 后 b_pag / 下一页 都出现，翻页真的有效，
     体积也只有 86KB（外部 CSS 在平铺模式下本来就跳过，不会变成 95 次 TLS）。 */
  String ua;
  if (host.indexOf("bing.com") >= 0) {
    ua = "Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36";
  } else {
    ua = "Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36";
  }
  String req = "GET " + path + " HTTP/1.1\\r\\n" +
               "Host: " + host + "\\r\\n" +
               "User-Agent: " + ua + "\\r\\n" +'''

t = io.open(P, encoding="utf-8", newline="").read()
log = []
if "bing.com\" >= 0" in t:
    log.append("ALREADY")
elif OLD not in t:
    log.append("*** OLD NOT FOUND ***")
else:
    t = t.replace(OLD, NEW, 1)
    io.open(P, "w", encoding="utf-8", newline="").write(t)
    log.append("patched")

v = io.open(P, encoding="utf-8").read()
log.append("verify bing ua: %s" % ("Android 13; Pixel 7" in v))
log.append("verify kitkat:  %s" % ("Android 4.4.2; Nexus 5" in v))
print("\n".join(log))
