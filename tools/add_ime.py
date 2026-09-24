#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""给浏览器搜索框接中文输入法（拼音 -> 常用字候选条）。

用法：<py> tools/add_ime.py
（Edit 工具在这个项目里会假成功，所有改源码的活都用脚本做，见 MEMORY.md）
"""
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
F = os.path.join(ROOT, "src", "app", "browser_screen.cpp")

src = io.open(F, encoding="utf-8", errors="replace").read()
nl = "\r\n" if "\r\n" in src else "\n"
orig = src


def sub(old, new, tag):
    global src
    if old not in src:
        print("MISS: " + tag)
        return False
    if new in src:
        print("SKIP(already): " + tag)
        return True
    src = src.replace(old, new, 1)
    print("OK: " + tag)
    return True


def n(s):
    return s.replace("\n", nl)


# ── 1) include ──
sub(n('#include "font_zh.h"\n'),
    n('#include "font_zh.h"\n#include "ime_pinyin.h"\n'), "include")

# ── 2) 全局状态 ──
sub(n("static lv_obj_t* g_searchTa = nullptr;\n"),
    n("""static lv_obj_t* g_searchTa = nullptr;

/* ── 中文输入法（IME）状态 ──
   lv_keyboard 自带的是纯 ASCII 键盘，没有 IME，中文一个字也打不出来。
   这里不改键盘，只监听输入框文本变化：从**尾部**抠出连续的 ASCII 小写字母
   当拼音，查 ime_pinyin.c 的表，把候选画成一条 chip 行贴在键盘上方。
   点候选 = 把尾部这串拼音替换成那个汉字 —— 中英混输、退格、接着打字都天然可用。 */
static lv_obj_t* g_imeBar = nullptr;   /* 候选条容器：挂在屏上，不跟 content 一起清 */
static char g_imePy[8] = {0};          /* 当前正在拼的拼音（不含声调） */
static int g_imePyLen = 0;             /* 拼音长度：上屏时要从文本尾部砍掉这么多字节 */
static char g_imePendHz[8] = {0};      /* 待上屏的汉字（延迟一拍用） */
static bool g_imePendSet = false;
"""), "globals")

# ── 3) IME 实现（放在 makeChipBtn 之后、showSearchHome 之前）──
IME_CODE = """
/* ═══════════════════════════════════════════════════════════════════════════
 * 中文输入法（拼音 -> 候选汉字）
 *
 * ⚠️ 上屏必须延迟一拍：候选 chip 的 CLICKED 回调里如果直接改输入框，
 *    会连锁触发 VALUE_CHANGED -> 重建候选条 -> lv_obj_clean 掉**正在派发事件的
 *    chip 自己**。这是本项目崩过好几次的模式，统一用一次性 lv_timer 绕开。
 * ═══════════════════════════════════════════════════════════════════════════ */
static void ime_pick_cb(lv_event_t* e);   /* 下面定义，ime_update 里要先引用 */
static void ime_apply_cb(lv_timer_t* t);

static void ime_hide() {
  if (g_imeBar && lv_obj_is_valid(g_imeBar))
    lv_obj_add_flag(g_imeBar, LV_OBJ_FLAG_HIDDEN);
  g_imePyLen = 0;
  g_imePy[0] = 0;
}

/* 重建候选条：从输入框尾部抠拼音 -> 查表 -> 画 chip。 */
static void ime_update() {
  if (!g_imeBar || !lv_obj_is_valid(g_imeBar)) return;
  if (!g_searchTa || !lv_obj_is_valid(g_searchTa)) return;

  const char* t = lv_textarea_get_text(g_searchTa);
  if (!t || !t[0]) { ime_hide(); return; }

  /* 只认尾部连续小写字母，最多 6 个（chuang/shuang 也就 6 个） */
  int len = (int)strlen(t);
  int p = len;
  while (p > 0 && len - p < 6) {
    unsigned char c = (unsigned char)t[p - 1];
    if (c >= 'a' && c <= 'z') p--; else break;
  }
  int pyLen = len - p;
  if (pyLen <= 0) { ime_hide(); return; }
  if (pyLen >= (int)sizeof(g_imePy)) { ime_hide(); return; }
  memcpy(g_imePy, t + p, (size_t)pyLen);
  g_imePy[pyLen] = 0;

  const char* han = ime_lookup(g_imePy);
  if (!han) { ime_hide(); return; }

  /* 3 字节一个汉字（表里全是 CJK 基本区） */
  int cnt = 0;
  while (han[cnt * 3]) cnt++;
  if (cnt <= 0) { ime_hide(); return; }

  lv_obj_clean(g_imeBar);

  lv_obj_t* pyLab = lv_label_create(g_imeBar);
  lv_label_set_text(pyLab, g_imePy);
  lv_obj_set_style_text_color(pyLab, lv_color_hex(0x66ccff), 0);
  lv_obj_set_style_text_font(pyLab, &lv_font_montserrat_16, 0);
  lv_obj_set_style_pad_right(pyLab, 6, 0);

  for (int i = 0; i < cnt; i++) {
    char hz[4];
    if (!ime_pick(han, i, hz)) break;
    lv_obj_t* b = makeChipBtn(g_imeBar, hz, ime_pick_cb, (void*)(intptr_t)i);
    lv_obj_set_size(b, 44, 36);
  }

  lv_obj_clear_flag(g_imeBar, LV_OBJ_FLAG_HIDDEN);
  lv_obj_scroll_to_x(g_imeBar, 0, LV_ANIM_OFF);
  g_imePyLen = pyLen;
}

