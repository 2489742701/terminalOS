# -*- coding: utf-8 -*-
"""广告标记改成全等匹配（子串会误杀搜索词本身）。

实测（2026-09-24，搜"广告投放"）：
  子串规则把 '广告投放' / '投放广告' 各丢了 6 次 —— 那是**搜索词和相关搜索**，
  是正经内容。广告标记是徽章，文本就是「广告」二字，全等匹配才对。
"""
import io
P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\browser_engine\src\layout_engine.cpp"
t = io.open(P, encoding="utf-8", newline=None).read()

OLD = """static bool flat_is_ad_text(const char *s) {
  size_t n = strlen(s);
  if (n == 0 || n > FLAT_AD_TEXT_MAX_BYTES) return false;
  for (int i = 0; kFlatAdPhrases[i]; i++)
    if (strstr(s, kFlatAdPhrases[i])) return true;
  char low[64];
  flat_lower_copy(low, s, n);
  for (int i = 0; kFlatAdPhrasesAscii[i]; i++)
    if (strstr(low, kFlatAdPhrasesAscii[i])) return true;
  return false;
}"""
NEW = """static bool flat_is_ad_text(const char *s) {
  size_t n = strlen(s);
  if (n == 0 || n > FLAT_AD_TEXT_MAX_BYTES) return false;
  /* ⚠️ 必须**全等**（trim 后 strcmp），不能用 strstr：
     2026-09-24 实测搜「广告投放」，子串规则把 '广告投放' / '投放广告'
     各丢了 6 次 —— 那是搜索词和相关搜索，是正经内容。
     广告标记是徽章，文本就是「广告」二字，全等才不会误伤。 */
  while (*s == ' ' || *s == '\\t' || *s == '\\n' || *s == '\\r') s++;
  for (int i = 0; kFlatAdPhrases[i]; i++)
    if (strcmp(s, kFlatAdPhrases[i]) == 0) return true;
  char low[64];
  flat_lower_copy(low, s, strlen(s));
  for (int i = 0; kFlatAdPhrasesAscii[i]; i++)
    if (strcmp(low, kFlatAdPhrasesAscii[i]) == 0) return true;
  return false;
}"""
assert t.count(OLD) == 1, "anchor not found"
t = t.replace(OLD, NEW)

# 广告词表：补几个徽章写法；删掉"商业推广/推广链接"这种会被"推广"重复命中的长条
OLD2 = """static const char *kFlatAdPhrases[] = {"广告", "推广", "赞助", "商业推广",
                                       "推广链接", "广告信息", NULL};"""
NEW2 = """static const char *kFlatAdPhrases[] = {"广告", "推广", "赞助", "广告信息",
                                       "商业推广", "推广链接", NULL};"""
assert t.count(OLD2) == 1, "anchor2 not found"
t = t.replace(OLD2, NEW2)

io.open(P, "w", encoding="utf-8", newline="").write(t)
print("patched: ad -> exact match")
