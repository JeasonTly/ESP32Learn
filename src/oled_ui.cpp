#include "oled_ui.h"

#include <Arduino.h>
#include <U8g2lib.h>

// 全缓冲构造函数，参数从左到右：
//   旋转：U8G2_R0 不转，U8G2_R1 顺时针 90 度，U8G2_R2 180 度，U8G2_R3 270 度
//   复位脚：U8X8_PIN_NONE 表示这块模块没有单独的复位引脚
//   SCL 时钟线：ESP32 默认 GPIO22
//   SDA 数据线：ESP32 默认 GPIO21
// 名字里的 128X64 是分辨率，F 表示先画在内存里再整屏送出，HW_I2C 表示用硬件 I2C
// 屏幕 I2C 地址默认 0x3C。若模块是 0x3D，在 begin() 之前调用 setI2CAddress(0x3D * 2)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ 22, /* data=*/ 21);

// 瓦力的脸就是一对望远镜镜头。左眼中心、右眼中心、平时的高度，单位像素
// 左边要躲开蓝牙标志，右边要躲开信号条，所以两只眼靠在屏幕中部
const int eyeLeftX = 38;
const int eyeRightX = 72;
const int eyeNeutralY = 29;
// 金属外壳半径、黑镜头半径、白色瞳孔半径
const int eyeHousingR = 15;
const int eyeLensR = 10;
const int eyePupilR = 3;

// 表情。平时两只镜头一样高。开心时外眼角上抬，难过时外眼角下垂，思考时抬起左眼
const int moodNeutral = 0;
const int moodHappy = 1;
const int moodSad = 2;
const int moodThink = 3;

// 眼皮。0 睁开，1 两只都合上，2 只合左眼，3 只合右眼
const int lidOpen = 0;
const int lidShut = 1;
const int lidWinkLeft = 2;
const int lidWinkRight = 3;

// 随机表情的编号。眨眼不在这里，眨眼按固定间隔单独走
const int gestureWink = 0;
const int gestureLook = 1;
const int gestureThink = 2;
const int gestureHappy = 3;
const int gestureSad = 4;

// 一步表情：瞳孔偏移、脸的情绪、眼皮、这一步停多久
// lookX 负是向左，lookY 负是向上。偏移保持在 -4 到 4，瞳孔才不会画出黑镜头
struct GestureStep {
  int8_t lookX;
  int8_t lookY;
  uint8_t mood;
  uint8_t lid;
  uint16_t holdMs;
};

// 四处看最多 4 步，数组留 6 步够用
const int maxGestureSteps = 6;
GestureStep gestureSteps[maxGestureSteps];
// 这一轮有几步
uint8_t gestureStepCount = 0;
// 正在画的是第几步
uint8_t gestureStepIndex = 0;
// 这一步开始的时刻，单位毫秒
unsigned long gestureStepMs = 0;
// true 表示正在做表情，false 表示平时的睁眼
bool gestureBusy = false;
// true 表示这一轮是定时眨眼。false 表示 wink、张望、思考、开心或难过
bool gestureIsBlink = true;
// 下一次眨眼的时刻。间隔按人安静时的眨眼频率，大约每分钟 15 到 20 次
unsigned long nextBlinkMs = 0;
// 下一次随机表情的时刻。和眨眼分开计时，隔几秒抽一次
unsigned long nextEventMs = 0;
// 上一轮随机表情。刚做过的不要马上再来一次
int lastEvent = gestureWink;

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

// 往这一轮表情里加一步。步数已经满了就丢掉，避免写出数组外面
static void pushStep(int lookX, int lookY, int mood, int lid, unsigned long holdMs) {
  if (gestureStepCount >= maxGestureSteps) {
    return;
  }
  gestureSteps[gestureStepCount].lookX = lookX;
  gestureSteps[gestureStepCount].lookY = lookY;
  gestureSteps[gestureStepCount].mood = mood;
  gestureSteps[gestureStepCount].lid = lid;
  gestureSteps[gestureStepCount].holdMs = holdMs;
  gestureStepCount = gestureStepCount + 1;
}

