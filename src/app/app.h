#ifndef APP_APP_H
#define APP_APP_H

class App {
 public:
  // 初始化应用（显示+触摸+LVGL+UI）
  static bool begin();

  // 主循环，需在 Arduino loop() 中调用
  static void loop();

 private:
  // LVGL 显示刷新回调
  static void dispFlush(lv_disp_drv_t* disp, const lv_area_t* area,
                        lv_color_t* colorP);

  // LVGL 触摸读取回调
  static void touchRead(lv_indev_drv_t* indev, lv_indev_data_t* data);
};

#endif