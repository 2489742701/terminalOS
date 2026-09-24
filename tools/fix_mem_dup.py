import io
P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\MEMORY.md"
t = io.open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in t
lines = t.replace("\r\n", "\n").split("\n")
# 去掉连续重复的 "## 排障手法"
out = []
prev = None
removed = 0
for l in lines:
    if l.strip() == "## 排障手法" and prev == "## 排障手法":
        removed += 1
        continue
    out.append(l)
    prev = l.strip()
t = "\n".join(out)
if crlf:
    t = t.replace("\n", "\r\n")
io.open(P, "w", encoding="utf-8", newline="").write(t)
v = io.open(P, encoding="utf-8").read()
print("removed dup headers:", removed)
print("count now:", v.count("## 排障手法"))
print("len:", len(v))
