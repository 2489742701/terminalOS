# 浏览器异步架构与 Lexbor CSS 崩溃修复

## 背景

在 ESP32-S3（16MB Flash / 8MB OPI PSRAM / 480×480 RGB IPS）上实现 TactileBrowser 引擎，
加载百度首页（~729KB HTML + 26KB CSS + 407B CSS）。核心挑战：
1. LVGL 不是线程安全的，所有 UI 操作必须在同一任务中
2. Lexbor CSS 解析器在解析大 CSS 后内部状态破坏，后续调用崩溃
3. ESP32-S3 双核任务调度 + PSRAM 带宽争抢

## 一、两阶段异步架构

### 问题
后台任务下载+解析+渲染全做 → 后台任务调 LVGL → Core 1 `lv_scr_load_anim` 崩溃

### 解决方案
将浏览器加载拆分为两个阶段：

| 阶段 | 执行核 | 任务 | 涉及 LVGL |
|------|--------|------|-----------|
| Phase 1 | Core 0 (后台任务) | 下载 HTML + 解析 DOM + 构建布局树 | 否 |
| Phase 2 | Core 1 (UI 任务) | 创建 LVGL 控件 + 渲染 | 是 |

### 关键实现

**状态机**：`BROWSER_IDLE → BROWSER_LOADING → BROWSER_LOADED / BROWSER_STOPPED / BROWSER_ERROR`

**后台任务**（`fetch_task`, Core 0, 64KB 栈）：
```cpp
g_taskResult = tactilebrowser_download_and_parse(
    url, 460, 360, &g_stopRequested, &g_layoutRoot);
g_taskDone = true;  // 通知 UI 任务
```

**UI 任务**（`BrowserScreen_tick`）检查 `g_taskDone` → 调用 Phase 2 渲染。

**引擎重建在 UI 任务中做**（不在后台任务中）：
```cpp
void startFetch(const String& url) {
    ensureEngineInit();
    // 引擎重建在 UI 任务中，避免后台任务操作全局状态
    tactilebrowser_core_cleanup();
    tactilebrowser_core_init();
    tactilebrowser_set_renderer(&g_renderer->base);
    tactilebrowser_set_html_downloader(arduino_download_html);
    // 创建后台任务
    xTaskCreatePinnedToCore(fetch_task, "fetch", 65536, NULL, 1, &g_fetchTask, 0);
}
```

### 协作式停止
`volatile bool g_stopRequested`，下载循环和 DOM 遍历每块检查。
用户点"停止"按钮 → `g_stopRequested = true` → 后台任务退出。

### 加载遮罩
半透明黑色 + "加载中，请稍等..." + 进度条 + 大红色停止按钮。

## 二、Lexbor CSS 解析器崩溃修复

### 现象
解析百度 26KB CSS 后，后续调用 `lxb_css_declaration_list_parse` 解析颜色值时崩溃：
```
EXCVADDR=0x08 (NULL+8)
backtrace: lxb_css_syntax_parse_declarations → lxb_css_declaration_list_parse
          → css_parser_parse_color_value (css_parser.cpp:521)
          → layout_apply_css_property → build_layout_tree_from_dom
```

### 根因
Lexbor CSS 解析器有**内部全局状态**。解析大 CSS（26KB）后内部状态破坏，
后续任何 `lxb_css_declaration_list_parse` 调用都会访问已破坏的内存。

### 解决方案：完全移除 Lexbor CSS 回退

`css_parser_parse_color_value` 只用手动解析，不依赖 Lexbor：

```cpp
bool css_parser_parse_color_value(const char *value, uint32_t *color_out) {
    if (!value || !color_out) return false;
    // 只用手动解析，完全不依赖 Lexbor CSS 解析器
    return parse_color_manual(value, color_out);
}
```

`parse_color_manual` 支持的格式：
- `#RGB` / `#RRGGBB`
- `rgb(r, g, b)` / `rgba(r, g, b, a)`（忽略 alpha）
- 常见颜色名（black/white/red/green/blue/...共 20 种）

解析失败 → 返回 false（颜色不应用，但不崩溃）。

### 为什么不修复 Lexbor 本身？
1. Lexbor 是第三方库，内部状态管理复杂
2. 裁剪版已去掉 selectors/style/unicode 等模块，可能引入了不一致
3. 手动解析覆盖 95%+ 的实际颜色值，足够用
4. 即使修复了颜色解析，其他 Lexbor CSS 调用也可能崩溃

## 三、ESP32-S3 双核协作要点

### 核心分配
- **Core 0**：WiFi/BT 任务、后台下载/解析任务
- **Core 1**：Arduino `loop()`、LVGL `lv_timer_handler()`、UI 渲染

### 让双核合作的关键
- `vTaskDelay(1)` 在下载循环、DOM 遍历每 50 节点、渲染每 30 节点调用
- 让 Core 0 WiFi 任务喘气，避免看门狗超时

### 内存注意事项
- `ESP.getFreeHeap()` 在 ESP32-S3 上会锁多堆，阻塞 CPU1 WiFi 任务 → 用 `xPortGetFreeHeapSize()`
- Lexbor 内存重定向到 PSRAM（`lexbor_memory_setup()`）
- Lexbor 完整版 DRAM 溢出 → 裁剪 selectors/style/unicode/punycode/engine/url 六个模块

## 四、CSS 限制

| 类型 | 限制 | 说明 |
|------|------|------|
| 外部 CSS | 64KB | 百度 26KB CSS 可完整解析 |
| 内联 CSS | 32KB | style 属性 |
| HTML 缓冲 | 768KB | 百度 729KB HTML 可完整读取 |

## 五、验证结果

百度首页加载成功：
- HTML: 729KB (status=200)
- CSS: 26KB + 407B (两个外部 CSS)
- 106 个 LVGL widget 创建
- 无崩溃、无看门狗超时
- 加载完成后可滚动浏览

## 六、经验总结

1. **LVGL 不是线程安全的**：所有 LVGL 操作必须在同一个任务中。后台任务只做数据准备，UI 任务做渲染。
2. **第三方库的内部状态**：Lexbor CSS 解析器有内部全局状态，大输入可能破坏状态。如果只需要简单解析（如颜色值），手动解析比依赖库更安全。
3. **双核协作**：`vTaskDelay(1)` 是让双核合作的关键。长时间运行的任务必须定期让出 CPU。
4. **引擎重建时机**：有状态的全局引擎重建必须在 UI 任务中做，不能在后台任务中做（会破坏正在使用的全局状态）。
5. **PSRAM 内存重定向**：大缓冲区（HTML 768KB、Lexbor 内存）重定向到 PSRAM，避免 DRAM 溢出。