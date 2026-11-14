#include "oled_ui.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include "face_bitmap.h"

// 全缓冲构造函数，参数从左到右：
//   旋转：U8G2_R0 不转，U8G2_R1 顺时针 90 度，U8G2_R2 180 度，U8G2_R3 270 度
//   复位脚：U8X8_PIN_NONE 表示这块模块没有单独的复位引脚
//   SCL 时钟线：ESP32 默认 GPIO22
//   SDA 数据线：ESP32 默认 GPIO21
// 名字里的 128X64 是分辨率，F 表示先画在内存里再整屏送出，HW_I2C 表示用硬件 I2C
// 屏幕 I2C 地址默认 0x3C。若模块是 0x3D，在 begin() 之前调用 setI2CAddress(0x3D * 2)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ 22, /* data=*/ 21);

// 头像左上角。22 给左侧蓝牙标志留空，右边停在信号条前面
const int faceDrawX = 22;
// 头顶尽量靠上，60 像素高的头在 64 像素屏上还剩 2 像素底边
const int faceDrawY = 2;

// 眼睛状态。睁眼不用额外画，点阵里已经有
// 1 两眼闭上，2 只闭左眼，3 只闭右眼
const int eyeShut = 1;
const int eyeWinkLeft = 2;
const int eyeWinkRight = 3;

// 一组眨眼动作最多 3 步：闭上、睁开、再闭上
struct EyeStep {
  uint8_t mode;
  uint16_t holdMs;
};
// 当前这一轮的步骤。不在眨眼时用不到
EyeStep eyeSteps[3];
// 这一轮有几步
uint8_t eyeStepCount = 0;
// 正在画的是第几步
uint8_t eyeStepIndex = 0;
// 这一步开始的时刻，单位毫秒
unsigned long eyeStepMs = 0;
// true 表示正在做眨眼或 wink，false 表示睁眼等待
bool eyeBusy = false;
// 下一次开始眨眼的时刻，单位毫秒
unsigned long nextBlinkMs = 0;
// 上一轮是不是 wink。1 表示是。刚 wink 过就不要连着再 wink
int lastGestureWasWink = 0;

// 低功耗蓝牙标志，宽 7 高 12。每一字节的最低位是这一行最左边的像素
const uint8_t bleRune[] = {
  0x04, 0x0C, 0x14, 0x25, 0x26, 0x1C,
  0x14, 0x26, 0x25, 0x14, 0x0C, 0x04
};

// 在屏幕右上角画信号条。连上的格涂实，没到的格只画空框
// signalBars 为 -1 时 5 根都是空框，左边再加一个叉，用来和“连上了但 0 格”区分开
static void drawSignalBars(int signalBars) {
  // 一共几根。这里改成别的数字时，WiFi 侧最高返回值也要一起改
  const int barCount = 5;
  // 每根宽度，单位像素。小于 2 时空框中间就没有空心了
  const int barWidth = 4;
  // 两根之间空多少像素。改成 1 会挨得更紧
  const int barGap = 2;
  // 信号条脚下那一条线的 y。所有格子的底边都对齐在它上面一像素
  const int barBottom = 16;
  // 最矮一根的高度，单位像素
  const int barMinHeight = 4;
  // 往右每根加高多少像素。5 根就是 4、7、10、13、16
  const int barStep = 3;
  // 整组有多宽：根数乘宽度，再加上中间的空隙
  int totalWidth = barCount * barWidth + (barCount - 1) * barGap;
  // 靠右边放。128 是屏宽，减掉整组宽度就是最左边那根的 x
  int originX = 128 - totalWidth;
  // 先沿用调用方传来的格数。未连接是 -1
  int bars = signalBars;
  // 格数不会超过根数。门槛改乱时最多把 5 根都涂实
  if (bars > barCount) {
    bars = barCount;
  }
  // 从左往右画，左边最矮，右边最高
  for (int i = 0; i < barCount; i++) {
    // 这一根多高。i 从 0 开始
    int barHeight = barMinHeight + i * barStep;
    // 这一根的左边。每往右一根，加上一根的宽度和一条空隙
    int barX = originX + i * (barWidth + barGap);
    // 顶边。底边对齐，所以越高的根顶边越靠上
    int barY = barBottom - barHeight;
    // i 比已亮格数小才涂实。3 格时 i 为 0、1、2 的三根是实心
    if (i < bars) {
      // drawBox(左, 上, 宽, 高)：实心矩形，单位都是像素。没有颜色参数，画出来就是亮点
      u8g2.drawBox(barX, barY, barWidth, barHeight);
    } else {
      // drawFrame(左, 上, 宽, 高)：同样位置和大小的空心框。宽或高小于 2 时会糊成一条线
      u8g2.drawFrame(barX, barY, barWidth, barHeight);
    }
  }
  // 只有未连接才画叉。连上但一格都不到时，屏幕上只有 5 个空框
  if (bars < 0) {
    // 叉宽 7 像素，放在信号条左边，中间留 3 像素
    int crossX = originX - 3 - 6;
    // 叉高 7 像素，放在整组信号条的中部，不贴着最上边
    int crossY = 4;
    // drawLine(起点 x, 起点 y, 终点 x, 终点 y)：两点之间的直线，单位像素
    u8g2.drawLine(crossX, crossY, crossX + 6, crossY + 6);
    // 再画反方向的一条，交叉之后才是叉
    u8g2.drawLine(crossX + 6, crossY, crossX, crossY + 6);
  }
}