// 按情绪算出一只镜头的圆心。side 为 0 是左眼，1 是右眼
static void eyePlace(int side, int mood, int *x, int *y) {
  int cx = eyeLeftX;
  if (side == 1) {
    cx = eyeRightX;
  }
  int cy = eyeNeutralY;
  // 开心：两只镜头往外抬，连起来像向上的弧
  if (mood == moodHappy) {
    cy = cy - 3;
    if (side == 0) {
      cx = cx - 2;
    } else {
      cx = cx + 2;
    }
  // 难过：两只镜头往外垂
  } else if (mood == moodSad) {
    cy = cy + 4;
    if (side == 0) {
      cx = cx - 1;
    } else {
      cx = cx + 1;
    }
  // 思考：只把左眼抬高一点
  } else if (mood == moodThink && side == 0) {
    cy = cy - 3;
  }
  *x = cx;
  *y = cy;
}

// 画一只镜头。shut 为 1 时盖上眼皮。squint 是上眼皮盖住的像素行数，0 表示不眯
static void drawEye(int cx, int cy, int lookX, int lookY, int shut, int squint) {
  // 外壳是实心亮圆，对应瓦力镜头外面的金属圈
  u8g2.setDrawColor(1);
  u8g2.drawDisc(cx, cy, eyeHousingR);
  if (shut) {
    // 合上后整圈都是金属色，中间留一条黑缝，像快门闭上
    u8g2.setDrawColor(0);
    u8g2.drawHLine(cx - 9, cy, 19);
    u8g2.setDrawColor(1);
    return;
  }
  // 金属圈上刻一圈暗槽，看起来像对焦环
  u8g2.setDrawColor(0);
  u8g2.drawCircle(cx, cy, eyeHousingR - 2);
  // 镜头玻璃是黑的
  u8g2.drawDisc(cx, cy, eyeLensR);
  // 白色瞳孔。lookX、lookY 是相对镜头中心的偏移
  u8g2.setDrawColor(1);
  u8g2.drawDisc(cx + lookX, cy + lookY, eyePupilR);
  // 左上角的小亮块是玻璃反光，不跟着瞳孔走
  u8g2.drawBox(cx - 6, cy - 6, 2, 2);
  if (squint > 0) {
    int top = cy - eyeLensR;
    // setClipWindow(左, 上, 右, 下)：之后只在这个矩形里画点，四边都包含
    // 这里只留下镜头上沿，用来把上眼皮盖上去
    u8g2.setClipWindow(cx - eyeLensR, top, cx + eyeLensR, top + squint - 1);
    u8g2.drawDisc(cx, cy, eyeLensR);
    // setMaxClipWindow：无参数。取消裁剪，不然后面的另一只眼和信号条也会被切掉
    u8g2.setMaxClipWindow();
  }
}

// 两只镜头中间的铰链，以及下面的细脖子。先画脖子，镜头会盖住脖子上端
static void drawNeck(int midX, int midY) {
  int stalkY = midY + 9;
  int plateY = midY + 22;
  u8g2.setDrawColor(1);
  // 细脖子。宽 4，高 14
  u8g2.drawBox(midX - 2, stalkY, 4, 14);
  // 脖子底下的小底座
  u8g2.drawBox(midX - 8, plateY, 16, 3);
  // 底座上两个暗点，像螺丝
  u8g2.setDrawColor(0);
  u8g2.drawPixel(midX - 5, plateY + 1);
  u8g2.drawPixel(midX + 4, plateY + 1);
  u8g2.setDrawColor(1);
}

// 两眼之间的连接块，中间一个螺丝孔
static void drawBridge(int midX, int midY) {
  u8g2.setDrawColor(1);
  u8g2.drawBox(midX - 6, midY - 3, 12, 6);
  u8g2.setDrawColor(0);
  u8g2.drawDisc(midX, midY, 1);
  u8g2.setDrawColor(1);
}

