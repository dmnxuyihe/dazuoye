#pragma once
#include "ui.h"
class UserWindow : public DesktopWindow {
    Q_OBJECT
  public:
    UserWindow(ApiClient *api, CacheStore *cache, ApiClient *historicalClient = nullptr);
    void navigate(const QString &page) override;
    void refresh();
    void login();

  private:
    void home();
    void mapPage();
    void stationPage();
    void charging();
    void statistics();
    void schedule();
    void profile();
    void wallet();
    void history();
    void orderCommand(const QString &action);
    void updateCharging();
    void loadStatistics();
    double batterySoc() const;
    QJsonArray stations, chargers, orders, ledger;
    QJsonObject me, active, analytics;
    QString selectedStation;
    int statsPeriod = 0, tariffMode = 0, chargeMode = 0, historyFilter = 0, walletFilter = 0;
    QString scheduleOrigin = "stats";
    QJsonObject historyData;
    ApiClient *historyApi;
    QWidget *footerBar;
    QTimer poll, events;
    QPointer<QObject> refreshContext;
    QPointer<QLabel> chargeEnergy, chargeCost, chargeState;
    QPointer<QLabel> chargeTime, vehicleState, vehicleTitle;
    QPointer<QWidget> statisticsHost;
    std::function<void()> statisticsLoader;
    QPointer<ArtWidget> batteryArt;
};
