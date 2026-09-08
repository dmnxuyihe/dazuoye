#pragma once
#include "client.h"
#include <QJsonArray>
#include <QtWidgets>

// 以下辅助函数集中处理 JSON 取值、格式化和常用控件创建，减少页面代码重复。
QString text(const QJsonObject &o, const QString &key, const QString &fallback = "—");
double number(const QJsonObject &o, const QString &key);
QString money(double value);
QString statusText(const QString &value);
QString uid();
QLabel *label(const QString &text, const QString &style = "", QWidget *parent = nullptr);
QPushButton *button(const QString &text, QObject *context, std::function<void()> action,
                    bool primary = false);
QFrame *card(const QString &title, QVBoxLayout **layout = nullptr);
QWidget *row(const QList<QWidget *> &widgets, const QList<int> &stretch = {});
void clearLayout(QLayout *layout);
void applyTheme(QApplication &app);
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
    // 同一个自绘控件通过 Kind 切换车辆、环形进度、地图、折线和能量流效果。
    enum Kind { Car, TopCar, Ring, Gauge, Map, Spark, Flow };
    ArtWidget(Kind kind, QWidget *parent = nullptr);
    void setValue(double v, const QString &caption = {});
    void setStations(const QJsonArray &rows, const QString &selected = {});
    void setValues(const QList<double> &values);
  signals:
    // 地图命中站点时发出站点 ID；外部窗口连接该信号完成页面跳转。
    void stationSelected(QString id);

  protected:
    // Qt 在控件需要重绘/收到鼠标按下时自动调用这两个事件函数。
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

  private:
    Kind kind;
    double value = 60;
    QString caption, selected;
    QJsonArray stations;
    QList<double> values;
    // hits 保存“屏幕坐标 -> 站点 ID”，供 mousePressEvent 做最近点命中测试。
    QList<QPair<QPointF, QString>> hits;
    QJsonArray roads;
    double phase = 0;
    // 动画定时器周期性推进 phase 并触发 update() 重绘。
    QTimer animation;
};
class DesktopWindow : public QMainWindow {
    Q_OBJECT
  public:
    DesktopWindow(ApiClient *api, CacheStore *cache, const QString &title);
    // 派生窗口实现具体页面路由；基类截图流程也通过它逐页导航。
    virtual void navigate(const QString &page) = 0;
    void screenshot(const QString &directory, const QStringList &pages, int delay = 1500);

  protected:
    // API 与缓存由 launch() 创建并保证比窗口活得更久，窗口只持有非拥有型指针。
    ApiClient *api;
    CacheStore *cache;
    QWidget *root, *content;
    QVBoxLayout *outer, *body;
    QHBoxLayout *nav;
    QLabel *connection;
    // current 记录当前路由，刷新数据后可重建同一页面。
    QString current;
    void message(const Reply &r);
    void openUrl(const QString &path);
};
