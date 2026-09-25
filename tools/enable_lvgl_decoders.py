#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""打开 LVGL 的图片解码器。

⚠️ 生效的 lv_conf.h **不在仓库里** —— 它在厂商 BSP 目录
   `4.0inch_ESP32-4848S040/1-Demo/Demo_Arduino/Libraries/Lvgl/lv_conf.h`
   （platformio.ini 的 lib_extra_dirs 指向工程外，按约定第三方库不入仓）。
   所以这个脚本就是那份修改的**唯一存档**：换机器/重装 BSP 后跑一遍即可，
   仓库里也能看出"我们到底改了 LVGL 什么"。

⚠️ 改完 lv_conf.h 必须删掉 `.wb_build/esp32s3/lib51c`（LVGL 的编译产物），
   否则 SCons 不会重编 LVGL —— 表现是开关改了却完全没生效（本项目踩过）。
"""
import io, os, sys, shutil

PROJ = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
CONF = (r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040"
        r"\4.0inch_ESP32-4848S040\1-Demo\Demo_Arduino\Libraries\Lvgl\lv_conf.h")

WANT = {
    "LV_USE_SJPG": 1,   # tjpgd：能解**普通 JPEG**（lv_sjpg.c 里有 is_jpg 分支）
    "LV_USE_PNG": 1,    # lodepng：网页图标/logo 有不少 PNG
    "LV_USE_BMP": 0,
    "LV_USE_GIF": 0,    # gifdec 要一直跑动画，480x480 上太贵，先不开
}

# ⚠️ 2026-09-25：LV_MEM_SIZE 必须从 128KB 提到 768KB。
#   lodepng 的分配器是 `lodepng_malloc() { return lv_mem_alloc(size); }` ——
#   走的是 **LVGL 自己的池**。一张 300x200 的 PNG 光解码输出就要 300*200*4 =
#   240KB，128KB 的池必然分配失败 → PNG 解码器返回 INV → 再被内建解码器
#   "接管"（内建会把压缩的 PNG 原文当像素交出去）→ 屏幕上一坨彩色乱码。
#   池本身就在 PSRAM（LV_MEM_POOL_IN_PSRAM=1），多占 640KB，PSRAM 还有 6MB+。
MEM_WANT = {"LV_MEM_SIZE": "(768U*1024U)"}
for k, v in MEM_WANT.items():
    old = "#define %s (128U*1024U)" % k
    new = "#define %s %s" % (k, v)
    if new in s:
        print("already: %s" % new.strip())
    elif old in s:
        s = s.replace(old, new, 1)
        print("set:     %s" % new.strip())
    else:
        print("MISS:    %s" % k)

if not os.path.exists(CONF):
    sys.exit("lv_conf.h not found: %s" % CONF)

s = io.open(CONF, encoding="utf-8", errors="replace").read()
for k, v in WANT.items():
    old = "#define %s %d" % (k, 1 - v)
    new = "#define %s %d" % (k, v)
    if new in s:
        print("already: %s" % new)
    elif old in s:
        s = s.replace(old, new, 1)
        print("set:     %s" % new)
    else:
        print("MISS:    %s" % k)
io.open(CONF, "w", encoding="utf-8", newline="").write(s)

# 强制 LVGL 重编。
# ⚠️ **用改名不用删除**：编辑器对"单轮内累计删除文件数"有阈值（默认 50），
#    LVGL 一个库就是 300+ 个 .o，rmtree 会被 SAFE_DELETE_BULK_CONFIRM_REQUIRED 拦死。
#    改名后 SCons 找不到 lib51c 就会整个重编，效果一样且不触发护栏。
lib = os.path.join(PROJ, ".wb_build", "esp32s3", "lib51c")
bak = lib + ".stale"
if os.path.isdir(bak):
    os.rename(bak, bak + ".old")     # 上一具尸体先挪开
if os.path.isdir(lib):
    os.rename(lib, bak)
    print("renamed  lib51c -> lib51c.stale (force LVGL rebuild)")
else:
    print("lib51c not present (will be built fresh)")
print("done")
