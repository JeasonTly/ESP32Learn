#include "bt_link.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

// 手机扫描列表里显示的名字。改掉之后安卓要按新名字去连
const char *btName = "ESP32-Tuly";
// 自定义服务。安卓连接后按这串 UUID 找到引脚控制
const char *pinServiceUuid = "9b3c0001-7e2a-4c1d-8f60-1a2b3c4d5e6f";
// 手机把指令写进这个特征。只写，不提供读取
const char *pinCommandUuid = "9b3c0002-7e2a-4c1d-8f60-1a2b3c4d5e6f";
// 执行结果放在这里。手机可以读，也可以订阅通知
const char *pinReplyUuid = "9b3c0003-7e2a-4c1d-8f60-1a2b3c4d5e6f";

// 允许安卓控制的引脚。屏幕占用 21 和 22，下载和启动占用 0、2、12、15，闪存占用 6 到 11
// 改这张表就能放开或禁止某一脚。数字是 GPIO 编号，不是开发板丝印上的其它名字
const int allowedPins[] = {4, 5, 13, 14, 16, 17, 18, 19, 23, 25, 26, 27, 32, 33};
// 上面数组有几项。以后只改数组内容，这里会跟着变
const int allowedPinCount = sizeof(allowedPins) / sizeof(allowedPins[0]);

// 回复特征。打开成功后才赋值，失败时保持空指针，避免对空对象发通知
BLECharacteristic *replyCharacteristic = nullptr;
// 有没有手机连着。蓝牙回调和 loop 不在同一个任务里，所以用 volatile
volatile bool bleLinkUp = false;

// 这一脚是否在允许表里。不在表里就不要执行 pinMode，避免把屏幕或下载脚改成输出
static bool pinAllowed(int pin) {
  // 逐项对照允许表
  for (int i = 0; i < allowedPinCount; i++) {
    // 命中就不用再往下找
    if (allowedPins[i] == pin) {
      return true;
    }
  }
  return false;
}

// 解析 "23,1" 这种文字。成功时写出引脚号和 0 或 1
// 引脚只接受 1 到 2 位数字，电平只能是单个 0 或 1。前后空格和回车会被去掉
static bool parseCommand(const String &raw, int *pin, int *level) {
  // 复制一份再裁剪，避免改到蓝牙库里的原文
  String text = raw;
  // trim() 无参数，去掉两端空格、换行和回车
  text.trim();
  // indexOf 返回逗号下标。找不到时是 -1
  int comma = text.indexOf(',');
  // 逗号不能在开头，后面还得留着电平那一位
  if (comma < 1 || comma >= (int)text.length() - 1) {
    return false;
  }
  // substring(起, 止) 不含止下标。逗号左边是引脚
  String pinText = text.substring(0, comma);
  // 逗号右边是电平
  String levelText = text.substring(comma + 1);
  pinText.trim();
  levelText.trim();
  // 引脚最多两位。电平必须正好是 0 或 1，不接受 01、high 这类写法
  if (pinText.length() < 1 || pinText.length() > 2 || (levelText != "0" && levelText != "1")) {
    return false;
  }
  // 引脚里混进字母就整条作废
  for (unsigned int i = 0; i < pinText.length(); i++) {
    // charAt(下标) 取出一个字符。isDigit 判断是不是 '0' 到 '9'
    if (!isDigit(pinText.charAt(i))) {
      return false;
    }
  }
  // toInt() 无参数，把十进制文字转成整数。非数字会得到 0，上面已经排除了
  *pin = pinText.toInt();
  // 电平字符 '1' 记为 1，否则记为 0
  *level = levelText.charAt(0) == '1' ? 1 : 0;
  return true;
}

// 把结果写进回复特征，并通知已经订阅的手机。同时打到 USB 串口
static void replyToPhone(const String &text) {
  // 蓝牙没打开时没有特征，只保留串口输出
  if (replyCharacteristic != nullptr) {
    // setValue(文字)：用这串 UTF-8 替换特征当前值。手机之后来读，得到的就是它
    replyCharacteristic->setValue(text);
    // notify() 无参数：把当前值推给已订阅的手机。没人订阅时手机收不到，但读特征仍然有效
    replyCharacteristic->notify();
  }
  // println 会在末尾加换行。监视器里看执行结果
  Serial.println(text);
}

