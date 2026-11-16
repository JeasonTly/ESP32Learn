#ifndef OLED_UI_H
#define OLED_UI_H

#include <stdint.h>

// 初始化 0.96 寸 SSD1306，先画一帧睁眼的头。此时 WiFi 和蓝牙都还没连上
void oledBegin();

// 推进表情。眨眼按人安静时的频率，大约每分钟 15 到 20 次；wink、四处看、思考、开心、难过隔几秒随机出现。到了切换点返回 true
bool oledPoll();

// 画整屏：中间是头，右上角是信号格，左侧是低功耗蓝牙状态
// signalBars：-1 未连接（空框加叉），0 到 5 是已连接时涂实的格数
// bleOn：true 表示已经有手机连上低功耗蓝牙
void oledShow(int signalBars, bool bleOn);

#endif
