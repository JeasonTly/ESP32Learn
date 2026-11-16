#ifndef WIFI_LINK_H
#define WIFI_LINK_H

// 以站点模式开始连接路由器，马上返回。连上与否交给 wifiLinkPoll
void wifiLinkBegin();

// 到了采样时间就再读一次 RSSI。格数或连接状态变了返回 true，调用方再重画屏幕
bool wifiLinkPoll();

// 当前要画的格数。未连接返回 -1，已连接返回 0 到 5
int wifiLinkBars();

#endif
