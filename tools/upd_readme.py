# -*- coding: utf-8 -*-
"""更新 README 的文档索引 / 功能 / 待办（按行切片，避开 CRLF 匹配问题）"""
import io, os, sys

p = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\README.md'
raw = io.open(p, encoding='utf-8', errors='replace').read()
nl = '\r\n' if '\r\n' in raw else '\n'
lines = raw.replace('\r\n', '\n').split('\n')

# --- 1) 定位文档表格 ---
i_head = next(i for i, l in enumerate(lines) if l.startswith('| 文档 | 内容 |'))
i_end = next(i for i, l in enumerate(lines[i_head:], i_head)
             if l.startswith('|') and 'docs/11' in l)
print('doc table: %d..%d' % (i_head + 1, i_end + 1))

doc_rows = [
    '| [`docs/00-项目上手指南.md`](docs/00-项目上手指南.md) | **第一次接手先读这篇**：目录结构、模块职责、构建烧录、串口命令全集、常用脚本、文档地图 |',
    '| [`docs/01-显示花屏排查与修复.md`](docs/01-显示花屏排查与修复.md) | 刷新瞬间花屏的根因（PSRAM 带宽争抢 / LCD DMA 饿死）、有效修复、已排除嫌疑、社区方案可行性 |',
    '| [`docs/02-板级硬事实与构建烧录.md`](docs/02-板级硬事实与构建烧录.md) | **改构建配置前必读**：Flash 必须 DIO、PSRAM 必须 OPI、构建环境坑、标准构建/烧录/抓日志命令、已知无害告警 |',
    '| [`docs/03-参考项目分析.md`](docs/03-参考项目分析.md) | 参考项目（MicroPythonOS Activity/Intent、First-Start 同硬件 BSP）分析 |',
    '| [`docs/04-当前项目知识库.md`](docs/04-当前项目知识库.md) | 硬件规格、引脚、构建配置、已解决的坑、中文字体方案 |',
    '| [`docs/05-浏览器异步架构与Lexbor崩溃修复.md`](docs/05-浏览器异步架构与Lexbor崩溃修复.md) | 两阶段异步渲染（后台解析 / UI 渲染）、Lexbor CSS 崩溃的绕法 |',
    '| [`docs/06-浏览器内存管理与Activity生命周期.md`](docs/06-浏览器内存管理与Activity生命周期.md) | **已实机验证**：常驻 fetch 任务、Activity 懒创建/退出回收、LVGL 池 vs DRAM 两套账本、`MAX_WIDGETS` 80→150、退出崩溃修复 |',
    '| [`docs/07-启动循环类问题排查方法论.md`](docs/07-启动循环类问题排查方法论.md) | bootloop 排查套路：区分"崩溃"vs"死循环"、反汇编 bootloader 崩点、用极简工程二分定位 |',
    '| [`docs/08-浏览器UI布局与整页缩放.md`](docs/08-浏览器UI布局与整页缩放.md) | 竖向分区坐标、底部两组互斥工具栏（含切换按钮布局约束）、整页缩放的 viewport↔屏幕换算与实测数据表 |',
    '| [`docs/09-中文字体管理与生成.md`](docs/09-中文字体管理与生成.md) | **改字体前必读**：字体分工（汉字自生成 / 英文走内置 montserrat + fallback）、可复现的生成流程、**sparse cmap 相对偏移的解码陷阱**、真实 FPS 的测量方式 |',
    '| [`docs/10-浏览器渲染裁剪与搜索框丢失.md`](docs/10-浏览器渲染裁剪与搜索框丢失.md) | **百度首页搜不到搜索框的根因**：外层 `<span>` 的图标字体字符让整棵子树被丢弃；取证三步法；自动 `<meta viewport>`；babe32 / TactileBrowser / QuickJS 对比 |',
    '| [`docs/11-屏销毁与常驻定时器悬空指针.md`](docs/11-屏销毁与常驻定时器悬空指针.md) | **渲染成功后 1 秒 LoadProhibited 的真凶**：欢迎屏 3s 定时器 + `auto_del` 静默删屏、`lv_timer` 不属于对象树、状态栏野指针；addr2line 取证全过程 |',
    '| [`docs/12-浏览器排版路线与平铺方案.md`](docs/12-浏览器排版路线与平铺方案.md) | **改排版前必读**：路线 A/C/D 取舍、平铺模式为什么不建容器、行合并规则、胶囊 chip 排版 |',
    '| [`docs/13-搜索优先的小引擎方案.md`](docs/13-搜索优先的小引擎方案.md) | 自建搜索首页、UA 与 SERP 实测（必应桌面 UA 才有 10 条 + 下一页）、壳子过滤规则、引擎横评 |',
    '| [`docs/14-坑点速查表.md`](docs/14-坑点速查表.md) | **按症状反查的最快入口**：开不了机/花屏/崩溃/内存/构建/排版/字体/网络/生命周期/工具 十类 |',
]
lines[i_head:i_end + 1] = doc_rows

# --- 2) 功能 / 待办整段替换 ---
i_feat = next(i for i, l in enumerate(lines) if l.startswith('## 当前功能'))
print('feature section starts at line %d' % (i_feat + 1))

tail = [
    '## 当前功能',
    '',
    '- Launcher / 时钟 / 设置 / 天气 / WiFi / 内存 / 系统信息 / 游戏 / 画板 多屏导航，黑底 + 白色线条图标',
    '- 息屏状态机（`src/app/screensaver.cpp`）：ACTIVE（亮）→ DIM（PWM 15% 暗显时间）→ OFF（关背光）',
    '  - DIM 态上滑 ≥120px 解锁；OFF 态触摸回 DIM',
    '- **浏览器**（`src/app/browser_screen.cpp` + `src/browser_engine/`）：HTML → Lexbor 解析 → 平铺排版 → LVGL 渲染',
    '  - 平铺排版（不建容器，避免 CSS 坐标压盖）、链接可点、搜索首页（必应/百度切换 + 常用词胶囊）',
    '  - 页面缓存：URI→HTML 存 PSRAM，TTL 5 分钟，3 槽 LRU，命中则跳过下载',
    '  - 下载当前页到 LittleFS（`dl` 或底栏下载键）+ 内置 HTTP 服务（`serve`），PC 浏览器访问设备 IP 看原始 HTML',
    '- NTP 校时（`src/hal/ntp_time.cpp`，手写避开 `Time` 库）、背光 PWM（LEDC，PIN_BL=38）',
    '',
    '## 待办',
    '',
    '- [ ] **中文输入法**（拼音 IME）—— `lv_keyboard` 只有 ASCII，现在中文只能点预设胶囊',
    '- [ ] SERP 广告过滤（广告/推广/sponsored）',
    '- [ ] 页脚备案/隐私/条款等垃圾一并丢掉',
    '- [ ] emoji 仍是豆腐块',
    '- [ ] 触摸坐标偏移；上滑时左下角一条白线',
    '- [ ] 蓝牙接入（现为占位）',
    '- [ ] 新闻源：等 master 给源再填（首页已留占位框）',
    '',
    '> 完整清单与背景见 [`docs/00` §7](docs/00-项目上手指南.md)。',
    '',
    '---',
    '',
    '## 协作方式',
    '',
    '**给 AI / 协作者的工作契约见 [`AGENT.md`](AGENT.md)** —— 排查方法论、本项目红线、',
    '知识落盘约定。开工前读完 `AGENT.md` + `docs/00`。',
    '',
]
lines[i_feat:] = tail

out = nl.join(lines)
io.open(p, 'w', encoding='utf-8', newline='').write(out)
print('written, size=%d' % os.path.getsize(p))
