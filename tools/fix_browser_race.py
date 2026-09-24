"""
fix_browser_race.py - 修浏览器后台任务与主线程拆树的竞态（实机 LoadProhibited）+ 加 perfx 诊断

崩溃实锤（2026-09-24，COM7）：
  back_async_cb -> nav_back_home -> nav_release_all_except -> lv_obj_del(browser)
  -> lv_obj_invalidate -> lv_obj_area_is_visible -> lv_obj_get_screen 崩。
  lv_obj_get_screen 是爬 parent 链的，崩 = 链上有野指针 = 对象树已被写坏。

真因：fetch 任务跑在**另一个任务**里，却直接操作 LVGL 对象
  （lv_obj_clear_flag(g_loadingOverlay,...)、往内容容器塞 widget）。
  BrowserScreen_close() 只丢了个 g_stopRequested = true 就立刻 contentReset()
  + 让 nav 去 lv_obj_del —— 树已经拆了，任务手里全是悬空指针，它再写一次
  就把树写坏，崩在**下一次**遍历的时候（所以栈看着像"删 clock 崩"，
  其实 clock 是无辜的，被破坏的是共享的对象树）。

修法：close() 里发停止请求后**轮询等任务自己退出**，等到了再动对象。
"""
import io, os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))


def rw(path, fn):
    p = os.path.join(ROOT, path)
    raw = io.open(p, 'r', encoding='utf-8', newline='').read()
    crlf = '\r\n' in raw
    s = raw.replace('\r\n', '\n')
    o = s
    s = fn(s)
    if s != o:
        if crlf:
            s = s.replace('\n', '\r\n')
        io.open(p, 'w', encoding='utf-8', newline='').write(s)
        print('WROTE ' + p)
    else:
        print('NO CHANGE ' + p)


# ---------- 1) browser_screen.cpp：close 里等任务退出 + 新增 isBusy ----------
def f_bcpp(s):
    old = """void BrowserScreen_close() {
  /* 若后台任务还在跑，让它尽快退出；常驻任务不会被删除 */
  g_stopRequested = true;
  g_hasPending = false;
  g_pendingUrl = "";
"""
    new = """void BrowserScreen_close() {
  /* 若后台任务还在跑，让它尽快退出；常驻任务不会被删除 */
  g_stopRequested = true;
  g_hasPending = false;
  g_pendingUrl = "";

  /* ⚠️ 必须等后台任务真正停下来，才能动对象树。
     2026-09-24 实机 LoadProhibited：nav_back_home -> lv_obj_del 崩在
     lv_obj_get_screen（爬 parent 链）—— 链被写坏了。
     根因：fetch 任务跑在另一个任务里，却直接操作 LVGL 对象（显示/隐藏
     g_loadingOverlay、往内容容器里塞 widget）。主线程这边一旦 contentReset()
     / lv_obj_del() 把树拆掉，它手里就全是悬空指针，再写一次就把树写坏，
     崩在"下一次"遍历时 —— 所以栈看着像 clock 崩，clock 其实是无辜的。
     这里轮询等 g_state 自己离开 LOADING；上限 300 x 10ms = 3 秒，
     网络卡死也不会把主线程锁死（超时就打 WARN 按原行为继续）。 */
  int waited = 0;
  while (g_state == BROWSER_LOADING && waited < 300) {
    delay(10);
    waited++;
  }
  if (g_state == BROWSER_LOADING) {
    Serial.println("[Browser] WARN: fetch task still running, releasing anyway");
  } else if (waited) {
    Serial.printf("[Browser] fetch task stopped after %d ms\\n", waited * 10);
  }
"""
    assert old in s, 'browser_screen.cpp close anchor'
    s = s.replace(old, new, 1)

    # 新增 isBusy，放在 preinit 之前
    old2 = """/* ── 启动早期调用：一次性建好常驻后台任务 ── */
void BrowserScreen_preinit() {"""
    new2 = """/* 后台任务是否还在跑。nav 拿它当释放守卫：任务没停就拆树 = 悬空指针。 */
bool BrowserScreen_isBusy() {
  return g_state == BROWSER_LOADING;
}

/* ── 启动早期调用：一次性建好常驻后台任务 ── */
void BrowserScreen_preinit() {"""
    assert old2 in s, 'browser_screen.cpp preinit anchor'
    return s.replace(old2, new2, 1)


# ---------- 2) browser_screen.h：声明 ----------
def f_bh(s):
    old = 'void BrowserScreen_close();'
    new = ('void BrowserScreen_close();\n'
           '/* 后台 fetch 任务是否仍在运行（nav 用它做释放守卫） */\n'
           'bool BrowserScreen_isBusy();')
    assert old in s, 'browser_screen.h anchor'
    return s.replace(old, new, 1)


# ---------- 3) nav.cpp：给 browser 挂 canRelease ----------
def f_nav(s):
    old = '''bool wifiCanRelease() { return !WifiScreen_isConnecting(); }'''
    new = ('''bool wifiCanRelease() { return !WifiScreen_isConnecting(); }

/* 浏览器同理：后台 fetch 任务在别的任务里直接操作 LVGL 对象，任务没停就
   lv_obj_del 整棵树 = 它手里变悬空指针，再写一次就把对象树写坏（实机崩在
   lv_obj_get_screen）。BrowserScreen_close() 内部已经会等任务退出，这里再
   兜一层：万一还停不下来，宁可这次不释放，也别把树写坏。 */
bool browserCanRelease() { return !BrowserScreen_isBusy(); }''')
    assert old in s, 'nav.cpp wifiCanRelease anchor'
    s = s.replace(old, new, 1)

    old2 = '''    {"browser",  &nav_browser,  BrowserScreen_create,  nullptr,         BrowserScreen_close},'''
    new2 = '''    {"browser",  &nav_browser,  BrowserScreen_create,  browserCanRelease, BrowserScreen_close},'''
    assert old2 in s, 'nav.cpp table anchor'
    return s.replace(old2, new2, 1)


rw('src/app/browser_screen.cpp', f_bcpp)
rw('src/app/browser_screen.h', f_bh)
rw('src/app/nav.cpp', f_nav)
print('DONE')
