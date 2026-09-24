# -*- coding: utf-8 -*-
"""selfTest 补 case 3：走 readFile()（String 累加）那条路径"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\src\hal\sd_card.cpp"
s = io.open(P, encoding="utf-8", newline="").read()

old = "  SD.remove(path);\n  heap_caps_free(pat);"
new = r'''  /* ── 3) 走 writeFile/readFile（页面缓存真正用的那条路）──
        模式里**不掺 0x00**，先排除 NUL 截断这个已知嫌疑。 */
  {
    String sdata;
    sdata.reserve(n + 16);
    for (size_t i = 0; i < n; i++) sdata += (char)(uint8_t)(i * 31u + (i >> 8));
    bool wok = SDCard::writeFile(path, sdata);
    String back;
    bool rok = SDCard::readFile(path, back);
    size_t bad = 0, firstBad = 0;
    size_t m = back.length() < n ? back.length() : n;
    for (size_t i = 0; i < m; i++)
      if ((uint8_t)back[i] != (uint8_t)sdata[i]) { if (!bad) firstBad = i; bad++; }
    Serial.printf("[SDTest] writeFile/readFile: w=%d r=%d len=%u/%u bad=%u firstBad=%u %s\n",
                  (int)wok, (int)rok, (unsigned)back.length(), (unsigned)n,
                  (unsigned)bad, (unsigned)firstBad, (bad || !wok || !rok) ? "MISMATCH" : "MATCH");
    if (bad || !wok || !rok) okAll = false;
  }

  /* ── 4) 同上，但模式里掺入 0x00（验证 String 累加遇到 NUL 会怎样）── */
  {
    String sdata;
    sdata.reserve(n + 16);
    for (size_t i = 0; i < n; i++) {
      uint8_t v = (uint8_t)(i * 31u + (i >> 8));
      if ((i % 997) == 0) v = 0;      /* 每 997 字节塞一个 NUL */
      sdata += (char)v;
    }
    bool wok = SDCard::writeFile(path, sdata);
    String back;
    bool rok = SDCard::readFile(path, back);
    Serial.printf("[SDTest] with-NUL: len=%u want=%u %s\n",
                  (unsigned)back.length(), (unsigned)n,
                  (back.length() == n) ? "LEN-OK" : "LEN-SHORT");
    if (back.length() != n) okAll = false;
  }

  SD.remove(path);
  heap_caps_free(pat);'''

if old not in s:
    raise SystemExit("MISS")
s = s.replace(old, new, 1)
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("OK case3/4")
