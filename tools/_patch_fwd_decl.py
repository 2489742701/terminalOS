# -*- coding: utf-8 -*-
"""给 browser_screen.cpp 补前向声明。

⚠️ loadFullImage / downloadImage 在文件靠前的位置（565 / 715 行），
   而对象存储那套工具函数定义在 1010 行之后 —— C/C++ 要求先声明后使用。
   不改就是一串 "was not declared in this scope"。
"""
import io

B = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\app\browser_screen.cpp'
s = io.open(B, encoding='utf-8').read()

anchor = """/* 按需加载原图：优先从本地读，盘上没有才联网。"""
decl = """/* 对象存储（定义在下面的"本地对象存储"段）：这几个在 loadFullImage /
   downloadImage 里就要用，而它们的位置比定义处靠前 —— 先声明一下。 */
#define BLOB_HDR 12
static uint8_t* blobPack(const char* magic, int w, int h,
                         const uint8_t* payload, size_t len, size_t* outLen);
static bool blobUnpack(const uint8_t* b, size_t n, const char* magic,
                       int* w, int* h, const uint8_t** payload, size_t* plen);
static bool storeSave(const char* name, const uint8_t* data, size_t len);
static bool storeLoad(const char* name, uint8_t** out, size_t* outLen);
static void imgBlobName(char* out, size_t cap, const char* url, bool thumb);

/* 按需加载原图：优先从本地读，盘上没有才联网。"""

assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, decl, 1)

# 后面定义处的 #define BLOB_HDR 12 会重复定义 → 去掉（同值重复定义其实是合法的，
# 但避免 -Wmacro-redefined 噪音，直接删掉后面那个）
old2 = """#define BLOB_HDR 12

static uint8_t* blobPack(const char* magic, int w, int h,
                         const uint8_t* payload, size_t len, size_t* outLen) {"""
new2 = """static uint8_t* blobPack(const char* magic, int w, int h,
                         const uint8_t* payload, size_t len, size_t* outLen) {"""
assert s.count(old2) == 1, s.count(old2)
s = s.replace(old2, new2, 1)

io.open(B, 'w', encoding='utf-8', newline='').write(s)
print('ok')
