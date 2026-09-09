#include "client.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
class ClientTest : public QObject {
    Q_OBJECT
  private slots:
    void cacheIsolation() {
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
        QCOMPARE(ApiClient::errorMessage(R"({"detail":[{"loc":["body","phone"],"msg":"String should match pattern"}]})",422),QString("请输入正确的11位手机号"));
        QCOMPARE(ApiClient::errorMessage(R"({"detail":[{"loc":["body","password"],"msg":"Field required"}]})",422),QString("密码不能为空且须符合格式及范围要求"));
        QCOMPARE(ApiClient::errorMessage(R"({"error":{"message":"余额不足"}})", 409),
                 QString("余额不足"));
        QCOMPARE(ApiClient::errorMessage(R"({"detail":[{"msg":"输入错误"}]})", 422),
                 QString("输入错误"));
    }
    void offlineReadOnly() {
        QTemporaryDir dir;
        CacheStore c(dir.filePath("cache.sqlite"));
        ApiClient api(QUrl("http://127.0.0.1:1"), &c);
        c.put("http://127.0.0.1:1|public||/public/stations",
              QJsonDocument(QJsonArray{QJsonObject{{"id", "a"}}}));
        bool got = false;
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
QTEST_MAIN(ClientTest)
#include "test_client.moc"
