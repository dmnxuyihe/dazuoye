#include "admin_window.h"
#include "user_window.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
class DesktopTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QPushButton *findButton(QWidget *root, const QString &caption) {
        // 只查找当前可见且文字完全匹配的按钮，模拟用户实际能点击的控件。
        for (auto b : root->findChildren<QPushButton *>())
            if (b->isVisible() && b->text() == caption)
                return b;
        return nullptr;
    }
    bool hasText(QWidget *root, const QString &text) {
        // 遍历可见标签判断页面是否已渲染目标业务文本。
        for (auto l : root->findChildren<QLabel *>())
            if (l->isVisible() && l->text().contains(text))
                return true;
        return false;
    }
    Reply call(ApiClient &api, const QString &method, const QString &path,
               const QJsonObject &body = {}) {
        // 测试辅助器用局部事件循环把异步 API 临时包装成同步调用，并设置 20 秒上限。
        Reply result;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        // 超时信号退出事件循环，避免网络异常导致测试永久卡住。
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
        // 整个测试套件仅执行一次：加载主题并建立截图输出目录。
        applyTheme(*qobject_cast<QApplication *>(qApp));
        QDir().mkpath(".runtime/qt-migration/integration");
    }
    void userCharging() {
        // 端到端模拟验证码登录、充值、预约、启停结算和各用户页面截图。
        CacheStore cache(dir.filePath("user.sqlite"));
        ApiClient api(QUrl(qEnvironmentVariable("ELECTRA_TEST_API")), &cache);
        UserWindow w(&api, &cache);
        w.show();
        QTRY_VERIFY_WITH_TIMEOUT(hasText(&w, "MG-4"), 10000);
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
        w.navigate("profile");
        QTRY_VERIFY_WITH_TIMEOUT(findButton(&w, "钱包充值"), 10000);
        auto recharge = findButton(&w, "钱包充值");
        QVERIFY(recharge);
        QTest::mouseClick(recharge, Qt::LeftButton);
        QTRY_VERIFY(w.findChild<QLineEdit *>("amount"));
        w.findChild<QLineEdit *>("amount")->setText("100");
        auto save = findButton(&w, "确认保存");
        QVERIFY(save);
        // QSignalSpy 记录 eventReceived 发射次数，用来验证 WebSocket 实时通知。
        QSignalSpy events(&api, &ApiClient::eventReceived);
        QTest::mouseClick(save, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(hasText(&w, "100.00"), 10000);
        w.navigate("station");
        QPushButton *reserve = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     for (auto b : w.findChildren<QPushButton *>())
                                         if (b->isVisible() && b->isEnabled() &&
                                             b->text().contains("kW") &&
                                             b->text().contains("可用")) {
                                             reserve = b;
                                             return true;
                                         }
                                     return false;
                                 })(),
                                 10000);
        QTest::mouseClick(reserve, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(findButton(&w, "⚡ 开始充电"), 10000);
        QTest::mouseClick(findButton(&w, "⚡ 开始充电"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(findButton(&w, "停止充电并结算"), 10000);
        QVERIFY(w.grab().save(".runtime/qt-migration/integration/user-charging.png"));
        QTest::mouseClick(findButton(&w, "停止充电并结算"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     auto r = call(api, "GET", "/me/orders?limit=200");
                                     return r.ok && !r.data.array().isEmpty() &&
                                            text(r.data.array()[0].toObject(), "status") ==
                                                "completed";
                                 })(),
                                 10000);
        auto orders = call(api, "GET", "/me/orders?limit=200").data.array();
        auto id = text(orders[0].toObject(), "id");
        QVERIFY(call(api, "POST", "/orders/" + id + "/stop").ok);
        auto entries = call(api, "GET", "/me/wallet-entries").data.array();
        int settlements = 0;
        for (auto e : entries)
            if (text(e.toObject(), "entry_type") == "charge")
                settlements++;
        QCOMPARE(settlements, 1);
        QTRY_VERIFY_WITH_TIMEOUT(events.count() > 0, 5000);
        w.refresh();
        QTest::qWait(400);
        for (const auto &page : QStringList{"stats", "schedule", "history", "profile"}) {
            w.navigate(page);
            QCoreApplication::processEvents();
            QVERIFY(w.grab().save(".runtime/qt-migration/integration/user-" + page + ".png"));
        }
    }
    void adminNative() {
        // 端到端验证管理员登录、用户新增/冻结、五个原生页面及预测接口。
        CacheStore cache(dir.filePath("admin.sqlite"));
        ApiClient api(QUrl(qEnvironmentVariable("ELECTRA_TEST_API")), &cache);
        AdminWindow w(&api, &cache);
        w.show();
        QTRY_VERIFY_WITH_TIMEOUT(api.authenticated(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(hasText(&w, "我的车辆"), 10000);
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
        QTest::mouseClick(findButton(&w, "冻结 / 解冻"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(table->findItems("已冻结", Qt::MatchExactly).size() == 1, 10000);
        for (auto d : w.findChildren<QDialog *>())
            d->close();
        for (const auto &page :
             QStringList{"dashboard", "station", "trips", "history", "forecast"}) {
            w.navigate(page);
            if (page == "forecast")
                QTRY_VERIFY_WITH_TIMEOUT(hasText(&w, "经验范围测试覆盖率"), 10000);
            QCoreApplication::processEvents();
            QVERIFY(w.grab().save(".runtime/qt-migration/integration/admin-" + page + ".png"));
        }
        auto forecast = call(api, "GET", "/admin/console/forecast");
        QVERIFY(forecast.ok);
        QCOMPARE(forecast.data.object()["prediction"].toArray().size(), 24);
    }
};
// 生成 Qt Test 主函数；界面测试需要 QApplication 而非纯 QCoreApplication。
QTEST_MAIN(DesktopTest)
#include "test_desktop.moc"
