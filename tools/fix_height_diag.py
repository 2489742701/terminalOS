"""内容高度诊断：测量前先强制 LVGL 重算布局（行级替换，兼容 CRLF）。

之前 render done 里的 scrollable 恒为负（total=80），看着像"内容只有 80px"，
其实是因为刚创建完 widget、LVGL 还没重算坐标，读到的是陈旧值。
这个假数字已经误导过一次判断，修掉它。
"""
import io

PATH = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"

ANCHOR = "int scrollBottom = g_content ? lv_obj_get_scroll_bottom(g_content) : 0;"
INSERT = [
    "          /* ⚠️ 必须先重算布局：刚创建完 widget 时 LVGL 还没更新坐标，",
    "             直接读会拿到陈旧值（曾恒为 -334 / total=80，误导过一次判断，",
    "             差点以为内容被压扁了）。 */",
    "          if (g_content) lv_obj_update_layout(g_content);",
]

t = io.open(PATH, encoding="utf-8", newline="").read()
crlf = "\r\n" in t
lines = t.replace("\r\n", "\n").split("\n")
out = ["crlf=%s" % crlf]

if "lv_obj_update_layout(g_content)" in t:
    out.append("ALREADY PATCHED")
else:
    idx = [i for i, l in enumerate(lines) if ANCHOR in l]
    if not idx:
        out.append("*** ANCHOR NOT FOUND ***")
    else:
        i = idx[0]
        lines[i:i] = INSERT
        t = "\n".join(lines)
        if crlf:
            t = t.replace("\n", "\r\n")
        io.open(PATH, "w", encoding="utf-8", newline="").write(t)
        out.append("patched at line %d" % (i + 1))

v = io.open(PATH, encoding="utf-8").read()
out.append("verify: %s" % ("lv_obj_update_layout(g_content)" in v))
print("\n".join(out))
