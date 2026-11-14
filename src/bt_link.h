#ifndef BT_LINK_H
#define BT_LINK_H

// 打开低功耗蓝牙从机，手机扫描到的名字是 ESP32-OLED
// 安卓向写入特征发送 "引脚,电平"。23,1 把 GPIO23 拉高，23,0 拉低
void btLinkBegin();

// 有没有手机连着。true 是已连接，false 是还在广播、等手机来连
bool btLinkConnected();

#endif