// 按当前这一步画瓦力的脸。没有表情时是平视、睁眼
static void drawWallE() {
  int lookX = 0;
  int lookY = 0;
  int mood = moodNeutral;
  int lid = lidOpen;
  if (gestureBusy) {
    lookX = gestureSteps[gestureStepIndex].lookX;
    lookY = gestureSteps[gestureStepIndex].lookY;
    mood = gestureSteps[gestureStepIndex].mood;
    lid = gestureSteps[gestureStepIndex].lid;
  }
  int lx = 0;
  int ly = 0;
  int rx = 0;
  int ry = 0;
  eyePlace(0, mood, &lx, &ly);
  eyePlace(1, mood, &rx, &ry);
  int midX = (lx + rx) / 2;
  int midY = (ly + ry) / 2;
  drawNeck(midX, midY);
  int leftShut = 0;
  int rightShut = 0;
  if (lid == lidShut || lid == lidWinkLeft) {
    leftShut = 1;
  }
  if (lid == lidShut || lid == lidWinkRight) {
    rightShut = 1;
  }
  // 开心时上眼皮盖住 6 行，镜头微眯
  int squint = 0;
  if (mood == moodHappy) {
    squint = 6;
  }
  drawEye(lx, ly, lookX, lookY, leftShut, squint);
  drawEye(rx, ry, lookX, lookY, rightShut, squint);
  // 铰链后画，盖住两只金属圈之间的缝
  drawBridge(midX, midY);
}

// 这一轮从第 0 步开始计时。blink 为 true 时记成定时眨眼
static void startGesture(bool blink) {
  gestureIsBlink = blink;
  gestureStepIndex = 0;
  gestureStepMs = millis();
  gestureBusy = true;
}

// 很快地眨一次。闭上大约 200 毫秒，屏幕刷新得过来，盯着也能看见
static void planSingleBlink() {
  gestureStepCount = 0;
  pushStep(0, 0, moodNeutral, lidShut, 200);
  startGesture(true);
}

// 连眨两次。中间睁开一小会儿，两次才分得开
static void planDoubleBlink() {
  gestureStepCount = 0;
  pushStep(0, 0, moodNeutral, lidShut, 160);
  pushStep(0, 0, moodNeutral, lidOpen, 140);
  pushStep(0, 0, moodNeutral, lidShut, 160);
  startGesture(true);
}

// 只闭一只眼。左右各一半
static void planWink() {
  int lid = lidWinkLeft;
  if (random(0, 2) == 1) {
    lid = lidWinkRight;
  }
  gestureStepCount = 0;
  pushStep(0, 0, moodNeutral, lid, random(220, 360));
  startGesture(false);
}

// 只朝一个方向看一会儿
static void planGlance() {
  int dir = random(0, 4);
  int lookX = 0;
  int lookY = 0;
  if (dir == 0) {
    lookX = -4;
  } else if (dir == 1) {
    lookX = 4;
  } else if (dir == 2) {
    lookY = -4;
  } else {
    lookY = 4;
  }
  gestureStepCount = 0;
  pushStep(lookX, lookY, moodNeutral, lidOpen, random(650, 1100));
  startGesture(false);
}

// 左、右、上、下各看一停，像在四处观察
static void planScan() {
  gestureStepCount = 0;
  pushStep(-4, 0, moodNeutral, lidOpen, random(420, 640));
  pushStep(4, 0, moodNeutral, lidOpen, random(420, 640));
  pushStep(0, -4, moodNeutral, lidOpen, random(420, 640));
  pushStep(0, 4, moodNeutral, lidOpen, random(420, 640));
  startGesture(false);
}

// 看向左上方，左眼抬起，停得比普通张望更久
static void planThink() {
  gestureStepCount = 0;
  pushStep(-4, -4, moodThink, lidOpen, random(1000, 1700));
  pushStep(-3, -4, moodThink, lidOpen, random(500, 900));
  startGesture(false);
}

// 外眼角上抬并微眯，中间夹一次眨眼
static void planHappy() {
  gestureStepCount = 0;
  pushStep(0, -2, moodHappy, lidOpen, random(280, 450));
  pushStep(0, -2, moodHappy, lidShut, random(70, 110));
  pushStep(0, -2, moodHappy, lidOpen, random(900, 1500));
  startGesture(false);
}

// 外眼角下垂，瞳孔看向下方，眨眼也比平时慢
static void planSad() {
  gestureStepCount = 0;
  pushStep(0, 4, moodSad, lidOpen, random(450, 700));
  pushStep(0, 4, moodSad, lidShut, random(150, 230));
  pushStep(0, 4, moodSad, lidOpen, random(1100, 1800));
  startGesture(false);
}

