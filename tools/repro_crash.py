#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
崩溃复现探针：模拟 master 的三级跳转，全程记录 DRAM/PSRAM 水位，
并捕获 panic / LoadProhibited / reboot 关键字。

用法: python repro_crash.py <COM口> [输出文件]
"""
import sys
import time
import re
import os
import io
import ssl
import urllib.request

try:
    import serial
except ImportError:
    print("ERROR: pyserial missing")
    sys.exit(1)

OUT = sys.argv[2] if len(sys.argv) > 2 else "repro_crash.txt"
PORT = sys.argv[1] if len(sys.argv) > 1 else "COM7"
BAUD = 115200

UA_DESKTOP = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
              "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36")

log_lines = []


def out(s=""):
    print(s)
    log_lines.append(s)


def fetch(url, ua=UA_DESKTOP, timeout=25):
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    req = urllib.request.Request(url, headers={
        "User-Agent": ua,
        "Accept": "text/html,application/xhtml+xml",
        "Accept-Language": "zh-CN,zh;q=0.9",
    })
    return urllib.request.urlopen(req, timeout=timeout, context=ctx).read()


def pick_links(html, limit=12):
    """提取结果里可点的绝对 http(s) 链接"""
    links = []
    for m in re.finditer(r'href="(https?://[^"]+)"', html):
        u = m.group(1)
        if "bing.com" in u or "microsoft" in u or "msn.com" in u:
            continue
        if u.endswith((".css", ".js", ".png", ".jpg", ".ico", ".svg")):
            continue
        links.append(u)
    seen = set()
    res = []
    for u in links:
        if u in seen:
            continue
        seen.add(u)
        res.append(u)
        if len(res) >= limit:
            break
    return res


def wait_until(ser, predicate, timeout_s, idle_timeout_s=None):
    """读串口直到 predicate(行) 为真；idle_timeout_s 内无新行则返回 False"""
    deadline = time.time() + timeout_s
    last = time.time()
    buf = []
    while time.time() < deadline:
        n = ser.in_waiting
        if n:
            raw = ser.read(n)
            try:
                txt = raw.decode("utf-8", "replace")
            except Exception:
                txt = str(raw)
            for line in txt.split("\n"):
                line = line.rstrip("\r")
                buf.append(line)
                out("    | " + line)
                if predicate(line):
                    return True, buf
            last = time.time()
        else:
            if idle_timeout_s and (time.time() - last) > idle_timeout_s:
                return False, buf
            time.sleep(0.05)
    return False, buf


MEM_RE = re.compile(r"DRAM free:\s*(\d+), PSRAM free:\s*(\d+)")


def read_mem(ser):
    ser.reset_input_buffer()
    ser.write(b"mem\n")
    ser.flush()
    ok, buf = wait_until(ser, lambda l: "PSRAM" in l or "free" in l.lower(), 8, 2)
    joined = "\n".join(buf)
    m = MEM_RE.search(joined)
    if m:
        return int(m.group(1)), int(m.group(2))
    # 退而求其次：抓任意数字对
    for l in buf:
        if "DRAM" in l and "PSRAM" in l:
            nums = re.findall(r"(\d{3,})", l)
            if len(nums) >= 2:
                return int(nums[-2]), int(nums[-1])
    return None, None


def main():
    out("=== repro_crash: 准备三级跳转 URL ===")
    try:
        h1 = fetch("https://cn.bing.com/search?q=esp32").decode("utf-8", "replace")
        out("  [PC] bing SERP %d B, b_algo=%d" % (len(h1), h1.count("b_algo")))
        l2 = pick_links(h1)
        out("  [PC] 二级候选 %d 个" % len(l2))
        for u in l2[:5]:
            out("       " + u[:110])
    except Exception as e:
        out("  [PC] 抓取失败: %r  -> 使用硬编码兜底" % (e,))
        l2 = []

    url1 = "https://cn.bing.com/search?q=esp32"
    url2 = l2[0] if l2 else "https://en.wikipedia.org/wiki/ESP32"
    url3 = ""

    if l2:
        try:
            h2 = fetch(url2).decode("utf-8", "replace")
            out("  [PC] 二级页 %d B" % len(h2))
            l3 = pick_links(h2)
            out("  [PC] 三级候选 %d 个" % len(l3))
            # 选同域的
            host = re.sub(r"^https?://", "", url2).split("/")[0]
            for u in l3:
                if host in u:
                    url3 = u
                    break
            if not url3 and l3:
                url3 = l3[0]
            out("  [PC] 三级 URL: " + (url3[:110] or "(无)"))
        except Exception as e:
            out("  [PC] 二级抓取失败: %r" % (e,))

    out("")
    out("=== 连接设备 %s ===" % PORT)
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.5)
    except Exception as e:
        out("串口打开失败: %r" % (e,))
        io.open(OUT, "w", encoding="utf-8").write("\n".join(log_lines))
        return
    time.sleep(0.3)
    ser.reset_input_buffer()

    steps = [
        ("1 必应搜索 esp32", url1),
        ("2 点进第一条结果", url2),
        ("3 再点里面的链接", url3),
    ]

    watermark = []
    for name, u in steps:
        if not u:
            out("  [%s] 无 URL，跳过" % name)
            continue
        out("")
        out("---- STEP %s ----" % name)
        d0, p0 = read_mem(ser)
        out("  >> 进入前 DRAM=%s PSRAM=%s" % (d0, p0))
        ser.write(("browser " + u + "\n").encode("utf-8"))
        # 等待本次渲染结束（render done / error / 已加载 / 失败 关键字）
        ok, buf = wait_until(
            ser,
            lambda l: ("render done." in l or "FATAL" in l or
                       "Guru Meditation" in l or "LoadProhibited" in l or
                       "panic" in l.lower() or "rst:" in l or
                       "已加载" in l or "渲染失败" in l),
            95, 25)
        joined = "\n".join(buf)
        if not ok:
            out("  !! 未看到结束标志（超时或静默）")
        d1, p1 = read_mem(ser)
        out("  >> 渲完后 DRAM=%s PSRAM=%s" % (d1, p1))
        if d0 and d1:
            out("  >> ΔDRAM = %+d" % (d1 - d0))
            watermark.append((name, d0, d1, p0, p1))

    out("")
    out("=== 水位汇总 ===")
    for name, d0, d1, p0, p1 in watermark:
        out("  %-22s DRAM %7d -> %7d  (%+d)   PSRAM %8d -> %8d"
            % (name, d0, d1, d1 - d0, p0, p1))

    # 再等一会，看是否会自发崩溃
    out("")
    out("=== 静置 25s 观察自发崩溃 ===")
    ok, buf = wait_until(
        ser,
        lambda l: ("Guru Meditation" in l or "LoadProhibited" in l or
                   "panic" in l.lower() or "rst:0x" in l or
                   "abort()" in l or "Stack canary" in l),
        25, None)
    if ok:
        out("  !! 检测到崩溃迹象")
    else:
        out("  静置期间无崩溃")

    ser.close()
    io.open(OUT, "w", encoding="utf-8", newline="\r\n").write("\n".join(log_lines))
    print("\nwritten ->", OUT)


if __name__ == "__main__":
    main()
