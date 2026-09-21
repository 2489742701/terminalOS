#!/usr/bin/env python3
"""Generate zh_symbols.txt with proper UTF-8 encoding for lv_font_conv."""
import sys

# All Chinese characters used in the project + common UI characters
symbols = (
    "的一是不了在人有我他这中大来上个国和地也子时道说们要就你会对为以到生而于出"
    "想其实现后然便所间用头面话又但看被没行期次位者老因给事成可些此样动同何力"
    "理做当从把体前进门件日新最经能开方已还三主产好长本面多前些如自里外下只都"
    "很就这那她它们国中年月日点分秒星期周时钟设置返回退出确认取消开关是否错误"
    "警告成功连接断开扫描密码网络无线未接入校准时间固件版本编译软屏锁解滑上下"
    "左右源双击主面可待游戏贪吃蛇分数最高重新开始暂停继续浏览器地址输入加载刷"
    "新后退前进页面搜索首页终端极客系统信息存储亮暗度背光级阶触摸显示驱配置引"
    "脚率宽高色深黑白底线图标格点阵渲染缓冲突存读写发送接收请求响应状态待机活"
    "休眠醒关闭启动重置更新检查测试调试日志打印串口波特连发收闭打始束终完成失"
    "败功正负多寡窄粗细圆方尖钝锐直弯平斜反内外东西南北往返导航选删增改查排序"
    "筛选过滤查找定位标记标签分类类型名值键对串数布尔空真假季春夏秋冬温湿干冷"
    "暖凉炎热强弱快慢远近重轻好坏对错进退直曲横竖红绿蓝黄青紫灰透明暗亮深浅边"
    "框线画布景图文标题示提警载关开清确取消返回入出上下左右前后中方向位置区域"
    "大小颜色底边框线面点阵渲染缓存冲突读写收发请求响应待机休眠唤醒关闭启动重"
    "置更新检查测试调试日志打印串口波特率连接发送接收断开打开开始结束完成失败"
    "成功正确错误警告提示信息确认取消进入后退前进刷新加载输入输出地址搜索浏览"
    "页面首页设置时间日期时钟星期月年日时分秒点零一二三四五六七八九十百千万亿"
)

# Deduplicate while preserving order
seen = set()
unique = []
for c in symbols:
    if c not in seen:
        seen.add(c)
        unique.append(c)

result = ''.join(unique)
print(f"Unique chars: {len(result)}")
print(f"First 30: {result[:30]}")

# Write with UTF-8 encoding (no BOM)
outpath = sys.argv[1] if len(sys.argv) > 1 else "zh_symbols_v2.txt"
with open(outpath, 'w', encoding='utf-8') as f:
    f.write(result)
print(f"Written to {outpath}")