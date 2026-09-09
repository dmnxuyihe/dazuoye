#pragma once
#include <QJsonArray>
#include <QtWidgets>
#include <QWebEngineView>

class ApiClient;
class StationMapBridge : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    Q_INVOKABLE void selectStation(const QString &id) { emit picked(id); }
    Q_INVOKABLE void locate(double latitude, double longitude) { emit located(latitude, longitude); }
    Q_INVOKABLE void navigateTo(const QString &id) { emit navigationRequested(id); }
    Q_INVOKABLE void geocode(const QString &address) { emit addressRequested(address); }
  signals:
    void addressRequested(QString address);
    void picked(QString id);
    void located(double latitude, double longitude);
    void navigationRequested(QString id);
};

// Only the map is HTML. Business requests and authentication remain in C++.
class StationMap : public QWidget {
    Q_OBJECT
  public:
    explicit StationMap(QWidget *parent = nullptr);
    void setApi(ApiClient *client);
    void setCompact(bool enabled);
    void setLocation(double latitude, double longitude);
    void setStations(const QJsonArray &rows, const QString &selected = {});
    void fitStations(bool allCities = false);
    void zoomAt(double factor, const QPointF &anchor);
  public slots:
    void selectStation(const QString &id);
  signals:
    void stationSelected(QString id);
    void locationChanged(double latitude, double longitude);
  private:
    void sync();
    void message(const QString &text);
    ApiClient *api = nullptr;
    double previewStamp = 0;
    int routeGeneration = 0;
    StationMapBridge *bridge;
    QWebEngineView *view;
    QJsonArray stations;
    QString selected;
    bool ready = false, compact = false;
    double myLatitude = 0, myLongitude = 0;
    bool hasLocation = false;
};
