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
    void stationChosen(QString stationId);
    void navigationRequested(QString stationId);

  private:
    void rebuild();
    QString selected;
    QJsonArray rows;
    QVBoxLayout *cards;
    QLabel *hint;
    QPushButton *summary;
    QScrollArea *scroll;
    bool expanded = false;
};
