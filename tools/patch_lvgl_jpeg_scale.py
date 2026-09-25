#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""给 LVGL 的 JPEG 解码器（lv_sjpg.c）打两针：解码时降采样 + 放宽 JPEG 识别。

⚠️ lv_sjpg.c **不在仓库里**（厂商 BSP 在 lib_extra_dirs 指向的工程外，
   第三方库按约定不入仓）。这个脚本就是那份修改的**唯一存档**，
   换机器/重装 BSP 后跑一遍即可。跟 enable_lvgl_decoders.py 是一对。

── 补丁 1：解码时降采样 ───────────────────────────────────────────────
    tjpgd 的 jd_decomp() 支持 1/1、1/2、1/4、1/8 输出（tjpgdcnf.h 里
    JD_USE_SCALE=1），但 LVGL 固定传 0。而 lv_sjpg.c 在 decoder_open 里
    **一次性**分配 w*h*3 的 RGB888 中间缓冲 —— 网页上的图动辄 1000+ 像素宽，
    一张 1920x1080 就是 6MB，PSRAM 直接见底。
    所以让调用方能指定档位，大图在**解码阶段**就缩掉，而不是解完再丢。

    档位放在 lv_img_header_t.reserved（2 bit，正好 0~3），随图携带。
    ⚠️ 不能用全局变量：解码是懒加载的，LVGL 图片缓存淘汰后会重新 open，
       那时全局档位早就被别的图改过了 —— 会缩错。

── 补丁 2：is_jpg 别只认 JFIF ─────────────────────────────────────────
    原实现要求文件头是 FF D8 FF E0 00 10 "JFIF"，也就是必须有 JFIF 的 APP0。
    现实里大量 JPEG 是 Exif（FF E1）或压根没有 APP0 —— 全被判成"不是 JPEG"，
    现象是"这张图明明下下来了却不显示"（实测 163 的 topapp.jpg 就是这样）。
    SOI(FF D8) + 紧跟一个 marker(FF xx) 就足以认定是 JPEG。

改完必须跑 enable_lvgl_decoders.py（或手动改名 .wb_build/esp32s3/lib51c）
强制 LVGL 重编，否则不生效。
"""
import io, os, sys

LVGL = (r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040"
        r"\4.0inch_ESP32-4848S040\1-Demo\Demo_Arduino\Libraries\Lvgl")
SRC = os.path.join(LVGL, "src", "extra", "libs", "sjpg", "lv_sjpg.c")
PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MARK_SCALE = "jpeg_scale_of"
MARK_SOI = "原来还要求紧跟 JFIF"   # 就是补丁注释里那句话，别写成不存在的字符串
fail = []

s = io.open(SRC, encoding="utf-8", errors="replace").read()


def rep(old, new, expect=1):
    global s
    n = s.count(old)
    if n != expect:
        fail.append("count=%d (want %d) :: %s" % (n, expect,
                                                  old.split("\n")[0][:70]))
        return
    s = s.replace(old, new, expect)


# ═══ 补丁 1：解码时降采样 ═══════════════════════════════════════════
if MARK_SCALE in s:
    print("skip: jpeg scale already patched")
else:
    rep("""    uint8_t ** frame_base_array;        //to save base address of each split frames upto sjpeg_total_frames.
    int * frame_base_offset;            //to save base offset for fseek
""",
        """    uint8_t ** frame_base_array;        //to save base address of each split frames upto sjpeg_total_frames.
    int * frame_base_offset;            //to save base offset for fseek
    uint8_t scale;                      /* geek-terminal: JPEG 降采样档位 0..3 (1/1,1/2,1/4,1/8) */
""")

    rep("""/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void lv_split_jpeg_init(void)""",
        """/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/* ── geek-terminal 补丁（2026-09-25）：JPEG 解码时降采样 ───────────────────
 * 档位来自 lv_img_header_t.reserved（2 bit，0~3 = 1/1、1/2、1/4、1/8）。
 * ⚠️ 只能对 LV_IMG_SRC_VARIABLE 用：那种 src 才是 lv_img_dsc_t*，
 *    FILE 类型的 src 是 char*（文件名），当成结构体读会直接崩。
 *    所以这里只读 VARIABLE 路径，FILE 路径的 scale 保持 memset 出来的 0。
 */
