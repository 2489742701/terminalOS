#pragma once

#include <lvgl.h>
#include "tactilebrowser_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  RenderInterface base;
} LvglRenderer;

LvglRenderer *lvgl_renderer_create(void);
void lvgl_renderer_destroy(LvglRenderer *renderer);

/* 链接点击回传：引擎只负责"把 URL 挂到 widget 上"，真正跳不跳、怎么进历史栈
   由 app 层决定（引擎不认识当前页/历史栈/是否正在加载）。
   ⚠️ 回调里**不要**直接 lv_obj_clean() 当前内容 —— 那个 widget 此刻正处在自己的
   事件回调里，删自己等于在事件派发循环中把 dsc 链表 free 掉（LoadProhibited）。
   正确做法：只记下 URL，等下一个 UI tick 再真正导航。 */
typedef void (*LvglLinkCallback)(const char *url);
void lvgl_renderer_set_link_callback(LvglLinkCallback cb);

/* 缩略图点击回传：参数就是那个布局节点（void* 是刻意的 —— 引擎不该知道
   app 层拿它去开全屏还是另存）。回调里只记指针，真开图放 tick 里做。 */
typedef void (*LvglImageCallback)(void *node);
void lvgl_renderer_set_image_callback(LvglImageCallback cb);

/* 诊断计数：本次渲染挂上了多少个可点链接（每次渲染前 reset，渲染后读）。 */
void lvgl_renderer_reset_link_count(void);
int lvgl_renderer_link_count(void);

RenderResult arduino_download_html(const char *url, MemoryBuffer *buffer);

/* ── 图片（缩略图）2026-09-25 ────────────────────────────────────────────
   <img> 以前只被映射成 ELEMENT_IMAGE，渲染阶段没人认识 —— 页面上一张图都没有。
   链路：dom_renderer 记绝对地址 → 后台任务挑小图下载 → 过尺寸闸 → 填 img_dsc
   → 渲染成 lv_img。下面四个函数就是这条链上的工具。 */

/* 二进制 GET。maxBytes 是硬上限，超过直接放弃（缩略图不需要原图）。
   referer 可空 —— 不少 CDN 防盗链，没 Referer 直接 403。
   返回 0=成功（*out 由调用方 heap_caps_free），-1=网络，-2=内存，-3=太大/非 200。 */
int arduino_download_binary(const char *url, uint8_t **out, size_t *outLen,
                            size_t maxBytes, const char *referer);

/* 只读文件头拿宽高，**不解码、不碰 LVGL**（后台任务里不能用 lv_img_decoder_*：
   LVGL 非线程安全）。支持的格式返回 true，不认识（webp/avif/svg…）返回 false。 */
bool tb_image_peek_size(const uint8_t *data, size_t len, int *w, int *h);

/* 接管 data 的所有权，包一个 lv_img_dsc_t。
   w/h 必须是 tb_image_peek_size 读出来的**原始**尺寸 —— LVGL 的 PNG 解码器
   不会自己去解析 PNG 头，它把 img_dsc->header 里的 w/h 原样抄走，填 0 的话
   图片尺寸就是 0×0，lv_img 什么都不显示（v1 就是栽在这里）。

   max_w / max_px 是解码后的预算：
     JPEG → 在 1/1、1/2、1/4、1/8 里挑最小能塞下的档位（tjpgd 解码时降采样）；
     PNG  → 没有降采样这一档，超预算直接返回 NULL。
   scale_out 可空，用来回传实际选中的档位（0~3）方便打日志。

   返回 NULL = 这张图不该显示（格式不认识，或 1/8 都塞不下预算）。
   ⚠️ 返回 NULL 时**没有**接管 data，调用方自己 heap_caps_free。 */
void *tb_image_dsc_create(uint8_t *data, size_t len, int w, int h,
                          int max_w, long max_px, int *scale_out);
/* 释放 dsc **和它持有的 data**。必须在 UI 线程调（内部碰 LVGL 图片缓存）。 */
void tb_image_dsc_free(void *dsc);

/* 释放一个**从来没画过**的 dsc：不碰 LVGL 图片缓存，所以后台任务里也能调。
   给"提前抓下来但最后没被任何节点认领"的图收尸用。 */
void tb_image_dsc_discard(void *dsc);

/* 把一张"原始字节 dsc"重采样成不超过 box_w × box_h 的小图（区域平均），
   返回新的 RGB565(A) dsc —— 缩略图和大图都是它做出来的，只是 box 不同。
   返回 NULL = 解不出来（渐进式 JPEG、超大图、格式不认识…）。
   ⚠️ 只能在 UI 线程调：里面要用 LVGL 的图片解码器。
   ⚠️ 内部会临时把 dsc.header.reserved 当成 JPEG 降采样档位（见
      tools/patch_lvgl_jpeg_scale.py），**不会**改动传进来的 dsc。 */
void *tb_image_resample(void *src_dsc, int box_w, int box_h,
                        int *out_w, int *out_h);

/* HTML 下载是否被截断（Content-Length 超过缓冲区上限）。
   截断时外部 CSS 下载应跳过，避免解析不完整 HTML 关联的样式表导致崩溃。 */
bool arduino_html_was_truncated();

/* 进度回调：下载/解析过程中定期调用，用于更新 UI 进度条。
    downloaded = 已处理量, total = 总量, stage = 当前阶段描述 */
typedef void (*HtmlProgressCallback)(int downloaded, int total, const char *stage);
void arduino_set_progress_callback(HtmlProgressCallback cb);

/* 协作式停止：设置 stop_flag=true 后，arduino_download_html 和 DOM 遍历会尽快退出。
   必须用 volatile 防止编译器优化掉检查。 */
void arduino_set_stop_flag(volatile bool *flag);

#ifdef __cplusplus
}
#endif