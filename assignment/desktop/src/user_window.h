#pragma once
#include "ui.h"
class UserWindow : public DesktopWindow {
    Q_OBJECT
  public:
    UserWindow(ApiClient *api, CacheStore *cache);
    void navigate(const QString &page) override;
    void refresh();
    void login();

  private:
    // 页面函数只负责根据内存数据构建 QWidget；refresh() 统一负责取数。
    void home();
    void mapPage();
    void stationPage();
    void charging();
    void statistics();
    void schedule();
    void profile();
    void history();
    void orderCommand(const QString &action);
    QJsonArray stations, chargers, orders, ledger;
    QJsonObject me, active, analytics;
    QString selectedStation;
    // poll 在充电中定期更新订单；events 把多条实时事件合并为一次刷新。
    QTimer poll, events;
    QPointer<QObject> refreshContext;
    // QPointer 防止离开充电页后，轮询回调继续写入已销毁的标签。
    QPointer<QLabel> chargeEnergy, chargeCost, chargeState;
};
