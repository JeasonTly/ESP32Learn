#include "wifi_link.h"

#include <Arduino.h>
#include <WiFi.h>

// 路由器名字。改成你家 WiFi 的 SSID，必须和路由器上完全一致
const char *wifiSsid = "CONQUEST-DEVELOPMENT";
// 路由器密码。空字符串表示开放网络
const char *wifiPassword = "88888888";

// 信号强度多久读一次，单位毫秒。500 就是每秒两次，和翻页互不干扰
const unsigned long rssiRefreshMs = 500;
// 上一次读取信号的时刻，单位毫秒
unsigned long lastRssiMs = 0;
// 最近一次读到的信号强度，单位 dBm。越靠近 0 越强
// 大约 -50 以内很强，-70 左右能正常用，-90 附近快断了
int rssiDbm = 0;
// 屏幕上正在显示的格数。-1 表示未连接，0 到 5 表示涂实了几格
// 只有格数变了才通知重画。dBm 在同一格里小幅跳动时，画面保持不动
int signalBars = -1;

// 下面 5 个门槛决定每一格的 dBm 范围，单位都是 dBm，越靠近 0 越强
// 5 格：大于等于 -50。和路由器在同一房间时常见
const int rssiBar5 = -50;
// 4 格：-60 到 -51
const int rssiBar4 = -60;
// 3 格：-70 到 -61。日常上网一般够用
const int rssiBar3 = -70;
// 2 格：-80 到 -71。能连上，速度容易变慢
const int rssiBar2 = -80;
// 1 格：-90 到 -81。比 -90 更弱（更负）就是 0 格，只留空框
const int rssiBar1 = -90;

// 读当前 WiFi 信号。没连上时返回 0，调用方不要把这个 0 当成满格
static int readRssiDbm() {
  // status() 无参数。WL_CONNECTED 已连接，WL_NO_SSID_AVAIL 找不到名字，
  // WL_CONNECT_FAILED 密码错误，WL_CONNECTION_LOST 中途掉线，WL_DISCONNECTED 已断开，
  // WL_IDLE_STATUS 空闲，WL_SCAN_COMPLETED 扫描结束
  if (WiFi.status() != WL_CONNECTED) {
    return 0;
  }
  // RSSI() 无参数，返回值单位是 dBm。只有已经连上时这个数才有效
  return WiFi.RSSI();
}

// 把 dBm 换成 0 到 5 格。必须已经确认 WiFi 连上再调用
// 没连上时 RSSI 常返回 0，0 会被当成满格
static int rssiToBars(int dbm) {
  // 先比最强的一档。改小 rssiBar5（例如 -40）会更难画满
  if (dbm >= rssiBar5) {
    return 5;
  }
  // 落在 4 格门槛和满格之间
  if (dbm >= rssiBar4) {
    return 4;
  }
  // 落在 3 格门槛和 4 格之间
  if (dbm >= rssiBar3) {
    return 3;
  }
  // 落在 2 格门槛和 3 格之间
  if (dbm >= rssiBar2) {
    return 2;
  }
  // 落在 1 格门槛和 2 格之间。再弱就一格都不涂
  if (dbm >= rssiBar1) {
    return 1;
  }
  return 0;
}

int wifiLinkBars() {
  return signalBars;
}

void wifiLinkBegin() {
  // mode(模式)：WIFI_STA 当客户端去连路由器，WIFI_AP 自己当热点，
  // WIFI_AP_STA 两种同时开，WIFI_OFF 关掉射频。返回 true 表示设置成功
  WiFi.mode(WIFI_STA);
  // begin(名字, 密码, 信道, 路由器 MAC, 是否马上连)
  // 信道写 0 表示自动；MAC 写 NULL 表示不指定某一台路由器；tryConnect 为 true 会立刻开始连接
  // 返回值是当时的 wl_status_t，刚调用时通常还不是 WL_CONNECTED，所以下面继续等
  WiFi.begin(wifiSsid, wifiPassword);
  // 没连上就每秒打一次状态码。WL_CONNECTED 的值是 3
  while (WiFi.status() != WL_CONNECTED) {
    // status() 无参数，取值见上面那一段
    Serial.println("错误代码:" + String(WiFi.status()));
    // delay(毫秒)：停住这么久。1000 就是 1 秒后再看一次
    delay(1000);
    Serial.println("连接WiFi中...");
  }
  Serial.println("WiFi连接成功");
  // localIP() 无参数，返回本机 IP。toString() 无参数，转成可打印的文字
  Serial.println("IP地址: " + WiFi.localIP().toString());
  // 连上后马上读一次，不用等第一个 500 毫秒周期
  rssiDbm = readRssiDbm();
  // 记下当前格数。之后格数没变就不会为了同一次读数再画一屏
  signalBars = rssiToBars(rssiDbm);
  // 记下这一次的时间，下一轮从这里再数 500 毫秒
  lastRssiMs = millis();
}

bool wifiLinkPoll() {
  // 还没到采样间隔就什么都不做
  if (millis() - lastRssiMs < rssiRefreshMs) {
    return false;
  }
  // 重新计时
  lastRssiMs = millis();
  // 先沿用上次的 dBm。没连上时不把 0 写进去，否则重连前会变成满格
  int latestDbm = rssiDbm;
  // -1 表示这一次读下来仍是未连接
  int latestBars = -1;
  // 只有已经连上才读 RSSI，并换成格数
  if (WiFi.status() == WL_CONNECTED) {
    latestDbm = readRssiDbm();
    latestBars = rssiToBars(latestDbm);
  }
  // dBm 和格数都没变，屏幕和串口都保持安静
  if (latestDbm == rssiDbm && latestBars == signalBars) {
    return false;
  }
  // 格数或连接状态和画面上不一致，才需要整屏重画
  bool lookChanged = latestBars != signalBars;
  // 记下这一次的读数，下次用来比较
  rssiDbm = latestDbm;
  signalBars = latestBars;
  // 未连接只打一句。连上了同时打出 dBm 和格数，方便对照门槛
  if (latestBars < 0) {
    Serial.println("WiFi 未连接");
  } else {
    Serial.println("RSSI: " + String(rssiDbm) + " dBm, " + String(latestBars) + "格");
  }
  return lookChanged;
}
