# -*- coding: utf-8 -*-
"""给 layout_engine.cpp 的垃圾过滤加广告/页脚规则。"""
import io, sys, re

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\layout_engine.cpp"
src = io.open(P, encoding="utf-8").read()

# ── 1) 新增两张词表（插在 kFlatJunkPhrasesAscii 之后）────────────────────
ANCHOR = 'static const char *kFlatJunkPhrasesAscii[] = {"loading", "please wait",\n                                              "just a moment", NULL};\n'
assert src.count(ANCHOR) == 1, "anchor1 not found"

NEW_TABLES = ANCHOR + '''
/* 广告 / 推广标记（2026-09-24）。
   ⚠️ 必须带**长度闸门**：搜「广告投放」时结果摘要里满是"广告"二字，那是正经内容。
   真正当标记用的都是独立短节点（「广告」6 字节、「推广」6 字节），
   所以超过 16 字节的一律放过。 */
#define FLAT_AD_TEXT_MAX_BYTES 16
static const char *kFlatAdPhrases[] = {"广告", "推广", "赞助", "商业推广",
                                       "推广链接", "广告信息", NULL};

/* 页脚法定文本：备案号 / 隐私 / 条款 / 版权。
   比广告标记长 ——「京ICP备05002793号-1」≈20 字节，「隐私政策 | 服务条款」≈20 字节，
   闸门放到 40。再长的正文里出现"条款"是正常的，不杀。 */
#define FLAT_FOOTER_TEXT_MAX_BYTES 40
static const char *kFlatFooterPhrases[] = {
    "备案",     "公网安备",   "隐私",     "条款",     "版权所有",
    "著作权",   "法律声明",   "免责声明", "侵权投诉", "用户协议",
    "意见反馈", NULL};

/* ASCII 广告标记（长度闸门同中文那档，略放宽到 24 以容纳 "all rights reserved" 的前半） */
static const char *kFlatAdPhrasesAscii[] = {"sponsored", "advertisement",
                                            "ad choices", "ads by", NULL};
static const char *kFlatFooterPhrasesAscii[] = {
    "all rights reserved", "privacy policy", "terms of service",
    "cookie policy",       "privacy",        "terms of",
    "copyright",           "icp",            NULL};

/* 短文本转小写到栈上（<256 B 才转，够用且不上堆） */
static void flat_lower_copy(char *out, const char *s, size_t n) {
  for (size_t i = 0; i <= n; i++) {
    char c = s[i];
    out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
  }
}

static bool flat_is_ad_text(const char *s) {
  size_t n = strlen(s);
  if (n == 0 || n > FLAT_AD_TEXT_MAX_BYTES) return false;
  for (int i = 0; kFlatAdPhrases[i]; i++)
    if (strstr(s, kFlatAdPhrases[i])) return true;
  char low[64];
  flat_lower_copy(low, s, n);
  for (int i = 0; kFlatAdPhrasesAscii[i]; i++)
    if (strstr(low, kFlatAdPhrasesAscii[i])) return true;
  return false;
}

static bool flat_is_footer_text(const char *s) {
  size_t n = strlen(s);
  if (n == 0 || n > FLAT_FOOTER_TEXT_MAX_BYTES) return false;
  for (int i = 0; kFlatFooterPhrases[i]; i++)
    if (strstr(s, kFlatFooterPhrases[i])) return true;
  char low[64];
  flat_lower_copy(low, s, n);
  for (int i = 0; kFlatFooterPhrasesAscii[i]; i++)
    if (strstr(low, kFlatFooterPhrasesAscii[i])) return true;
  return false;
}
'''
src = src.replace(ANCHOR, NEW_TABLES)

# ── 2) flat_is_junk_text 里接上两张新表 ───────────────────────────────────
OLD = """static bool flat_is_junk_text(const char *s) {
  if (!s || !s[0])
    return true;
  if (flat_is_separator_only(s))
    return true;
  for (int i = 0; kFlatJunkPhrases[i]; i++) {"""
NEW = """static bool flat_is_junk_text(const char *s) {
  if (!s || !s[0])
    return true;
  if (flat_is_separator_only(s))
    return true;
  if (flat_is_ad_text(s))     return true;
  if (flat_is_footer_text(s)) return true;
  for (int i = 0; kFlatJunkPhrases[i]; i++) {"""
assert src.count(OLD) == 1, "anchor2 not found"
src = src.replace(OLD, NEW)

# ── 3) drop 时打印命中的广告/页脚文本，便于串口核对（限前 24 条）──────────
OLD2 = """  if (node->text_content && flat_is_junk_text(node->text_content)) {
    free(node->text_content);
    node->text_content = NULL;
    n++;
  }"""
NEW2 = """  if (node->text_content && flat_is_junk_text(node->text_content)) {
    /* 广告/页脚命中值得看一眼（分隔符合并类命中太多，不打） */
    static int s_junkLog = 0;
    if (s_junkLog < 24 &&
        (flat_is_ad_text(node->text_content) ||
         flat_is_footer_text(node->text_content))) {
      Serial.printf("[Junk] drop '%s'\\n", node->text_content);
      s_junkLog++;
    }
    free(node->text_content);
    node->text_content = NULL;
    n++;
  }"""
assert src.count(OLD2) == 1, "anchor3 not found"
src = src.replace(OLD2, NEW2)

io.open(P, "w", encoding="utf-8", newline="").write(src)
print("patched OK")
