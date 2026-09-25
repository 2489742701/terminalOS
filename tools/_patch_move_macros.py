# -*- coding: utf-8 -*-
"""把图片那组宏提到文件靠前的位置。

loadFullImage / openImageViewer 在 565 行，而 IMG_MAX_* 定义在 940 行那一大段
注释中间 —— 要么把宏提前，要么把函数挪后。挪函数会牵动一堆调用点，提前宏更省事。
⚠️ 同时删掉后面重复的定义（不然 -Wmacro-redefined）。
"""
import io

B = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp'
s = io.open(B, encoding='utf-8').read()

# ---- 1) 从图片段摘掉宏定义 ----
old = """#define IMG_MAX         6         /* 一页最多几张图 */
#define IMG_MAX_BYTES   65536     /* 单张下载字节上限 */
/* 整个图片阶段的时间预算。几张图把页面加载拖成分钟级是不可接受的 ——
   预算用完就放弃剩下的，页面该渲染渲染（顶多少几张缩略图）。 */
#define IMG_TOTAL_MS    20000
#define IMG_MAX_W       464       /* 解码后宽度上限（= 内容区宽，超了戳出右边） */
#define IMG_MAX_PIXELS  110000    /* 解码后像素上限：w*h*3 ≈ 330KB 中间缓冲 */
"""
new = """/* 上面那几个宏（IMG_MAX / IMG_MAX_BYTES / IMG_TOTAL_MS / IMG_MAX_W /
   IMG_MAX_PIXELS）已经提到文件前半段去了 —— openImageViewer 要用它们，
   而它比这一节靠前得多。门槛的取舍说明见下面"缩略图"注释块。 */
"""
assert s.count(old) == 1, ('remove', s.count(old))
s = s.replace(old, new, 1)

# ---- 2) 插到前向声明块里 ----
anchor = """#define BLOB_HDR 12
static uint8_t* blobPack("""
macros = """/* 图片门槛（原本在"缩略图"那一节，因为 openImageViewer 更早要用，提到这里）：
     IMG_MAX         一页最多几张图
     IMG_MAX_BYTES   单张下载字节上限
     IMG_TOTAL_MS    整个图片阶段的时间预算：几张图把页面加载拖成分钟级不可接受，
                     预算用完就放弃剩下的，页面该渲染渲染（顶多少几张缩略图）
     IMG_MAX_W       解码后宽度上限（= 内容区宽，超了戳出右边）
     IMG_MAX_PIXELS  解码后像素上限：w*h*3 ≈ 330KB 中间缓冲 */
#define IMG_MAX         6
#define IMG_MAX_BYTES   65536
#define IMG_TOTAL_MS    20000
#define IMG_MAX_W       464
#define IMG_MAX_PIXELS  110000

#define BLOB_HDR 12
static uint8_t* blobPack("""
assert s.count(anchor) == 1, ('insert', s.count(anchor))
s = s.replace(anchor, macros, 1)

io.open(B, 'w', encoding='utf-8', newline='').write(s)
print('ok')
