#include "client.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
class ClientTest : public QObject {
    Q_OBJECT
  private slots:
    void cacheIsolation() {
        // 验证快照按缓存键隔离，且偏好设置可持久读写。
        QTemporaryDir dir;
        CacheStore c(dir.filePath("cache.sqlite"));
        QVERIFY(c.available());
        c.put("server|user-a|/me", QJsonDocument(QJsonObject{{"balance", "25.00"}}));
        auto r = c.get("server|user-a|/me");
        QVERIFY(r.ok && r.cached);
        QVERIFY(!r.cachedAt.isEmpty());
        QCOMPARE(r.data.object()["balance"].toString(), QString("25.00"));
        QVERIFY(!c.get("server|user-b|/me").ok);
        QVERIFY(!c.get("other-server|user-a|/me").ok);
        c.setPreference("limit", "80");
        QCOMPARE(c.preference("limit"), QString("80"));
    }
    void errors() {
        // 验证业务错误与 FastAPI 参数校验错误都能转换为中文提示。
        QCOMPARE(ApiClient::errorMessage(R"({"error":{"message":"余额不足"}})", 409),
                 QString("余额不足"));
        QCOMPARE(ApiClient::errorMessage(R"({"detail":[{"msg":"输入错误"}]})", 422),
                 QString("输入错误"));
    }
    void offlineReadOnly() {
        // 使用必定不可达的本机端口模拟断网：GET 应回退缓存，POST 应被拒绝。
        QTemporaryDir dir;
        CacheStore c(dir.filePath("cache.sqlite"));
        ApiClient api(QUrl("http://127.0.0.1:1"), &c);
        c.put("http://127.0.0.1:1|public||/public/stations",
              QJsonDocument(QJsonArray{QJsonObject{{"id", "a"}}}));
        bool got = false;
        // 异步回调设置 got；QTRY_VERIFY 会泵送事件循环直到回调完成或超时。
        api.get("/public/stations", this, [&](const Reply &r) {
            QVERIFY(r.ok && r.cached);
            got = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(got, 20000);
        QVERIFY(!api.online());
        bool denied = false;
        api.request("POST", "/orders", {}, this, [&](const Reply &r) {
            QVERIFY(!r.ok);
            QVERIFY(r.error.contains("离线"));
            denied = true;
        });
        QVERIFY(denied);
    }
};
// QTEST_MAIN 自动创建 QApplication 并依次执行 private slots 中的测试函数。
QTEST_MAIN(ClientTest)
#include "test_client.moc"
