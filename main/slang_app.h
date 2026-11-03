#pragma once

#include "esp_err.h"

/* 离线梗词典应用。启动后直接进词表，不经过硬件测试菜单。
 * 调用方先完成显示初始化；本函数再接按键、电量和页面。 */
esp_err_t slang_app_start(void);

/* 按键回调只入队。深睡唤醒守卫由 main 先处理，再把剩余事件交来。 */
void slang_button_event(int button, int event);
