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
    void dashboard();
    void stationsPage();
    void ordersPage();
    void auditPage();
    void forecastPage();
    void manager(const QString &kind);
    void settings();
    void withdrawalsPage();
    void tariffDialog(const QJsonObject &station, std::function<void()> after);
    void manualLogin();
    QJsonArray stations, chargers, users, orders, logs;
    QJsonObject summary, preferences, analytics;
    QString selectedOrder, selectedStation, forecastScope = "business", orderFilter = "all", orderSearch;
    QPointer<QObject> refreshContext;
    QTimer refreshTimer;
    std::function<void()> dashboardLoader;
    int revenueDays = 7, stationFilter = 0;
    int offsetOrders = 0, offsetLogs = 0;
};
