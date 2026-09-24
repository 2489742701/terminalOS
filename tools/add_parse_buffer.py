"""新增 tactilebrowser_parse_html_buffer：跳过下载，直接用内存里的 HTML 建布局树。

用途：页面缓存命中时（前进/后退/重访）不必再联网，直接拿 PSRAM 里那份缓存渲染。
与 download_and_parse 同构，只是把"下载"换成"自拷一份入参"。
入参 html 由调用方持有，这里自己拷一份，调用方返回后可立即释放。
"""
import io

CPP = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\tactilebrowser_core.cpp"
HDR = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\include\tactilebrowser_core.h"

FUNC = r'''
/* Phase 1 变体：HTML 已在内存里（页面缓存命中），跳过下载直接建布局树。
   与 tactilebrowser_download_and_parse 同构，同样不触碰 LVGL，可在后台任务跑。
   ⚠️ 入参 html 由调用方持有，这里**自己拷一份**，所以调用方返回后立刻释放也没事。 */
RenderResult tactilebrowser_parse_html_buffer(const char *url, const char *html,
                                              size_t length, int max_width,
                                              int max_height,
                                              volatile bool *stop_flag,
                                              LayoutNode **out_layout) {
  if (!url || !html || length == 0 || !out_layout || !global_renderer)
    return RENDER_ERROR_UNKNOWN;
  *out_layout = NULL;

  dom_renderer_set_stop_flag(stop_flag);

  char *copy = (char *)tb_alloc(length);
  if (!copy) {
    dom_renderer_set_stop_flag(nullptr);
    return RENDER_ERROR_MEMORY;
  }
  memcpy(copy, html, length);

  lxb_html_document_t *document = html_parser.parse_html(copy, length);
  free(copy);  /* DOM 已解析，这份拷贝可以释放 */
  if (!document) {
    arduino_set_stop_flag(nullptr);
    dom_renderer_set_stop_flag(nullptr);
    return RENDER_ERROR_PARSE;
  }

  global_renderer_struct.platform_data = NULL;  /* Phase 1 不触碰 LVGL */
  RenderContext context = {.renderer = &global_renderer_struct,
                           .root_container = NULL,
                           .current_y = 0,
                           .max_width = max_width,
                           .max_height = max_height,
                           .document_url = url};

  RenderResult build_result =
      dom_renderer_build_layout_only(document, &context, out_layout);

  lxb_html_document_destroy(document);

  arduino_set_stop_flag(nullptr);
  dom_renderer_set_stop_flag(nullptr);
  return build_result;
}
'''

DECL = r'''
/* Phase 1 变体：HTML 已在内存（页面缓存命中），跳过下载直接建布局树。
   html 由调用方持有，内部自拷一份。 */
RenderResult tactilebrowser_parse_html_buffer(const char *url, const char *html,
                                              size_t length, int max_width,
                                              int max_height,
                                              volatile bool *stop_flag,
                                              LayoutNode **out_layout);
'''

log = []

t = io.open(CPP, encoding="utf-8", newline="").read()
if "tactilebrowser_parse_html_buffer" in t:
    log.append("cpp: ALREADY")
else:
    A = "/* Phase 2: 渲染布局树到 LVGL 控件（快速，在 UI 任务中调用） */"
    if A not in t:
        log.append("cpp: *** ANCHOR NOT FOUND ***")
    else:
        t = t.replace(A, FUNC.lstrip("\n") + "\n" + A, 1)
        io.open(CPP, "w", encoding="utf-8", newline="").write(t)
        log.append("cpp: patched")
log.append("cpp verify: %s" % (
    "tactilebrowser_parse_html_buffer" in io.open(CPP, encoding="utf-8").read()))

h = io.open(HDR, encoding="utf-8", newline="").read()
if "tactilebrowser_parse_html_buffer" in h:
    log.append("hdr: ALREADY")
else:
    A = "RenderResult tactilebrowser_render_layout(LayoutNode *layout_root,"
    if A not in h:
        log.append("hdr: *** ANCHOR NOT FOUND ***")
    else:
        h = h.replace(A, DECL.lstrip("\n") + "\n" + A, 1)
        io.open(HDR, "w", encoding="utf-8", newline="").write(h)
        log.append("hdr: patched")
log.append("hdr verify: %s" % (
    "tactilebrowser_parse_html_buffer" in io.open(HDR, encoding="utf-8").read()))

print("\n".join(log))