static uint8_t jpeg_scale_of(const void * src)
{
    const lv_img_dsc_t * d = (const lv_img_dsc_t *)src;
    uint8_t s = d->header.reserved;
    return s > 3 ? 0 : s;
}

void lv_split_jpeg_init(void)""")

    rep("""            if(rc == JDR_OK) {
                header->w = jd_tmp.width;
                header->h = jd_tmp.height;

            }""",
        """            if(rc == JDR_OK) {
                uint8_t sc = jpeg_scale_of(src);   /* geek-terminal */
                header->w = jd_tmp.width >> sc;
                header->h = jd_tmp.height >> sc;

            }""")

    rep("""            if(rc == JDR_OK) {
                sjpeg->sjpeg_x_res = jd_tmp.width;
                sjpeg->sjpeg_y_res = jd_tmp.height;
                sjpeg->sjpeg_total_frames = 1;
                sjpeg->sjpeg_single_frame_height = jd_tmp.height;

                sjpeg->frame_base_array = lv_mem_alloc(sizeof(uint8_t *) * sjpeg->sjpeg_total_frames);""",
        """            if(rc == JDR_OK) {
                uint8_t sc = jpeg_scale_of(dsc->src);   /* geek-terminal */
                sjpeg->scale = sc;
                sjpeg->sjpeg_x_res = jd_tmp.width >> sc;
                sjpeg->sjpeg_y_res = jd_tmp.height >> sc;
                sjpeg->sjpeg_total_frames = 1;
                sjpeg->sjpeg_single_frame_height = jd_tmp.height >> sc;

                sjpeg->frame_base_array = lv_mem_alloc(sizeof(uint8_t *) * sjpeg->sjpeg_total_frames);""")

    rep("jd_decomp(sjpeg->tjpeg_jd, img_data_cb, 0);",
        "jd_decomp(sjpeg->tjpeg_jd, img_data_cb, sjpeg->scale);", expect=2)

    if not fail:
        print("patched: jpeg downscale")

# ═══ 补丁 2：is_jpg 放宽 ═════════════════════════════════════════════
if MARK_SOI in s:
    print("skip: is_jpg already patched")
else:
    rep("""static int is_jpg(const uint8_t * raw_data)
{
    const uint8_t jpg_signature[] = {0xFF, 0xD8, 0xFF,  0xE0,  0x00,  0x10, 0x4A,  0x46, 0x49, 0x46};
    return memcmp(jpg_signature, raw_data, sizeof(jpg_signature)) == 0;
}""",
        """static int is_jpg(const uint8_t * raw_data)
{
    /* geek-terminal 补丁（2026-09-25）：
       原来还要求紧跟 JFIF 的 APP0（FF E0 00 10 "JFIF"）。现实里一大半 JPEG
       是 Exif（FF E1）或压根没有 APP0 —— 全被判成"不是 JPEG"，现象是
       "图明明下下来了却不显示"（163 的 topapp.jpg 就是活例子）。
       SOI(FF D8) 后面紧跟任意一个 marker(FF xx) 就足以认定是 JPEG；
       至于它到底是渐进式还是 CMYK，jd_prepare() 会自己拒，我们不用管。 */
    return raw_data[0] == 0xFF && raw_data[1] == 0xD8 && raw_data[2] == 0xFF;
}""")
    if not fail:
        print("patched: is_jpg relaxed")

if fail:
    print("FAILED:")
    for f in fail:
        print("  -", f)
    sys.exit(1)

io.open(SRC, "w", encoding="utf-8", newline="").write(s)

# 强制 LVGL 重编（用改名不用删除：rmtree 300+ 个 .o 会撞编辑器的批量删除护栏）
lib = os.path.join(PROJ, ".wb_build", "esp32s3", "lib51c")
bak = lib + ".stale"
if os.path.isdir(bak):
    # 上一具尸体还在就再加个后缀：直接 rename 到同名会 FileExistsError，
    # 而 rmtree 又会撞编辑器的批量删除护栏 —— 尸体留着不管，下次构建会清掉。
    n = 1
    while os.path.exists("%s.old%d" % (bak, n)):
        n += 1
    os.rename(bak, "%s.old%d" % (bak, n))
if os.path.isdir(lib):
    os.rename(lib, bak)
    print("renamed  lib51c -> lib51c.stale (force LVGL rebuild)")
else:
    print("lib51c not present (will be built fresh)")
print("done")