// 左侧画低功耗蓝牙标志。实心点表示手机已连接，空心点表示还在等
static void drawBleStatus(bool bleOn) {
  // 标志左上角。x=1 贴着左边，y=16 放在头像左侧的空档里
  const int markX = 1;
  const int markY = 16;
  // drawXBMP(左, 上, 宽, 高, 点阵)：宽 7、高 12，单位像素
  u8g2.drawXBMP(markX, markY, 7, 12, bleRune);
  // 状态点的圆心，在标志下面
  int dotX = markX + 3;
  int dotY = markY + 16;
  if (bleOn) {
    // drawDisc(圆心 x, 圆心 y, 半径)：实心圆。半径 2 约 5 像素宽
    u8g2.drawDisc(dotX, dotY, 2);
  } else {
    // drawCircle(圆心 x, 圆心 y, 半径)：空心圆。和实心点同一个位置，方便对照
    u8g2.drawCircle(dotX, dotY, 2);
  }
  // setFont(字库)：u8g2_font_4x6_tr 每个字母大约 4 像素宽、6 像素高，t 透明背景，r 只含英文数字
  u8g2.setFont(u8g2_font_4x6_tr);
  // setFontPosTop：无参数。之后 drawStr 的 y 是字的顶边
  u8g2.setFontPosTop();
  // drawStr(x, y, 文字)：画 ASCII。LE 标明这是低功耗蓝牙，不是经典蓝牙
  u8g2.drawStr(markX, dotY + 5, "LE");
}

// 一只眼里的一段黑眼洞。x、y 相对点阵左上角，len 是这一行连续的黑点个数
struct EyeSpan {
  uint8_t x;
  uint8_t y;
  uint8_t len;
};

// 画面左边那只眼，头歪着所以它更低。眼洞往右下走，闭眼盖在中段，不盖到上面的刘海
static const EyeSpan lowEyeSpans[] = {
  {19, 37, 7},
  {25, 38, 2},
  {26, 39, 4},
  {26, 40, 4},
  {27, 41, 2},
  {27, 42, 3},
};

// 画面右边那只眼，位置更高。上一版盖在眼洞上沿，这里下移 4 行到眼洞中段
static const EyeSpan highEyeSpans[] = {
  {33, 32, 7},
  {33, 33, 5},
  {33, 34, 3},
  {30, 35, 5},
  {30, 36, 6},
  {30, 37, 6},
};

// 先把这只眼的黑眼洞涂成亮的，再沿原来的斜度画一条黑线
static void drawShutEye(const EyeSpan *spans, int spanCount, int x0, int y0, int x1, int y1) {
  // setDrawColor(1)：亮点。黑眼洞被涂上之后，这只眼才算闭上
  u8g2.setDrawColor(1);
  // 逐段涂。改 span 的 x 或 len 会盖到旁边的头发或鼻子
  for (int i = 0; i < spanCount; i++) {
    // drawBox(左, 上, 宽, 高)：高 1 就是这一行。宽是这段黑眼洞的像素数
    u8g2.drawBox(faceDrawX + spans[i].x, faceDrawY + spans[i].y, spans[i].len, 1);
  }
  // setDrawColor(0)：闭眼线画成黑点，斜着压在刚涂上的眼洞上
  u8g2.setDrawColor(0);
  // drawLine(起点 x, 起点 y, 终点 x, 终点 y)：从眼洞左上画到右下，单位像素
  u8g2.drawLine(faceDrawX + x0, faceDrawY + y0, faceDrawX + x1, faceDrawY + y1);
  // 改回亮点。不改的话，后面的信号条和蓝牙标志也会画成黑的
  u8g2.setDrawColor(1);
}

// 按当前步骤盖上闭眼或 wink。睁眼时什么都不画，用点阵里的眼睛
static void drawEyes() {
  // 没在眨眼就保持点阵里的睁眼
  if (!eyeBusy) {
    return;
  }
  // 这一步要画成哪种眼
  int mode = eyeSteps[eyeStepIndex].mode;
  // 两只都闭上。快速眨眼和连眨的闭眼步都走这里
  if (mode == eyeShut) {
    // 左边这只更低，闭眼线从 (20,37) 斜到 (29,42)
    drawShutEye(lowEyeSpans, 6, 20, 37, 29, 42);
    // 右边这只更高，闭眼线从 (33,33) 斜到 (35,37)，比上一版低 4 行
    drawShutEye(highEyeSpans, 6, 33, 33, 35, 37);
    return;
  }
  // 只闭画面左边那只，右边仍用点阵里的睁眼
  if (mode == eyeWinkLeft) {
    drawShutEye(lowEyeSpans, 6, 20, 37, 29, 42);
    return;
  }
  // 只闭画面右边那只
  if (mode == eyeWinkRight) {
    drawShutEye(highEyeSpans, 6, 33, 33, 35, 37);
  }
}

