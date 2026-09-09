#pragma once
#include "ui.h"
class Segments : public QWidget {
    Q_OBJECT
  public:
    Segments(const QStringList &labels, int selected = 0, QWidget *parent = nullptr);
  signals:
    void changed(int index);
};
class DataGraphic : public QWidget {
  public:
    enum Type { Trend, GroupedBars, Heatmap, Journey };
    explicit DataGraphic(Type type, QWidget *parent = nullptr);
    void setData(QList<QList<double>> series, QStringList labels = {}, QString unit = {});
    void setJourney(const QJsonObject &order);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

  private:
    Type type;
    QList<QList<double>> series;
    QStringList labels;
    QString unit;
    QJsonObject order;
};
QWidget *pageHeading(const QString &title, QObject *context, std::function<void()> back);
QWidget *detailLine(const QString &icon, const QString &caption, const QString &value);
QWidget *amountBar(const QString &caption, double value, double max, const QColor &color,
                   const QString &unit);
QWidget *emptyPanel(const QString &title, const QString &description,
                    const QString &icon = "history");
