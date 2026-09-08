#pragma once
#include "ui.h"
class AdminWindow : public DesktopWindow {
    Q_OBJECT
  public:
    AdminWindow(ApiClient *api, CacheStore *cache);
    void navigate(const QString &page) override;
    void login();
    void refresh();

  private:
    // 每个函数负责重建一个业务页面；manager 是用户、站点、充电枪、订单的通用管理器。
    void dashboard();
    void stationsPage();
    void ordersPage();
    void auditPage();
    void forecastPage();
    void manager(const QString &kind);
    void settings();
    void manualLogin();
    // 最近一次批量请求得到的内存快照，页面渲染不直接阻塞等待网络。
    QJsonArray stations, chargers, users, orders, logs;
    QJsonObject summary, preferences, analytics;
    QString selectedStation, forecastScope = "all", orderFilter = "all", orderSearch;
    // 刷新上下文销毁后，旧请求回调会因 QPointer 变空而被 ApiClient 忽略。
    QPointer<QObject> refreshContext;
    // WebSocket 事件触发短延迟合并刷新，避免事件密集时连续请求。
    QTimer refreshTimer;
    int offsetOrders = 0, offsetLogs = 0;
};
