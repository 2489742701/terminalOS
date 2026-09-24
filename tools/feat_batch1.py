#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
浏览器功能改动第一批（2026-09-23 master 要求）：
 1. 去掉百度，只保留必应
 2. 页面缓存 3 槽 -> 2 槽（只留「这一页 + 上一页」）
 3. 下载的 HTML 里写入原 URL（列表页要靠它显示可读名字）
 4. 渲染完成后打印 loopTask 剩余栈（以后能提前看到水位，不用再猜）
"""
import io
import os

P = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp'

raw = io.open(P, encoding='utf-8', errors='replace').read()
NL = '\r\n' if '\r\n' in raw else '\n'
S = raw.replace('\r\n', '\n')

REPS = []

# ── 1. 去百度：引擎注释 + g_engineIdx ─────────────────────────────────────
REPS.append((
    """/* 默认必应（实测 2026-09-23，KitKat 移动 UA）：
     必应 cn.bing.com/search?q=esp32 → **59KB**，10 条结果是**静态 HTML**（li.b_algo，
     <h2> 标题 + <a href> 真链），我们的平铺渲染能直接吃下。
     百度 m.baidu.com/s?word=esp32 → **2.93MB**，光 <head> 就 380KB，
     786KB 的下载上限连正文都摸不全（实测只渲到筛选栏，27 widget），
     而且结果大概率靠 JS 渲染。所以百度留着能选，但默认走必应。 */
static int g_engineIdx = 1;                 /* 0=百度 1=必应 */""",
    """/* 搜索引擎固定为必应（2026-09-23 由 master 拍板去掉百度）：
     · 必应 cn.bing.com：桌面 UA 下 ~100KB，10 条结果是**静态 HTML**
       （li.b_algo，<h2> 标题 + <a href> 真链），平铺渲染能直接吃下。
     · 百度 m.baidu.com：2.93MB 起步（光 <head> 就 380KB），786KB 的下载上限
       连正文都摸不全，结果还靠 JS 渲染 + 反爬，对我们这种无 JS 客户端不可用了。 */"""))

# ── 2. contentReset 里清掉 g_engineBtn ────────────────────────────────────
REPS.append((
    """  if (g_content && lv_obj_is_valid(g_content)) lv_obj_clean(g_content);
  g_searchTa = nullptr;
  g_engineBtn[0] = nullptr;
  g_engineBtn[1] = nullptr;
}""",
    """  if (g_content && lv_obj_is_valid(g_content)) lv_obj_clean(g_content);
  g_searchTa = nullptr;
}"""))

REPS.append((
    """static lv_obj_t* g_engineBtn[2] = {nullptr, nullptr};
""",
    ""))

# ── 3. startSearch 只走必应 ───────────────────────────────────────────────
REPS.append((
    """  String url = (g_engineIdx == 0)
                   ? String("https://m.baidu.com/s?word=") + enc
                   : String("https://cn.bing.com/search?q=") + enc;""",
    """  String url = String("https://cn.bing.com/search?q=") + enc;"""))

# ── 4. 删除 applyEngineStyle / engine_cb ──────────────────────────────────
REPS.append((
    """static void applyEngineStyle() {
  for (int i = 0; i < 2; i++) {
    lv_obj_t* b = g_engineBtn[i];
    if (!b || !lv_obj_is_valid(b)) continue;
    bool on = (g_engineIdx == i);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_bg_opa(b, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(b, on ? lv_color_white()
                                        : lv_color_hex(0x444444), 0);
  }
}

static void engine_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_engineIdx = (int)(intptr_t)lv_event_get_user_data(e);
  applyEngineStyle();
}

""",
    ""))

# ── 5. showSearchHome 里的引擎切换 -> 去掉 ────────────────────────────────
REPS.append((
    """  /* ── 引擎切换 ── */
  lv_obj_t* engRow = makeRow(g_content, false);
  g_engineBtn[0] = makeChipBtn(engRow, "百度", engine_cb, (void*)0);
  g_engineBtn[1] = makeChipBtn(engRow, "必应", engine_cb, (void*)1);
  applyEngineStyle();
