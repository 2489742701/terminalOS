# -*- coding: utf-8 -*-
import io, os

D = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\.workbuddy\memory"
os.makedirs(D, exist_ok=True)

note = """

## 天气页：展示 JSON 全量内容（2026-09-25 续）

master 要求「展示所有 JSON 能拉到的内容，能下滑看近 7 日」。落地结果：
一次 open-meteo 请求 ~3.0KB，current 16 项 + daily 7 日 + hourly 24 条，
页面分两段：上半屏固定（城市/温度/描述/体感），下半屏可下滑容器依次放
「当前实况」16 格 →「未来 24 小时」8 列×3 行 →「近 7 日预报」（温度区间 +
降水概率/降水量/风/紫外/日出日落）。竖滑退出关掉（allowVertical=false）。

踩到三个坑（已写进 docs/09 的 I18~I21）：

1. **URL 被 snprintf 静默截断 → 400**。完整 URL 实测 594 字节，原来 buf[512]，
   加 hourly 之后设备端就开始 `HTTP 400`，而**同一个 URL 在 PC 上 200** ——
   极易误判成"对方接口不接受参数"。改成 buf[768]。
   判定口诀：设备 400 / PC 200，先怀疑 URL 截断。

2. **create / tick / delete_cb 不能待在匿名 namespace 里**。它们要被 nav.cpp、
   serial_console.cpp 链接（外部链接），可又要读写 namespace 里的 g_* 指针 →
   `'g_dayDate' was not declared in this scope`。反向挪也不行（变内部链接 →
   链接期 undefined reference）。最终：struct/constexpr/全部 g_* 提到 namespace
   外面加 static，namespace 里只留纯内部函数（也 static）。脚本
   `tools/_patch_weather_ns.py`。跟 I15 的 `g_uiAnim` 是同一条规矩的两种翻车。

3. **LVGL 8.3 没有 `lv_obj_set_scroll_dir()`**（8.4 才有）。而且就算有，在
   竖向滚动父容器里嵌横向滚动子容器，竖滑会先被子容器吃掉 → 7 日列表滑不动。
   逐时改成 `LV_FLEX_FLOW_ROW_WRAP` 排 8 列×3 行，跟着页面竖滑。

另外两个交叉结论：
- **「编译成功」可能是陈旧 .o 的假阳性**：这次就是先看到 [SUCCESS]，下一轮才
  暴露出上一次根本没编。以后改完必须核 `.o` vs 源文件 mtime + grep error 计数。
- 后台任务连发两次 notify 会连跑两轮 HTTPS → 加 `g_busy / g_again` 合并，
  正在跑时只置"补一轮"，别再 notify。

docs/09 顺手去掉了 I11~I17 被重复写入的那一份。
"""

io.open(os.path.join(D, "2026-09-25.md"), "a", encoding="utf-8").write(note)
print("appended", len(note))
