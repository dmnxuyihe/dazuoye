#include "admin_window.h"
#include "user_window.h"
#include "visuals.h"
#include <QtTest>
#include <QtCharts/QChartView>
class FixtureClient : public ApiClient {
  public:
    bool logged = true;
    QJsonObject order;
    QStringList writes;
    QString vehicleName = "测试车辆甲";
    int statisticsReads = 0;
    bool forecastAvailable = true;
    FixtureClient(CacheStore *c) : ApiClient(QUrl("http://127.0.0.1:1"), c) {}
    bool authenticated() const override {
        return logged;
    }
    void session(const QString &) override {
        logged = true;
    }
    QJsonDocument fixture(QString name) {
        QFile f(QString(ELECTRA_FIXTURES_DIR) + "/" + name + ".json");
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll());
    }
    void request(const QString &method, const QString &path, const QJsonObject &body,
                 QObject *context, Callback done, bool = true) override {
        Reply r;
        r.ok = true;
        r.status = 200;
        auto stations = fixture("stations").array();
        QJsonArray chargers, orders, logs;
        for (auto v : stations) {
            auto o = v.toObject();
            chargers.append(QJsonObject{{"id", "gun-" + text(o, "id")},
                                        {"station_id", o["id"]},
                                        {"station_name", o["name"]},
                                        {"code", "DC-01"},
                                        {"kind", "fast"},
                                        {"status", "available"},
                                        {"power_kw", 120}});
        }
        for (int i = 0; i < 12; ++i) {
            auto s = stations[i % stations.size()].toObject();
            orders.append(QJsonObject{
                {"id", QString("fixture-order-%1").arg(i)},
                {"station_name", s["name"]},
                {"charger_code", "DC-01"},
                {"phone", "测试账户"},
                {"status", QStringList{"completed", "charging", "reserved", "cancelled"}[i % 4]},
                {"energy_kwh", 12.3 + i},
                {"amount", 18.2 + i},
                {"reserved_at", QString("2026-09-%1T08:30:00Z").arg(7 - i % 7, 2, 10, QChar('0'))},
                {"ended_at", QString("2026-09-%1T09:30:00Z").arg(7 - i % 7, 2, 10, QChar('0'))}});
            logs.append(
                QJsonObject{{"id", QString::number(i)},
                            {"action", QStringList{"order.reserve", "order.start", "order.stop",
                                                   "wallet.adjustment", "console.settings",
                                                   "charger.update"}[i % 6]},
                            {"target_type", "order"},
                            {"target_id", QString("fixture-%1").arg(i)},
                            {"admin_id", "fixture-admin"},
                            {"created_at", QString("2026-09-%1T%2:30:00Z")
                                               .arg(7 - i % 7, 2, 10, QChar('0'))
                                               .arg(i % 24, 2, 10, QChar('0'))}});
        }
        if (path.startsWith("/auth/"))
            r.data = QJsonDocument(
                QJsonObject{{"access_token", "fixture-only"}, {"development_code", "246810"}});
        else if (method == "POST" && path == "/orders") {
            writes << body["charger_id"].toString();
            order = QJsonObject{{"id", "fixture-order"},
                                {"station_name", "前海枢纽站"},
                                {"status", "reserved"},
                                {"energy_kwh", 0},
                                {"amount", 0}};
            r.data = QJsonDocument(order);
        } else if (path.startsWith("/orders/")) {
            if (method == "POST")
                order["status"] = path.endsWith("start") ? "charging" : "completed";
            r.data = QJsonDocument(order);
        } else if (path.contains("forecast")) {
            auto data = fixture("forecast").object();
            if (!forecastAvailable) {
                data["available"] = false;
                data["message"] = "测试：历史数据尚未导入";
                for (const auto &key : {"prediction", "history", "metrics"}) data.remove(key);
            }
            r.data = QJsonDocument(data);
        } else if (path == "/public/analytics/urbanev")
            r.data = fixture("hourly");
        else if (path.contains("analytics"))
            r.data = fixture("analytics");
        else if (path.endsWith("/settings"))
            r.data = QJsonDocument(
                QJsonObject{{"daily_goal", 72}, {"weekly_goal", 18}, {"vehicle", "Tesla M-3"}});
        else if (path.contains("chargers"))
            r.data = QJsonDocument(chargers);
        else if (path == "/admin/stations" || path == "/public/stations")
            r.data = QJsonDocument(stations);
        else if (path.contains("ops-logs"))
            r.data = QJsonDocument(logs);
        else if (path.contains("orders?"))
            r.data = QJsonDocument(orders);
        else if (path.startsWith("/me/statistics")) {
            ++statisticsReads;
            r.data = QJsonDocument(QJsonObject{{"daily", QJsonArray{}}, {"stations", QJsonArray{}}});
        } else if (path == "/me")
            r.data = QJsonDocument(
                QJsonObject{{"nickname", "测试车主"}, {"phone", "仅界面测试"}, {"balance", 128.5},
                            {"vehicle_name", vehicleName}, {"vehicle_soc", 40}, {"battery_kwh", 60}});
        else if (path.contains("users") || path.contains("wallet-entries"))
            r.data = QJsonDocument(QJsonArray{});
        else
            r.data = QJsonDocument(QJsonObject{});
        QTimer::singleShot(0, context, [r, done] { done(r); });
    }
};
class VisualTest : public QObject {
    Q_OBJECT
    QPushButton *buttonWith(QWidget &w, QString text) {
        for (auto b : w.findChildren<QPushButton *>())
            if (b->text() == text && b->isVisible())
                return b;
        return nullptr;
    }
  private slots:
    void accountAndStatisticsStayLive() {
        QTemporaryDir dir;
        CacheStore cache(dir.filePath("live.sqlite"));
        FixtureClient api(&cache);
        UserWindow w(&api, &cache); w.show();
        auto contains = [&](const QString &value) {
            for (auto l : w.findChildren<QLabel *>())
                if (l->isVisible() && l->text() == value) return true;
            return false;
        };
        QTRY_VERIFY(contains("测试车辆甲"));
        api.vehicleName = "测试车辆乙";
        w.refresh();
        QTRY_VERIFY(contains("测试车辆乙"));
        w.navigate("stats");
        QTRY_VERIFY(api.statisticsReads > 0);
        const auto reads = api.statisticsReads;
        QPointer<DataGraphic> graph = w.findChild<DataGraphic *>();
        QVERIFY(graph);
        emit api.eventReceived(QJsonObject{{"type", "order.paid"}});
        QTRY_VERIFY(api.statisticsReads > reads);
        QVERIFY(graph);
    }
    void refreshAndReturnNavigation() {
        QTemporaryDir dir;
        CacheStore cache(dir.filePath("refresh.sqlite"));
        FixtureClient api(&cache), history(&cache);
        UserWindow w(&api, &cache, &history); w.show(); QTest::qWait(100);
        w.navigate("stats"); QTest::qWait(50);
        QPointer<DataGraphic> chart = w.findChild<DataGraphic *>();
        QVERIFY(chart);
        w.refresh(); QTest::qWait(100);
        QVERIFY(chart); // Background refresh must not replace the user's chart.
        w.navigate("profile");
        w.navigate("schedule"); QTest::qWait(50);
        auto back = buttonWith(w, "‹"); QVERIFY(back); back->click();
        QTRY_VERIFY(buttonWith(w,"钱包"));
        w.navigate("map");
        QTRY_VERIFY(buttonWith(w,"首页"));
        QTRY_VERIFY(buttonWith(w,"我的"));
    }
    void dashboardLayoutAndLiveMetrics() {
        QTemporaryDir dir; CacheStore cache(dir.filePath("layout.sqlite")); FixtureClient api(&cache);
        AdminWindow w(&api,&cache); w.show();
        QTRY_VERIFY(w.findChild<ArtWidget *>("fleet-occupancy"));
        auto car = w.findChild<ArtWidget *>("fleet-occupancy");
        QCOMPARE(car->property("displayValue").toDouble(),0.);
        QPointer<QWidget> top=w.findChild<QWidget *>("dashboard-top");
        auto period=w.findChild<QComboBox *>("revenue-period"); QVERIFY(period);
        period->setCurrentIndex(1); emit period->activated(1);
        w.refresh(); QTest::qWait(100);
        QVERIFY(top); QCOMPARE(period->currentData().toInt(),30);
        for(auto size:{QSize(1060,720),QSize(1460,1000)}) {
            w.resize(size); QTest::qWait(100);
            QVERIFY(top->height()<=360);
            for(auto child:top->findChildren<ArtWidget *>()) {
                QVERIFY(child->width()>150); QVERIFY(child->height()<=225);
            }
            auto scroll=w.findChild<QScrollArea *>("page-scroll"); QVERIFY(scroll);
            QVERIFY(scroll->widget()->width()<=scroll->viewport()->width());
        }
    }
    void userPages() {
        QTemporaryDir dir;
        CacheStore cache(dir.filePath("user.sqlite"));
        FixtureClient api(&cache), history(&cache);
        UserWindow w(&api, &cache, &history);
        w.show();
        QTest::qWait(100);
        QDir().mkpath(".runtime/qt-complete/test-screenshots/user");
        for (auto page :
             {"home", "map", "station", "charging", "stats", "schedule", "history", "profile", "avatar"}) {
            w.navigate(page);
            QTest::qWait(100);
            QVERIFY(w.grab().save(
                QString(".runtime/qt-complete/test-screenshots/user/%1.png").arg(page)));
        }
        w.navigate("station");
        QTest::qWait(100);
        auto reserve = buttonWith(w, "ϟ  预约并准备充电");
        QVERIFY(reserve);
        QVERIFY(reserve->isEnabled());
        QTest::mouseClick(reserve, Qt::LeftButton);
        QTRY_COMPARE(api.writes.size(), 1);
        QTRY_VERIFY(buttonWith(w, "⚡ 开始充电"));
        QTest::mouseClick(buttonWith(w, "⚡ 开始充电"), Qt::LeftButton);
        QTRY_VERIFY(buttonWith(w, "停止充电"));
    }
    void adminPages() {
        QTemporaryDir dir;
        CacheStore cache(dir.filePath("admin.sqlite"));
        FixtureClient api(&cache);
        AdminWindow w(&api, &cache);
        w.show();
        QTest::qWait(100);
        QDir().mkpath(".runtime/qt-complete/test-screenshots/admin");
        for (auto page : {"dashboard", "station", "trips", "history", "forecast"}) {
            w.navigate(page);
            QTest::qWait(130);
            if (QString(page)=="trips") {
                auto progress=w.findChild<QWidget *>("order-progress");
                auto detail=w.findChild<QWidget *>("selected-order-detail");
                QVERIFY(progress);QVERIFY(detail);
                QVERIFY(progress->mapTo(&w,QPoint(0,progress->height())).y()<=detail->mapTo(&w,QPoint(0,0)).y());
            }
            QVERIFY(w.grab().save(
                QString(".runtime/qt-complete/test-screenshots/admin/%1.png").arg(page)));
        }
    }
    void forecastDataAndUnavailableState() {
        QTemporaryDir dir; CacheStore cache(dir.filePath("forecast.sqlite")); FixtureClient api(&cache);
        AdminWindow w(&api,&cache);w.show();QTest::qWait(100);
        w.navigate("forecast");
        QTRY_VERIFY(w.findChild<QChartView *>());
        QTRY_VERIFY(w.findChild<QComboBox *>("forecast-scope"));
        auto select=w.findChild<QComboBox *>("forecast-scope");QVERIFY(select);
        QCOMPARE(select->count(),14);
        QCOMPARE(select->currentData().toString(),QString("business"));
        api.forecastAvailable=false;
        w.navigate("forecast");QTest::qWait(100);
        QVERIFY(!w.findChild<QChartView *>());
        select=w.findChild<QComboBox *>("forecast-scope");QVERIFY(select);QCOMPARE(select->count(),14);
        bool message=false;
        for(auto label:w.findChildren<QLabel *>())
            if(label->text().contains("测试：历史数据尚未导入"))message=true;
        QVERIFY(message);
        api.forecastAvailable=true;
        select->setCurrentIndex(1);emit select->activated(1);
        QTRY_VERIFY(w.findChild<QChartView *>());
    }
    void dialogDismissal() {
        QTemporaryDir dir;
        CacheStore cache(dir.filePath("dialogs.sqlite"));
        FixtureClient api(&cache), history(&cache);
        UserWindow user(&api, &cache, &history);
        AdminWindow admin(&api, &cache);
        user.show(); admin.show();
        QTest::qWait(150);
        auto checkClose = [&](QWidget &owner, const QString &shot) {
            QDialog *dialog = nullptr;
            for (auto d : owner.findChildren<QDialog *>()) if (d->isVisible()) dialog = d;
            if (!dialog) return false;
            auto close = dialog->findChild<QPushButton *>("dialog-close");
            if (!close || !close->isVisible() || !dialog->rect().contains(close->mapTo(dialog, close->rect().center()))) return false;
            QDir().mkpath(".runtime/qt-dialog-fix");
            dialog->grab().save(".runtime/qt-dialog-fix/" + shot + ".png");
            QSignalSpy rejected(dialog, &QDialog::rejected);
            QTest::mouseClick(close, Qt::LeftButton);
            return rejected.count() == 1 && !dialog->isVisible();
        };
        auto management = buttonWith(admin, "用户");
        QVERIFY(management); management->click(); QTest::qWait(80);
        QVERIFY(checkClose(admin, "manager"));
        user.navigate("profile"); QTest::qWait(80);
        auto wallet = buttonWith(user, "钱包");
        QVERIFY(wallet); wallet->click(); QTest::qWait(30);
        auto recharge = buttonWith(user, "充值");
        QVERIFY(recharge); recharge->click(); QTest::qWait(80);
        QVERIFY(checkClose(user, "wallet"));
        showDetail(&user, "详情", {{"name", "只读测试"}}); QTest::qWait(30);
        QVERIFY(checkClose(user, "detail"));
        editForm(&user, &api, "编辑资料", "PATCH", "/me", {{"nickname", "昵称"}}, {}, [] {});
        QTest::qWait(30); QVERIFY(checkClose(user, "form"));
        QVERIFY(api.writes.isEmpty());
        // Esc still dismisses, without submitting the form.
        editForm(&user, &api, "编辑资料", "PATCH", "/me", {{"nickname", "昵称"}}, {}, [] {});
        QTest::qWait(30);
        for (auto d : user.findChildren<QDialog *>()) if (d->isVisible()) {
            QSignalSpy rejected(d, &QDialog::rejected);
            QTest::keyClick(d, Qt::Key_Escape);
            QCOMPARE(rejected.count(), 1);
        }
        QVERIFY(api.writes.isEmpty());
    }
    void segments() {
        Segments tabs({"月度", "年度"});
        tabs.show();
        QSignalSpy spy(&tabs, &Segments::changed);
        auto buttons = tabs.findChildren<QPushButton *>();
        QTest::mouseClick(buttons[1], Qt::LeftButton);
        QCOMPARE(spy.count(), 1);
        QVERIFY(buttons[1]->isChecked());
        QVERIFY(!buttons[0]->isChecked());
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    applyTheme(app);
    VisualTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_visuals.moc"