// 人在安静、没有盯着细看时，大约每分钟眨 15 到 20 次，平均隔 3 到 4 秒
// 不是固定节拍：多数落在这个范围里，偶尔隔得近一点，偶尔会多看几秒
// random(含下限, 不含上限)
static unsigned long blinkGapMs() {
  int roll = random(0, 100);
  // 大约三成隔 2 到 3 秒
  if (roll < 30) {
    return random(2000, 3000);
  }
  // 大约两成会停 4.2 到 6.5 秒
  if (roll >= 80) {
    return random(4200, 6500);
  }
  // 其余一半左右落在 3 到 4.2 秒，整体平均接近每分钟 17 次
  return random(3000, 4200);
}

// 下一次随机表情隔 4 到 8 秒，和眨眼各走各的
static unsigned long eventGapMs() {
  return random(4000, 8000);
}

// 定时眨眼。大约五次里有一次连眨，其余都是单次
static void planBlink() {
  if (random(0, 5) == 0) {
    planDoubleBlink();
  } else {
    planSingleBlink();
  }
}

// 抽一个 wink、四处看、思考、开心或难过。不连着重复同一种
static void planRandomEvent() {
  int event = random(gestureWink, gestureSad + 1);
  if (event == lastEvent) {
    event = event + 1;
    if (event > gestureSad) {
      event = gestureWink;
    }
  }
  lastEvent = event;
  if (event == gestureWink) {
    planWink();
  } else if (event == gestureLook) {
    if (random(0, 2) == 0) {
      planGlance();
    } else {
      planScan();
    }
  } else if (event == gestureThink) {
    planThink();
  } else if (event == gestureHappy) {
    planHappy();
  } else {
    planSad();
  }
}

void oledShow(int signalBars, bool bleOn) {
  // clearBuffer：无参数。清空内存里的画面，还没送到屏幕
  u8g2.clearBuffer();
  // setDrawColor(1)：后面的圆和线条都画成亮点
  u8g2.setDrawColor(1);
  drawWallE();
  // 信号条画在脸之后，右上角不会被镜头挡住
  drawSignalBars(signalBars);
  // 左侧蓝牙状态同样后画
  drawBleStatus(bleOn);
  // sendBuffer：无参数。把这一帧送到 SSD1306
  u8g2.sendBuffer();
}

void oledBegin() {
  // begin：无参数。初始化屏幕并打开 I2C
  u8g2.begin();
  // randomSeed(种子)：打乱后面的 random。用当前微秒，每次上电的间隔不会完全一样
  randomSeed(micros());
  // 第一次眨眼按人的间隔来。随机表情再晚几秒，避免和第一次眨眼叠在一起
  nextBlinkMs = millis() + blinkGapMs();
  nextEventMs = millis() + eventGapMs();
  // 开机这一帧：WiFi 未连接，蓝牙未连接
  oledShow(-1, false);
}

bool oledPoll() {
  unsigned long now = millis();
  // 正在做眨眼或别的表情，先把这一步走完
  if (gestureBusy) {
    if (now - gestureStepMs < gestureSteps[gestureStepIndex].holdMs) {
      return false;
    }
    gestureStepIndex = gestureStepIndex + 1;
    // 步骤用完了，回到平视，并预约下一次
    if (gestureStepIndex >= gestureStepCount) {
      gestureBusy = false;
      if (gestureIsBlink) {
        nextBlinkMs = now + blinkGapMs();
      } else {
        nextEventMs = now + eventGapMs();
        // 随机表情如果占掉了眨眼的点，睁眼后再按人的间隔眨一次
        if (now >= nextBlinkMs) {
          nextBlinkMs = now + blinkGapMs();
        }
      }
      return true;
    }
    gestureStepMs = now;
    return true;
  }
  // 随机表情到点就先做它。眨眼被占掉的话，上面结束时会重新预约
  if (now >= nextEventMs) {
    planRandomEvent();
    return true;
  }
  // 到了人眨眼的间隔就眨一次
  if (now >= nextBlinkMs) {
    planBlink();
    return true;
  }
  return false;
}