// 手机写入指令时，库在蓝牙任务里调用这个类。对象必须一直活着，不能写在函数里面
class PinCommandCallbacks : public BLECharacteristicCallbacks {
  // onWrite(特征)：手机刚写完。参数就是被写入的那个特征
  void onWrite(BLECharacteristic *characteristic) override {
    // getValue() 无参数，返回手机这次写来的 UTF-8 文字
    String raw = characteristic->getValue();
    // 解析出的引脚号。解析失败时不会使用
    int pin = 0;
    // 解析出的电平，0 或 1
    int level = 0;
    // 格式不对就回复 ERR format，引脚保持原样
    if (!parseCommand(raw, &pin, &level)) {
      replyToPhone("ERR format");
      return;
    }
    // 屏幕、下载和闪存脚直接拒绝，避免把正在用的脚改成输出
    if (!pinAllowed(pin)) {
      replyToPhone("ERR pin");
      return;
    }
    // pinMode(引脚, 模式)：OUTPUT 推挽输出，INPUT 高阻输入，
    // INPUT_PULLUP 内部上拉，INPUT_PULLDOWN 内部下拉。这里每次设成输出，第一次控制才会真正驱动该脚
    pinMode(pin, OUTPUT);
    // digitalWrite(引脚, 电平)：HIGH 约 3.3V，LOW 为 0V。把 1 写成 HIGH，0 写成 LOW
    digitalWrite(pin, level == 1 ? HIGH : LOW);
    // 拼出手机能直接判断的结果。例如 OK 23=1
    replyToPhone("OK " + String(pin) + "=" + String(level));
  }
};

// 连接和断开只打日志。断开后的重新广播由 advertiseOnDisconnect 负责
class LinkCallbacks : public BLEServerCallbacks {
  // onConnect(服务器)：有手机连上时调用。参数是本机这个从机
  void onConnect(BLEServer *server) override {
    // 至少这一台已经连上。屏幕左侧的点会改成实心
    bleLinkUp = true;
    // getConnectedCount() 无参数，返回当前连着的手机数量
    Serial.println("手机已连上，数量 " + String(server->getConnectedCount()));
  }

  // onDisconnect(服务器)：手机断开时调用
  void onDisconnect(BLEServer *server) override {
    // 回调发生时，这次断开还没从计数里减掉。大于 1 才说明还有别的手机
    bleLinkUp = server->getConnectedCount() > 1;
    Serial.println("手机已断开，数量 " + String(server->getConnectedCount()));
  }
};

// 这两个回调对象活得和程序一样久。写在函数里的话，函数返回后蓝牙库还会调用它们
PinCommandCallbacks pinCommandCallbacks;
LinkCallbacks linkCallbacks;

bool btLinkConnected() {
  return bleLinkUp;
}

void btLinkBegin() {
  // init(设备名)：启动低功耗蓝牙，并记下广播名字。返回 false 表示协议栈没起来
  if (!BLEDevice::init(btName)) {
    Serial.println("低功耗蓝牙打开失败");
    return;
  }
  // createServer() 无参数，本机当从机，等安卓来连
  BLEServer *server = BLEDevice::createServer();
  // setCallbacks(回调对象)：连接和断开时调用 linkCallbacks
  server->setCallbacks(&linkCallbacks);
  // advertiseOnDisconnect(是否继续广播)：true 时手机断开后马上又能被扫到
  server->advertiseOnDisconnect(true);
  // createService(UUID)：建立一个服务。安卓靠 pinServiceUuid 找到它
  BLEService *service = server->createService(pinServiceUuid);
  // createCharacteristic(UUID, 属性)：PROPERTY_WRITE 表示手机可以写入并等待本机确认
  // 另外还有 PROPERTY_READ 读取、PROPERTY_NOTIFY 通知、PROPERTY_WRITE_NR 写入但不等确认
  BLECharacteristic *commandCharacteristic = service->createCharacteristic(
    pinCommandUuid,
    BLECharacteristic::PROPERTY_WRITE
  );
  // 有写入时交给 pinCommandCallbacks
  commandCharacteristic->setCallbacks(&pinCommandCallbacks);
  // 回复特征同时允许读取和通知。手机不订阅时仍可以主动读
  replyCharacteristic = service->createCharacteristic(
    pinReplyUuid,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  // BLE2902 是标准的客户端配置描述符，UUID 固定 0x2902。安卓写 0x0001 表示订阅通知
  // 这个对象要留到关机，所以用 new，不在这里 delete
  replyCharacteristic->addDescriptor(new BLE2902());
  // 还没有指令时，读回复特征得到这句，方便确认特征找对了
  replyCharacteristic->setValue("READY");
  // start() 无参数，把服务登记进协议栈。没调用的话手机连接后看不到特征
  service->start();
  // getAdvertising() 无参数，取出广播对象
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  // addServiceUUID(UUID)：广播包里带上服务号，安卓可以按服务过滤
  advertising->addServiceUUID(pinServiceUuid);
  // setScanResponse(是否使用扫描回复)：true 时设备名放在扫描回复包。名字本身来自上面的 init
  advertising->setScanResponse(true);
  // startAdvertising() 无参数，开始被手机扫描
  BLEDevice::startAdvertising();
  Serial.println("低功耗蓝牙已打开，名字 ESP32-OLED");
  Serial.println("写入特征 9b3c0002-7e2a-4c1d-8f60-1a2b3c4d5e6f");
  Serial.println("指令格式: 引脚,电平    例如 23,1");
}
