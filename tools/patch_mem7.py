#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""按行号精确更新长期记忆里的必应段 + 插入诊断小节。"""
import io
import os
import sys

MEM = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory\MEMORY.md"

NEW_BING = [
    "- **默认引擎 = 必应，且必应必须走桌面 Chrome120 UA**（2026-09-23 实测）：",
    "  移动 UA（KitKat/Android13/iPhone）只给 **5 条** `li.b_algo` + **0 个分页标记**",
    "  （`first=` 被服务端直接忽略）；**桌面 UA → 10 条 + 真的「下一页」**，~100KB。",
    "  百度/其它站点仍用 KitKat（桌面 UA 会撞 wappass 验证码）。",
    "  ⚠️ 「下一页」是**假分页**：href 带 `FPIG=<32位hex>` token，但裸 `first=11` /",
    "    带 FPIG / 再加 cookie+Referer，三种方式与第 1 页相比**新增结果都是 0** ——",
    "    必应对无 JS 客户端只下发固定 ~10 条语料。留着当官方入口，别指望翻出新东西。",
    "  ⚠️ `www.bing.com` 会被地域 302 到 `cn.bing.com`。",
    "  ⚠️ 这个网络里 DDG/Mojeek/searx/Brave/Marginalia 全不可达；360 351KB 且 `pn=` 无效、",
    "    搜狗 296KB 的「下一页」是 JS 按钮、百度 2.7MB 超 786KB 上限。→ 只能选必应。",
]

NEW_DIAG = [
    "- ✅ **SERP 壳子过滤** `flat_is_serp_chrome_link()`：丢 `filters=ex1`（时间筛选）/",
    "  `qpvt=`（全部·时间不限）/ `FORM=HDRSC`（顶部导航图片视频学术…）/ 垂搜路径 /",
    "  `first=` 且**文本是 1~3 位纯数字**（页码「2」「3」）；`FORM=PORE`（下一页）反向保留。",
    "  ⚠️ `first=` **不能无条件删**：`<li>` 会从子树继承 href（`subtree_first_href`），",
    "  分页容器 `<li class=b_pag>` 会被连坐掉，「下一页」跟着消失（踩过）。",
    "- ✅ **诊断**：串口 `flatdump <n>` 调平铺 dump 的行上限（默认 60）。分页排在第 60 行",
    "  之后，日志里看不到会误判成「没渲染」。",
]


def main():
    with io.open(MEM, "r", encoding="utf-8", newline="") as f:
        raw = f.read()
    lines = raw.split("\n")
    crlf = "\r\n" in raw

    # 找到并替换「默认引擎 = 必应」那一段（1-based 87..88）
    idx = None
    for i, l in enumerate(lines):
        if "默认引擎 = 必应" in l:
            idx = i
            break
    if idx is None:
        print("[FAIL] 找不到必应段")
        return 1
    # 该段到下一行以 "- " 开头的行前结束（保留百度那行）
    end = idx + 1
    while end < len(lines) and not lines[end].lstrip().startswith("- "):
        end += 1
    print("[INFO] 替换 %d..%d" % (idx + 1, end))
    lines[idx:end] = NEW_BING

    # 在「平铺跳过外部 CSS」之前插入诊断小节
    j = None
    for i, l in enumerate(lines):
        if "平铺跳过外部 CSS" in l:
            j = i
            break
    if j is None:
        print("[FAIL] 找不到平铺锚点")
        return 1
    lines[j:j] = NEW_DIAG

    out = "\n".join(lines)
    with io.open(MEM, "w", encoding="utf-8", newline="") as f:
        f.write(out)
    with io.open(MEM, "r", encoding="utf-8", newline="") as f:
        back = f.read()
    ok = "flatdump" in back and "桌面 Chrome120" in back
    print("[%s] 回读校验 (CRLF=%s)" % ("OK" if ok else "FAIL", crlf))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