""",
    """  /* 搜索引擎固定必应 —— 百度因体积与反爬已弃用（见文件头注释），不再给切换入口 */
"""))

# ── 6. 缓存 3 槽 -> 2 槽 ──────────────────────────────────────────────────
REPS.append((
    """ * 拷贝放在 PSRAM：前进/后退/重访命中就跳过 TLS+下载，直接建布局树。
 * ⚠️ 只缓存 PAGE_CACHE_MAX_ENTRY 字节以内的大页；超了就不缓存（PSRAM 也要省着用）。 */
static const int PAGE_CACHE_SLOTS = 3;                      /* 缓存几页 */""",
    """ * 拷贝放在 PSRAM：前进/后退/重访命中就跳过 TLS+下载，直接建布局树。
 * ⚠️ 只缓存 PAGE_CACHE_MAX_ENTRY 字节以内的大页；超了就不缓存（PSRAM 也要省着用）。
 *
 * 2026-09-23 master 要求：只保留「当前页 + 上一页」两页，其余一律及时释放。
 * 所以这里是 2 槽 —— 这是**硬上限**，不是性能调优参数。想要更多请走
 * 下载（LittleFS）或设置里的缓存管理。 */
static const int PAGE_CACHE_SLOTS = 2;                      /* 当前页 + 上一页 */"""))

# ── 7. 下载时把原 URL 写进文件第一行 ──────────────────────────────────────
REPS.append((
    """  File f = LittleFS.open(path, "w");
  if (!f) { toast("写文件失败"); LittleFS.end(); return; }
  size_t w = f.write(g_pageCache[ci].data, g_pageCache[ci].len);
  f.close();""",
    """  File f = LittleFS.open(path, "w");
  if (!f) { toast("写文件失败"); LittleFS.end(); return; }
  /* 第一行写原 URL：文件名只有 hash，"下载的网站"列表页靠它显示可读名字，
     也才能在离线状态下把这个 URL 塞回页面缓存重渲染。 */
  size_t w = f.print("<!--URL:");
  w += f.print(g_currentUrl);
  w += f.print("-->\\n");
  w += f.write(g_pageCache[ci].data, g_pageCache[ci].len);
  f.close();"""))

# ── 8. 渲染后打印 loopTask 剩余栈 ─────────────────────────────────────────
REPS.append((
    """          Serial.printf("[Browser] render done. DRAM free: %u, PSRAM free: %u\\n",
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));""",
    """          Serial.printf("[Browser] render done. DRAM free: %u, PSRAM free: %u\\n",
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
          /* 主任务栈水位预警（2026-09-23 崩溃复盘加的）：
             渲染是本工程最深的调用链，loopTask 一旦见底就是整机重启 +
             触摸全失效（RGB 由 DMA 自行刷新，画面还在，极具迷惑性）。
             这里持续报数，接近 0 就该警惕递归又失控了。 */
          {
            UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
            Serial.printf("[Browser] loopTask stack: %u B left (peak used %u B)\\n",
                          (unsigned)(hwm * sizeof(StackType_t)),
                          (unsigned)(ARDUINO_LOOP_STACK_SIZE - hwm * sizeof(StackType_t)));
          }"""))

miss = []
for old, new in REPS:
    if old in S:
        S = S.replace(old, new, 1)
        print('  OK   ', old.strip().split('\n')[0][:70])
    else:
        miss.append(old.strip().split('\n')[0][:70])
        print('  MISS ', old.strip().split('\n')[0][:70])

io.open(P, 'w', encoding='utf-8', newline='').write(S.replace('\n', NL))
print('written, size =', os.path.getsize(P))
print('missed:', len(miss))
