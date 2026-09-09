#pragma once
#include "client.h"
#include <QJsonArray>
#include <QtWidgets>

namespace DialogAppearance {
inline constexpr qreal WidthRatio = 0.80;
inline constexpr qreal HeightRatio = 0.60;
inline constexpr int BorderWidth = 2;
inline constexpr int CornerRadius = 18;
inline constexpr char BackgroundColor[] = "#2a2033";
inline constexpr char BorderColor[] = "#594566";
}

QString text(const QJsonObject &o, const QString &key, const QString &fallback = "—");
double number(const QJsonObject &o, const QString &key);
QString money(double value);
QString localDateTime(const QString &value);
QString statusText(const QString &value);
QString uid();
QIcon appIcon(const QString &name, const QColor &color = QColor("#d7c5e8"));
QWidget *valueBlock(const QString &value, const QString &unit, const QString &caption);
class NavButton : public QPushButton {
  public:
    NavButton(const QString &title, const QString &icon, bool vertical = false);

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    QString iconName;
    bool vertical;
};
QLabel *label(const QString &text, const QString &style = "", QWidget *parent = nullptr);
QPushButton *button(const QString &text, QObject *context, std::function<void()> action,
                    bool primary = false);
QFrame *card(const QString &title, QVBoxLayout **layout = nullptr);
QWidget *row(const QList<QWidget *> &widgets, const QList<int> &stretch = {});
void clearLayout(QLayout *layout);
void applyTheme(QApplication &app);
void installDialogPolicy(QApplication &app);
QWidget *dialogHeader(QDialog *dialog, QLabel *title);
void showDetail(QWidget *parent, const QString &title, const QJsonObject &data);
void editForm(QWidget *parent, ApiClient *api, const QString &title, const QString &method,
              const QString &path, const QList<QPair<QString, QString>> &fields, QJsonObject values,
              std::function<void()> success);
void command(QWidget *parent, ApiClient *api, const QString &method, const QString &path,
             QJsonObject body, std::function<void(const Reply &)> success);
QWidget *picture(const QString &name, int height);

class ArtWidget : public QWidget {
    Q_OBJECT
  public:
    enum Kind { Car, VerticalCar, TopCar, Ring, Gauge, Spark, Flow, UserHero };
    ArtWidget(Kind kind, QWidget *parent = nullptr);
    void setValue(double v, const QString &caption = {});
    void setValues(const QList<double> &values, const QStringList &labels = {});

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    Kind kind;
    double value = -1;
    QString caption;
    QList<double> values;
    QStringList categories;
    double phase = 0;
    QTimer animation;
};
class DesktopWindow : public QMainWindow {
    Q_OBJECT
  public:
    DesktopWindow(ApiClient *api, CacheStore *cache, const QString &title);
    virtual void navigate(const QString &page) = 0;
    void screenshot(const QString &directory, const QStringList &pages, int delay = 1500);

  protected:
    ApiClient *api;
    CacheStore *cache;
    QWidget *root, *content;
    QVBoxLayout *outer, *body;
    QHBoxLayout *nav;
    QLabel *connection;
    QString current;
    void message(const Reply &r);
    void openUrl(const QString &path);
};
