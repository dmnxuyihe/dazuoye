#include "admin_window.h"
#include "user_window.h"
#include "avatar_editor.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
class DesktopTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QPushButton *findButton(QWidget *root, const QString &caption) {
        for (auto b : root->findChildren<QPushButton *>())
            if (b->isVisible() && b->text() == caption)
                return b;
        return nullptr;
    }
    bool hasText(QWidget *root, const QString &text) {
        for (auto l : root->findChildren<QLabel *>())
            if (l->isVisible() && l->text().contains(text))
                return true;
        return false;
    }
    Reply call(ApiClient &api, const QString &method, const QString &path,
               const QJsonObject &body = {}) {
        Reply result;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        api.request(method, path, body, &loop, [&](const Reply &r) {
            result = r;
            loop.quit();
        });
        timer.start(20000);
        loop.exec();
        return result;
    }
  private slots:
    void initTestCase() {
        applyTheme(*qobject_cast<QApplication *>(qApp));
        QDir().mkpath(".runtime/qt-migration/integration");
    }
    void userCharging() {
        CacheStore cache(dir.filePath("user.sqlite"));
        ApiClient api(QUrl(qEnvironmentVariable("ELECTRA_TEST_API")), &cache);
        UserWindow w(&api, &cache);
        w.show();
        QTRY_VERIFY_WITH_TIMEOUT(hasText(&w, "未绑定车辆"), 10000);
        w.login();
        QTRY_VERIFY(w.findChild<QLineEdit *>("phone"));
        w.findChild<QLineEdit *>("phone")->setText("13987651234");
        auto send = findButton(&w, "获取验证码");
        QVERIFY(send);
        QTest::mouseClick(send, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(hasText(&w, "246810"), 10000);
        w.findChild<QLineEdit *>("otp")->setText("246810");
        auto login = findButton(&w, "登录");
        QVERIFY(login);
        QTest::mouseClick(login, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(api.authenticated(), 10000);
        auto profile = call(api, "GET", "/me");
        QVERIFY(profile.ok);
        QString user = text(profile.data.object(), "id");
        QVERIFY(!user.isEmpty());
        bool avatarSaved=false;
        showAvatarEditor(&w,&api,[&](const QJsonObject &){avatarSaved=true;});
        auto avatar=static_cast<AvatarCanvas *>(w.findChild<QWidget *>("avatar-canvas"));QVERIFY(avatar);
        avatar->rotate();avatar->setBrightness(15);const auto expectedAvatar=avatar->result();
        QTest::qWait(100);
        auto avatarSave=w.findChild<QPushButton *>("avatar-save");QVERIFY(avatarSave);
        QTest::mouseClick(avatarSave,Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(avatarSaved,10000);
        const auto savedAvatar=call(api,"GET","/me");QVERIFY(savedAvatar.ok);
        QImage stored;QVERIFY(stored.loadFromData(QByteArray::fromBase64(text(savedAvatar.data.object(),"avatar_data").toLatin1()),"PNG"));
        QCOMPARE(stored.convertToFormat(QImage::Format_ARGB32),expectedAvatar.convertToFormat(QImage::Format_ARGB32));
        // Changes made through the API must appear without navigating or manually refreshing.
        w.navigate("home");
        QVERIFY(call(api,"PUT","/me/vehicle",{{"vehicle_name","跨端同步车辆"},{"vehicle_plate","粤BQT901"},
            {"battery_kwh",64},{"vehicle_soc",35},{"charge_limit",85}}).ok);
        QTRY_VERIFY_WITH_TIMEOUT(hasText(&w,"跨端同步车辆"),10000);
        QVERIFY(w.grab().save(".runtime/qt-migration/integration/user-home.png"));
        CacheStore observerCache(dir.filePath("observer.sqlite"));
        ApiClient observerApi(QUrl(qEnvironmentVariable("ELECTRA_TEST_API")),&observerCache);
        AdminWindow observer(&observerApi,&observerCache); observer.show();
        QTRY_VERIFY_WITH_TIMEOUT(observer.findChild<ArtWidget *>("fleet-occupancy"),10000);
        auto occupancy=observer.findChild<ArtWidget *>("fleet-occupancy");
        QCOMPARE(occupancy->property("displayValue").toDouble(),0.);
        w.navigate("profile");
        QTRY_VERIFY_WITH_TIMEOUT(findButton(&w, "钱包"), 10000);
        QTest::mouseClick(findButton(&w, "钱包"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(findButton(&w, "充值"), 10000);
        auto recharge = findButton(&w, "充值");
        QVERIFY(recharge);
        QTest::mouseClick(recharge, Qt::LeftButton);
        QTRY_VERIFY(w.findChild<QLineEdit *>("amount"));
        w.findChild<QLineEdit *>("amount")->setText("100");
        auto save = findButton(&w, "确认保存");
        QVERIFY(save);
        QSignalSpy events(&api, &ApiClient::eventReceived);
        QTest::mouseClick(save, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(hasText(&w, "100.00"), 10000);
        w.navigate("station");
        QPushButton *reserve = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     for (auto b : w.findChildren<QPushButton *>())
                                         if (b->isVisible() && b->isEnabled() &&
                                             b->text() == "ϟ  预约并准备充电") {
                                             reserve = b;
                                             return true;
                                         }
                                     return false;
                                 })(),
                                 10000);
        QVERIFY(w.grab().save(".runtime/qt-migration/integration/user-station-cards.png"));
        QTest::mouseClick(reserve, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(findButton(&w, "⚡ 开始充电"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(occupancy->property("displayValue").toDouble()>0,10000);
        QTest::mouseClick(findButton(&w, "⚡ 开始充电"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(findButton(&w, "停止充电"), 10000);
        QVERIFY(w.grab().save(".runtime/qt-migration/integration/user-charging.png"));
        QTimer::singleShot(100, [] { if (auto box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click(); });
        QTest::mouseClick(findButton(&w, "停止充电"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     auto r = call(api, "GET", "/me/orders?limit=200");
                                     return r.ok && !r.data.array().isEmpty() &&
                                            text(r.data.array()[0].toObject(), "status") ==
                                                "pending_payment";
                                 })(),
                                 10000);
        auto orders = call(api, "GET", "/me/orders?limit=200").data.array();
        auto id = text(orders[0].toObject(), "id");
        QVERIFY(call(api, "POST", "/orders/" + id + "/stop").ok);
        QTRY_VERIFY_WITH_TIMEOUT(w.findChild<QPushButton *>("pay-order"),10000);
        QTimer::singleShot(100, [] { if (auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))box->button(QMessageBox::Yes)->click();});
        QTest::mouseClick(w.findChild<QPushButton *>("pay-order"),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(text(call(api,"GET","/orders/"+id).data.object(),"status")=="completed",10000);
        QVERIFY(call(api,"POST","/orders/"+id+"/pay").ok);
        QTRY_COMPARE_WITH_TIMEOUT(occupancy->property("displayValue").toDouble(),0.,10000);
        w.navigate("stats");
        const auto invoice = call(api,"GET","/orders/"+id).data.object();
        QTRY_VERIFY_WITH_TIMEOUT(hasText(&w,"¥"+money(number(invoice,"amount"))),10000);
        QVERIFY(observer.grab().save(".runtime/qt-migration/integration/admin-live-dashboard.png"));
        auto entries = call(api, "GET", "/me/wallet-entries").data.array();
        int settlements = 0;
        for (auto e : entries)
            if (text(e.toObject(), "entry_type") == "charge")
                settlements++;
        QCOMPARE(settlements, 1);
        QTRY_VERIFY_WITH_TIMEOUT(events.count() > 0, 5000);
        QTest::qWait(100);
        for (const auto &page : QStringList{"stats", "schedule", "history", "profile"}) {
            w.navigate(page);
            QCoreApplication::processEvents();
            QVERIFY(w.grab().save(".runtime/qt-migration/integration/user-" + page + ".png"));
        }
    }
    void adminNative() {
        CacheStore cache(dir.filePath("admin.sqlite"));
        ApiClient api(QUrl(qEnvironmentVariable("ELECTRA_TEST_API")), &cache);
        AdminWindow w(&api, &cache);
        w.show();
        QTRY_VERIFY_WITH_TIMEOUT(api.authenticated(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(hasText(&w, "运营站点"), 10000);
        auto users = findButton(&w, "用户");
        QVERIFY(users);
        QTest::mouseClick(users, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(findButton(&w, "新增"), 10000);
        QTest::mouseClick(findButton(&w, "新增"), Qt::LeftButton);
        QTRY_VERIFY(w.findChild<QLineEdit *>("phone"));
        w.findChild<QLineEdit *>("phone")->setText("13987651235");
        w.findChild<QLineEdit *>("nickname")->setText("Qt原生管理验收");
        QTRY_VERIFY_WITH_TIMEOUT(findButton(&w, "确认保存"), 10000);
        QTest::mouseClick(findButton(&w, "确认保存"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(
            ([&] {
                auto table = w.findChild<QTableWidget *>();
                return table && table->findItems("Qt原生管理验收", Qt::MatchExactly).size() == 1;
            })(),
            10000);
        auto table = w.findChild<QTableWidget *>();
        auto item = table->findItems("Qt原生管理验收", Qt::MatchExactly)[0];
        table->selectRow(item->row());
        QTimer::singleShot(100, [] { if (auto box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click(); });
        QTest::mouseClick(findButton(&w, "冻结 / 解冻"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(table->findItems("已冻结", Qt::MatchExactly).size() == 1, 10000);
        for (auto d : w.findChildren<QDialog *>())
            d->close();
        for (const auto &page :
             QStringList{"dashboard", "station", "trips", "history", "forecast"}) {
            w.navigate(page);
            if (page == "forecast")
                QTRY_VERIFY_WITH_TIMEOUT(hasText(&w, "暂无可用的同期预测"), 10000);
            QCoreApplication::processEvents();
            QVERIFY(w.grab().save(".runtime/qt-migration/integration/admin-" + page + ".png"));
        }
        auto forecast = call(api, "GET", "/admin/console/forecast");
        QVERIFY(forecast.ok);
        QVERIFY(!forecast.data.object()["available"].toBool());
        QCOMPARE(forecast.data.object()["scopes"].toArray().size(), 14);
        auto artifact = call(api, "GET", "/admin/console/forecast?mode=artifact&scope=all");
        QVERIFY(artifact.ok);
        QCOMPARE(artifact.data.object()["prediction"].toArray().size(), 24);
    }
    void externalPaymentUpdatesOpenStatistics() {
        CacheStore cache(dir.filePath("statistics.sqlite")), writerCache(dir.filePath("writer.sqlite"));
        ApiClient api(QUrl(qEnvironmentVariable("ELECTRA_TEST_API")), &cache);
        ApiClient writer(QUrl(qEnvironmentVariable("ELECTRA_TEST_API")), &writerCache);
        QVERIFY(call(writer,"POST","/auth/otp/request",{{"phone","13987651236"}}).ok);
        const auto login=call(writer,"POST","/auth/otp/verify",{{"phone","13987651236"},{"code","246810"}});
        QVERIFY(login.ok);
        const auto token=text(login.data.object(),"access_token"); api.session(token);writer.session(token);
        QVERIFY(call(writer,"POST","/wallet/recharges",{{"amount",100},{"idempotency_key",uid()}}).ok);
        UserWindow w(&api,&cache);w.show();w.navigate("stats");
        QTRY_VERIFY_WITH_TIMEOUT(hasText(&w,"0 笔"),10000);
        QPointer<QWidget> graph=w.findChild<QWidget *>("personal-statistics");QVERIFY(graph);
        const auto stations=call(writer,"GET","/public/stations").data.array();QVERIFY(!stations.isEmpty());
        const auto chargers=call(writer,"GET","/public/stations/"+text(stations[0].toObject(),"id")+"/chargers").data.array();
        QString charger;
        for(auto v:chargers)if(text(v.toObject(),"status")=="available"){charger=text(v.toObject(),"id");break;}
        QVERIFY(!charger.isEmpty());
        const auto reserved=call(writer,"POST","/orders",{{"charger_id",charger},{"idempotency_key",uid()}});
        QVERIFY(reserved.ok);const auto id=text(reserved.data.object(),"id");
        QVERIFY(call(writer,"POST","/orders/"+id+"/start").ok);
        QVERIFY(call(writer,"POST","/orders/"+id+"/stop").ok);
        QVERIFY(call(writer,"POST","/orders/"+id+"/pay").ok);
        QTRY_VERIFY_WITH_TIMEOUT(hasText(&w,"1 笔"),10000);
        QVERIFY(graph); // Data changed in place while the user remained on this screen.
        QVERIFY(w.grab().save(".runtime/qt-migration/integration/user-live-statistics.png"));
        CacheStore reopenedCache(dir.filePath("reopened.sqlite"));
        ApiClient reopened(QUrl(qEnvironmentVariable("ELECTRA_TEST_API")),&reopenedCache);reopened.session(token);
        UserWindow restored(&reopened,&reopenedCache);restored.show();restored.navigate("stats");
        QTRY_VERIFY_WITH_TIMEOUT(hasText(&restored,"1 笔"),10000);
    }
};
QTEST_MAIN(DesktopTest)
#include "test_desktop.moc"
