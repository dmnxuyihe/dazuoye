#pragma once

#include <QJsonArray>
#include <QtWidgets>

class StationRecommendations : public QWidget {
    Q_OBJECT
  public:
    explicit StationRecommendations(QWidget *parent = nullptr);
    void setStations(const QJsonArray &stations, const QString &selectedId);
    void setSelected(const QString &stationId);

  signals:
    void stationSelected(QString stationId);

  private:
    void rebuild();
    QString selected;
    QJsonArray rows;
    QHBoxLayout *cards;
    QLabel *hint;
};
