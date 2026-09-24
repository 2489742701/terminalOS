# -*- coding: utf-8 -*-
"""纠正 I19：上一版把根因误判成"匿名 namespace 不可见"，实际是声明顺序。

实锤：nav.cpp 里 nav_open() 就在 `}  // namespace` 之后，照样调用 namespace 内的
find() 且一直编译通过 —— 匿名 namespace 等价于 `namespace unique {} + using
namespace unique;`，**关闭之后仍然可以通过非限定名访问**。

weather 那次的真正链路是：
  constexpr int DAY_N 被我挪到了 struct DayFc 前面（第 66 行），
  可 g_dayDate[DAY_N] 在第 55 行就用到了它 → DAY_N 未声明 → 数组声明失败 →
  连锁报成一串 'g_xxx' was not declared in this scope。
"""
import io

P = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\docs\09-坑点速查表.md"
src = io.open(P, encoding="utf-8").read()

start = src.index("### I19 ·")
end = src.index("### I20 ·")

new = """### I19 · 一串 `g_xxx was not declared` 的**真正**根因常是"声明顺序"，不是 namespace
- **症状**：改完 weather_screen.cpp 后冒出
  `'g_hourLab' / 'g_dayDate' / 'g_dayTmp' … was not declared in this scope` 一长串，
  很容易误判成"匿名 namespace 里的符号外面看不见"。
- **❌ 误判**：我当时就是这么写的，还据此把一批变量搬出了 namespace —— 白改。
  反证很硬：nav.cpp 的 `nav_open()` 就在 `}  // namespace` **之后**，照样调用
  namespace 内的 `find()`，一直编译得好好的。
  匿名 namespace 等价于 `namespace unique {…} + using namespace unique;`，
  **关闭之后仍然可以用非限定名访问**。
- **✅ 真因**：`constexpr int DAY_N` 被挪到了 `struct DayFc` 前面（第 66 行），
  可 `lv_obj_t* g_dayDate[DAY_N]` 在第 55 行就用到它 → `DAY_N` 未声明 →
  这一行数组声明**整条失效** → 后面每个用到它的地方都报"未声明"。
- **教训**：看到"一串变量未声明"，**先去看这些声明所在行的依赖**（数组长度常量、
  类型名）是不是在后面才定义；别急着怪 namespace / 链接性。
- 顺带留下的规矩仍然成立：要被别的 .cpp 链接的符号（create/tick/fetchNow）
  必须有外部链接（I15 的 `g_uiAnim` 是同一条规矩的另一种翻车）。

"""

src = src[:start] + new + src[end:]
io.open(P, "w", encoding="utf-8", newline="").write(src)
print("I19 rewritten")
