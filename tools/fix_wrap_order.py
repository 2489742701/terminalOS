"""把 BrowserScreen_serve / BrowserScreen_listPages 挪到 pageServerStart 之后。"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"

t = io.open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in t
lines = t.replace("\r\n", "\n").split("\n")
log = []

WRAP = ["void BrowserScreen_serve(bool on) { on ? pageServerStart() : pageServerStop(); }",
        "void BrowserScreen_listPages() { listSavedPages(); }"]

# 1) 删掉现有位置
keep = []
removed = 0
for l in lines:
    if l.strip() in WRAP:
        removed += 1
        continue
    keep.append(l)
lines = keep
log.append("removed %d" % removed)

# 2) 插到 "static void listSavedPages" 函数结束之后
i = -1
for k, l in enumerate(lines):
    if "static void listSavedPages()" in l:
        i = k
        break
if i < 0:
    log.append("*** listSavedPages not found ***")
else:
    # 找该函数的收尾大括号（缩进 0 的 }）
    j = i
    while j < len(lines) and lines[j].strip() != "}":
        j += 1
    lines[j + 1:j + 1] = [""] + WRAP
    log.append("inserted after line %d" % (j + 1))

out = "\n".join(lines)
if crlf:
    out = out.replace("\n", "\r\n")
io.open(P, "w", encoding="utf-8", newline="").write(out)

v = io.open(P, encoding="utf-8").read()
ip = v.find("void BrowserScreen_serve")
idf = v.find("static void pageServerStart")
log.append("serve_idx=%d def_idx=%d order_ok=%s" % (ip, idf, ip > idf))
print("\n".join(log))
