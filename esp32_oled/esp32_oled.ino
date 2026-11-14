// 这个文件留给 Arduino IDE 对照。PlatformIO 实际烧录的是上一级目录的 src/main.cpp。
// Arduino IDE 会自动包含 Arduino.h，所以这里不再写那一行。

// Wire 是 ESP32 的 I2C 主机库，SSD1306 通过它跟屏幕说话
#include <Wire.h>
// SSD1306Wire：0.96 寸 SSD1306 屏的驱动，负责建 I2C 连接和把画面送到屏上
#include "SSD1306Wire.h"
// OLEDDisplayUi：翻页界面。一页叫一帧，换页可以滑动
#include "OLEDDisplayUi.h"

// SSD1306Wire 构造函数，括号里从左到右是：
//   地址 _address：屏的 I2C 地址。常见 0x3C，少数模块是 0x3D
//   SDA _sda：数据线引脚。ESP32 默认 21；写 -1 表示不改引脚、不自己调用 Wire.begin
//   SCL _scl：时钟线引脚。ESP32 默认 22；SDA 为 -1 时这个值不生效
//   分辨率 g：GEOMETRY_128_64（0.96 寸默认）、GEOMETRY_128_32、GEOMETRY_64_48、GEOMETRY_64_32、GEOMETRY_RAWMODE
//   哪条 I2C _i2cBus：I2C_ONE 用 Wire（默认），I2C_TWO 用 Wire1
//   时钟 _frequency：I2C 速度，单位 Hz，默认 700000；写 -1 表示不改速度
SSD1306Wire display(0x3c, 21, 22);

// 把上面的屏幕交给翻页库。只收一个参数：OLEDDisplay 指针
OLEDDisplayUi ui(&display);

// 画第 1 页。库在每次刷新时调用它。
// 参数：display 是屏幕；state 是当前页号、是否正在滑动等状态；
// x、y 是滑动偏移，单位像素。静止时都是 0，向左滑时 x 为负
void drawFrame1(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  // setTextAlignment：文字相对给定坐标的对齐方式
  // 可选 TEXT_ALIGN_LEFT 左对齐、TEXT_ALIGN_CENTER 水平居中、
  // TEXT_ALIGN_RIGHT 右对齐、TEXT_ALIGN_CENTER_BOTH 水平垂直都居中
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  // setFont：当前字体。库自带 ArialMT_Plain_10、ArialMT_Plain_16、ArialMT_Plain_24，数字是字高像素
  display->setFont(ArialMT_Plain_24);
  // drawString(x, y, 文字)：在坐标处画字符串，返回写出的字符数
  // x 向右增大，y 向下增大，原点在屏幕左上角。128x64 屏的水平中心是 64
  // 必须加上本帧的 x、y，滑动动画才会带着这行字一起走
  display->drawString(64 + x, 16 + y, "Page 1");
}

// 画第 2 页。参数含义与 drawFrame1 相同
void drawFrame2(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  // 本页文字也按坐标水平居中
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  // 字高 24 像素
  display->setFont(ArialMT_Plain_24);
  // 22 是距屏幕顶部的像素。加上 y 后，翻页时这行字跟着滑动
  display->drawString(64 + x, 22 + y, "Page 2");
}

void drawFrame3(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  //display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_24);
  display->drawString(64 + x, 22 + y, "中文页面");
}

// 页绘制函数的名单。必须写在全局：setFrames 只记下这个数组的地址，
// 如果写在 setup() 里面，setup 结束后数组就消失，翻页会读到无效地址
// 成员类型 FrameCallback，顺序就是页码，从 0 开始
FrameCallback frames[] = { drawFrame1, drawFrame2,drawFrame3};
// 上面数组有几页。setFrames 的第二个参数要用这个数
int frameCount = 3;

// 上电后只执行一次
void setup() {
  // Serial.begin(波特率)：打开 USB 串口。115200 是常用速度，监视器要设成同一个数
  Serial.begin(115200);

  // setTargetFPS(帧每秒)：界面刷新目标。常用 30 或 60。越高越流畅，也越占 CPU
  ui.setTargetFPS(30);
  // setFrames(函数数组, 页数)：登记要轮流显示的页。页数是 uint8_t，最大 255
  ui.setFrames(frames, frameCount);
  // enableAutoTransition()：无参数。到时间后自动滑到下一页
  // 对应的关闭函数是 disableAutoTransition()
  ui.enableAutoTransition();
  // setTimePerFrame(毫秒)：每一页停多久再开始滑走。1000 就是 1 秒
  ui.setTimePerFrame(1000);
  // setTimePerTransition(毫秒)：两页之间的滑动要花多久。400 就是 0.4 秒
  ui.setTimePerTransition(400);
  // setFrameAnimation(方向)：新页从哪边滑入
  // 可选 SLIDE_LEFT 向左、SLIDE_RIGHT 向右、SLIDE_UP 向上、SLIDE_DOWN 向下
  ui.setFrameAnimation(SLIDE_LEFT);
  // setIndicatorPosition(位置)：底部小圆点画在哪条边
  // 可选 TOP 上、BOTTOM 下、LEFT 左、RIGHT 右
  ui.setIndicatorPosition(BOTTOM);

  // init()：无参数。初始化屏幕控制器，内部会调用 display.init()
  ui.init();
  // println：从串口打出一行，方便确认 setup 已经走完
  Serial.println("Setup complete");
}

// setup 之后反复执行
void loop() {
  // update()：无参数。到了刷新时间就画当前页并推进动画
  // 返回值是距离下一次刷新还剩多少毫秒；来不及画时会小于等于 0
  int remaining = ui.update();
  // 还有空闲时间才等待，把 CPU 让出来。remaining 的单位是毫秒
  if (remaining > 0) {
    // delay(毫秒)：暂停这么久。这里用来对齐 setTargetFPS 设定的刷新间隔
    delay(remaining);
  }
}
