#pragma once
#include <lvgl.h>

// 白色线条图标（矢量，运行时用 lv_canvas 绘制，透明底、可任意缩放）
enum class Icon {
  Clock,
  Settings,
  Back,
  Wifi,
  Weather,
  Switch,
  Terminal,
  Music,
  Power,
  Game,
  Browser,
  Lock,
  Battery,    // 电池（外壳 + 电量条）
  Bluetooth,  // 蓝牙
  Forward,    // 右箭头（与 Back 配套）
  Home,       // 房子（首页）
  Refresh,    // 环形箭头（重新加载）
  ExitDoor,   // 箭头 + 门（退出 / 登出）
  More,       // 三个点（更多）
  Download,   // 向下箭头 + 底线（把当前页存下来）
  Tasks,      // 四个小方块（后台运行的应用）
};

// 在 parent 内创建一个 size×size 的白色线条图标，返回 canvas 对象。
// 调用方负责布局（align / set_pos）。buffer 分配在 PSRAM，生命周期跟随对象。
lv_obj_t* icon_create(lv_obj_t* parent, Icon type, uint16_t size);

// WiFi 专用：level 0..3 表示点亮几格信号。未点亮的格子用暗色（0x555555）画出，
// 保证格子形状始终可见 —— 不要改成透明。
lv_obj_t* icon_create_wifi(lv_obj_t* parent, uint16_t size, int level);

// 就地重画已存在的 WiFi 画布，不重新分配缓冲（信号强度变化频繁，避免 PSRAM 碎片）
void icon_wifi_set_level(lv_obj_t* canvas, uint16_t size, int level);

// 就地换图标类型（同一个画布反复用，省一次 PSRAM 分配）。size 必须与创建时一致。
void icon_set_type(lv_obj_t* canvas, Icon type, uint16_t size);