/* 选中第 idx 个候选：只记下来，真正的改文本在下一拍做。 */
static void ime_pick_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!g_searchTa || !lv_obj_is_valid(g_searchTa)) return;
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  const char* han = ime_lookup(g_imePy);
  if (!han) return;
  if (!ime_pick(han, idx, g_imePendHz)) return;
  g_imePendSet = true;
  lv_timer_t* t = lv_timer_create(ime_apply_cb, 1, NULL);
  if (t) lv_timer_set_repeat_count(t, 1);
}

/* 上一拍选中的汉字落地：砍掉尾部拼音 + 补上汉字。 */
static void ime_apply_cb(lv_timer_t* t) {
  lv_timer_del(t);
  if (!g_imePendSet) return;
  g_imePendSet = false;
  if (!g_searchTa || !lv_obj_is_valid(g_searchTa)) return;

  const char* cur = lv_textarea_get_text(g_searchTa);
  if (!cur) return;
  String s(cur);
  int cut = g_imePyLen;
  if (cut > (int)s.length()) cut = (int)s.length();
  if (cut > 0) s.remove((unsigned int)(s.length() - cut), (unsigned int)cut);
  s += g_imePendHz;
  lv_textarea_set_text(g_searchTa, s.c_str());
  ime_update();   /* 尾部通常已经没有拼音了 -> 顺带把候选条收起来 */
}

static void ime_changed_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  ime_update();
}

"""
sub(n("static void showSearchHome() {"), IME_CODE.replace("\n", nl) + n("static void showSearchHome() {"), "ime impl")

# ── 4) 输入框挂 VALUE_CHANGED ──
sub(n("  lv_obj_add_event_cb(g_searchTa, search_go_cb, LV_EVENT_READY, NULL);\n"),
    n("  lv_obj_add_event_cb(g_searchTa, search_go_cb, LV_EVENT_READY, NULL);\n"
      "  lv_obj_add_event_cb(g_searchTa, ime_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);\n"),
    "ta cb")

# ── 5) 失焦时收起候选条 ──
sub(n("""  } else if (code == LV_EVENT_DEFOCUSED) {
    lv_obj_add_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);
  }"""),
    n("""  } else if (code == LV_EVENT_DEFOCUSED) {
    lv_obj_add_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);
    ime_hide();
  }"""), "focus hide")

# ── 6) 搜索/跳转时收起候选条 ──
sub(n("""  if (g_searchKb && lv_obj_is_valid(g_searchKb))
    lv_obj_add_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);
  g_linkPending = url;"""),
    n("""  if (g_searchKb && lv_obj_is_valid(g_searchKb))
    lv_obj_add_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);
  ime_hide();
  g_linkPending = url;"""), "search hide")

# ── 7) 候选条容器（挂在屏上，贴在键盘上方）──
sub(n("""  lv_obj_add_event_cb(g_searchKb, kb_hide_cb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(g_searchKb, kb_hide_cb, LV_EVENT_CANCEL, NULL);
"""),
    n("""  lv_obj_add_event_cb(g_searchKb, kb_hide_cb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(g_searchKb, kb_hide_cb, LV_EVENT_CANCEL, NULL);

  /* ── 中文候选条：贴在键盘上沿，横向可滑（候选最多 12 个，屏宽放不下就滑）──
     挂在屏上而不是 g_content 里：网页内容会被 lv_obj_clean 清掉。 */
  g_imeBar = lv_obj_create(scr);
  lv_obj_set_size(g_imeBar, 480, 44);
  lv_obj_align_to(g_imeBar, g_searchKb, LV_ALIGN_OUT_TOP_MID, 0, -2);
  lv_obj_set_flex_flow(g_imeBar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_imeBar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(g_imeBar, 4, 0);
  lv_obj_set_style_pad_gap(g_imeBar, 4, 0);
  lv_obj_set_style_border_width(g_imeBar, 0, 0);
  lv_obj_set_style_radius(g_imeBar, 0, 0);
  lv_obj_set_style_bg_color(g_imeBar, lv_color_hex(0x0a0a0a), 0);
  lv_obj_set_style_bg_opa(g_imeBar, LV_OPA_COVER, 0);
  lv_obj_set_scroll_dir(g_imeBar, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(g_imeBar, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(g_imeBar, LV_OBJ_FLAG_HIDDEN);
"""), "ime bar")

# ── 8) showSearchHome 里重置状态 ──
sub(n("  if (g_searchKb) lv_obj_add_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);\n  contentReset();"),
    n("  if (g_searchKb) lv_obj_add_flag(g_searchKb, LV_OBJ_FLAG_HIDDEN);\n  ime_hide();\n  contentReset();"),
    "home hide")

if src != orig:
    io.open(F, "w", encoding="utf-8", newline="") .write(src)
    print("written", F)
else:
    print("NO CHANGE")
