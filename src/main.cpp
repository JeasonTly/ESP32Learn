#include <Arduino.h>
#include "oled_ui.h"
#include "wifi_link.h"
#include "bt_link.h"

// 上一帧屏幕上的蓝牙状态。和这一帧不一样才重画，避免每圈 loop 都刷屏
bool lastBleOn = false;

// 上电后只执行一次
void setup() {
  // Serial.begin(波特率)：打开 USB 串口。常用 9600 或 115200，监视器要设成同一个数
  Serial.begin(115200);
  // 先画头像。这时 WiFi 还没连，蓝牙也还没开
  oledBegin();
  // 串口打出一行，确认屏幕初始化已经走完
  Serial.println("Setup complete");
  // 阻塞等待连上路由器，并记下第一笔信号格数
  wifiLinkBegin();
  // 打开低功耗蓝牙从机，等安卓写入引脚指令
  btLinkBegin();
  // 用刚读到的格数再画一屏，右上角从叉变成信号条，左侧仍是未连接的空心点
  oledShow(wifiLinkBars(), btLinkConnected());
}

// setup 之后反复执行
void loop() {
  // 这一轮要不要整屏重画。眨眼、信号格或蓝牙连接变化时才置真
  bool needDraw = false;
  // 到了闭眼、睁眼或 wink 的切换点
  if (oledPoll()) {
    needDraw = true;
  }
  // 到点就重读信号。返回 true 表示格数或连接状态变了
  if (wifiLinkPoll()) {
    needDraw = true;
  }
  // 读这一帧的蓝牙连接。true 是已有手机连上
  bool bleOn = btLinkConnected();
  // 跟上一帧不同才重画。连上变实心点，断开变空心点
  if (bleOn != lastBleOn) {
    lastBleOn = bleOn;
    needDraw = true;
  }
  // 只有画面真的变了才送一帧，避免信号数字小幅跳动时闪屏
  if (needDraw) {
    oledShow(wifiLinkBars(), bleOn);
  }
}