// 人两次眨眼之间通常隔几秒，偶尔会盯久一点，偶尔又隔得比较近
static unsigned long blinkGapMs() {
  // random(含下限, 不含上限)。0 到 99
  int roll = random(0, 100);
  // 大约一成的时间会停 7 到 12 秒，避免眨得像节拍器
  if (roll < 12) {
    return random(7000, 12000);
  }
  // 大约两成隔 1.6 到 2.6 秒，看起来精神一点
  if (roll < 35) {
    return random(1600, 2600);
  }
  // 其余落在 2.8 到 6.2 秒，接近平常说话时的间隔
  return random(2800, 6200);
}

// 排一轮眼睛动作。大多数是一次很快的全眨，少数连眨两次，再少数是单眼 wink
static void planBlink() {
  // 0 到 99，用来决定这一轮做什么
  int roll = random(0, 100);
  // 刚 wink 过就改成眨眼，避免一只眼连着抽
  bool doWink = roll >= 86 && lastGestureWasWink == 0;
  if (doWink) {
    // 记下这一轮是 wink
    lastGestureWasWink = 1;
    // 左右各一半。0 闭左眼，1 闭右眼
    if (random(0, 2) == 0) {
      eyeSteps[0].mode = eyeWinkLeft;
    } else {
      eyeSteps[0].mode = eyeWinkRight;
    }
    // wink 比普通眨眼稍长，不然会看成闪了一下。140 到 219 毫秒
    eyeSteps[0].holdMs = random(140, 220);
    // 这一轮只有一步，结束后回到睁眼
    eyeStepCount = 1;
  } else if (roll < 16) {
    // 连眨不算 wink
    lastGestureWasWink = 0;
    // 第一次闭上，60 到 99 毫秒，属于快眨
    eyeSteps[0].mode = eyeShut;
    eyeSteps[0].holdMs = random(60, 100);
    // 中间睁开 90 到 149 毫秒，两次才分得开
    eyeSteps[1].mode = 0;
    eyeSteps[1].holdMs = random(90, 150);
    // 马上再闭上一次，同样是快眨
    eyeSteps[2].mode = eyeShut;
    eyeSteps[2].holdMs = random(60, 100);
    eyeStepCount = 3;
  } else {
    // 最常见的一次快眨，闭上 50 到 109 毫秒就睁开
    lastGestureWasWink = 0;
    eyeSteps[0].mode = eyeShut;
    eyeSteps[0].holdMs = random(50, 110);
    eyeStepCount = 1;
  }
  // 从第 0 步开始画
  eyeStepIndex = 0;
  // 这一步的起始时间
  eyeStepMs = millis();
  // 进入眨眼，下一次 oledShow 会把眼睛盖上
  eyeBusy = true;
}

void oledShow(int signalBars, bool bleOn) {
  // clearBuffer：无参数。清空内存里的画面，还没送到屏幕
  u8g2.clearBuffer();
  // setDrawColor(1)：后面的点阵和线条都画成亮点
  u8g2.setDrawColor(1);
  // drawXBMP(左, 上, 宽, 高, 点阵)：把头像放在中间偏左
  u8g2.drawXBMP(faceDrawX, faceDrawY, faceWidth, faceHeight, faceBits);
  // 需要闭眼或 wink 时，盖住点阵里的眼睛
  drawEyes();
  // 信号条画在头像之后，右上角不会被头发挡住
  drawSignalBars(signalBars);
  // 左侧蓝牙状态同样后画，避免被头发盖住
  drawBleStatus(bleOn);
  // sendBuffer：无参数。把这一帧送到 SSD1306
  u8g2.sendBuffer();
}

void oledBegin() {
  // begin：无参数。初始化屏幕并打开 I2C
  u8g2.begin();
  // randomSeed(种子)：打乱后面的 random。用当前微秒，每次上电的间隔不会完全一样
  randomSeed(micros());
  // 先睁眼看一会儿，1.6 秒后再开始第一次眨眼
  nextBlinkMs = millis() + 1600;
  // 开机这一帧：WiFi 未连接，蓝牙未连接
  oledShow(-1, false);
}

bool oledPoll() {
  // 现在的毫秒数
  unsigned long now = millis();
  // 正在睁眼等待下一次
  if (!eyeBusy) {
    // 还没到点就继续睁着
    if (now < nextBlinkMs) {
      return false;
    }
    // 到点了，排一轮眨眼或 wink，这一帧就要重画
    planBlink();
    return true;
  }
  // 这一步还没走完
  if (now - eyeStepMs < eyeSteps[eyeStepIndex].holdMs) {
    return false;
  }
  // 进入下一步
  eyeStepIndex = eyeStepIndex + 1;
  // 步骤用完了，回到睁眼，并重新抽下一次的间隔
  if (eyeStepIndex >= eyeStepCount) {
    eyeBusy = false;
    nextBlinkMs = now + blinkGapMs();
    return true;
  }
  // 下一步从现在开始计时
  eyeStepMs = now;
  return true;
}
