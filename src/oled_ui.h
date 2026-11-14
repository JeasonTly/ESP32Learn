#ifndef OLED_UI_H
#define OLED_UI_H

#include <stdint.h>

// 初始化 0.96 寸 SSD1306，先画一帧睁眼的头。此时 WiFi 和蓝牙都还没连上
void oledBegin();

// 推进眨眼。到了闭眼、睁眼或 wink 的切换点就返回 true，调用方要再画一屏
bool oledPoll();

// 画整屏：中间是头，右上角是信号格，左侧是低功耗蓝牙状态
// signalBars：-1 未连接（空框加叉），0 到 5 是已连接时涂实的格数
// bleOn：true 表示已经有手机连上低功耗蓝牙
void oledShow(int signalBars, bool bleOn);

#endif
