"""把误插到 listSavedPages 函数体中间的两行挪到函数结束之后。"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp"

t = io.open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in t
lines = t.replace("\r\n", "\n").split("\n")
log = []

WRAP = ["void BrowserScreen_serve(bool on) { on ? pageServerStart() : pageServerStop(); }",
        "void BrowserScreen_listPages() { listSavedPages(); }"]

# 删掉这两行（任何位置）
keep = []
removed = 0
for l in lines:
    if l.strip() in WRAP:
        removed += 1
        continue
    keep.append(l)
lines = keep
log.append("removed %d" % removed)

# 找到 listSavedPages 函数体真正的收尾：从函数头开始数括号
i = -1
for k, l in enumerate(lines):
    if l.startswith("static void listSavedPages()"):
        i = k
        break
if i < 0:
    log.append("*** not found ***")
else:
    depth = 0
    j = i
    started = False
    while j < len(lines):
        depth += lines[j].count("{") - lines[j].count("}")
        if "{" in lines[j]:
            started = True
        if started and depth == 0:
            break
        j += 1
    lines[j + 1:j + 1] = [""] + WRAP
    log.append("inserted after line %d (func ends at %d)" % (j + 1, j + 1))

out = "\n".join(lines)
if crlf:
    out = out.replace("\n", "\r\n")
io.open(P, "w", encoding="utf-8", newline="").write(out)

v = io.open(P, encoding="utf-8").read()
log.append("serve after def: %s" % (v.find("void BrowserScreen_serve") > v.find("static void pageServerStart")))
log.append("serve after listSavedPages: %s" % (v.find("void BrowserScreen_serve") > v.find("static void listSavedPages")))
print("\n".join(log))
