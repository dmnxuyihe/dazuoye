#include "client.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QNetworkReply>
#include <QPointer>
#include <QMap>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrlQuery>
#include <QUuid>
#include <QVariant>

CacheStore::CacheStore(const QString &path) : connection(QUuid::createUuid().toString()) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(path);
    if (db.open()) {
        QSqlQuery q(db);
        q.exec("CREATE TABLE IF NOT EXISTS snapshots (key TEXT PRIMARY KEY, body BLOB NOT NULL, "
               "saved_at TEXT NOT NULL)");
        q.exec(
            "CREATE TABLE IF NOT EXISTS preferences (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
    }
}
CacheStore::~CacheStore() {
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection);
}
bool CacheStore::available() const {
    return db.isOpen();
}
void CacheStore::put(const QString &key, const QJsonDocument &data) {
    if (!available())
        return;
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO snapshots VALUES(?,?,?)");
    q.addBindValue(key);
    q.addBindValue(data.toJson(QJsonDocument::Compact));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.exec();
}
Reply CacheStore::get(const QString &key) const {
    Reply r;
    if (!available())
        return r;
    QSqlQuery q(db);
    q.prepare("SELECT body,saved_at FROM snapshots WHERE key=?");
    q.addBindValue(key);
    if (q.exec() && q.next()) {
        r.data = QJsonDocument::fromJson(q.value(0).toByteArray());
        r.ok = !r.data.isNull();
        r.cached = true;
        r.cachedAt = q.value(1).toString();
    }
    return r;
}
void CacheStore::setPreference(const QString &key, const QString &value) {
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO preferences VALUES(?,?)");
    q.addBindValue(key);
    q.addBindValue(value);
    q.exec();
}
QString CacheStore::preference(const QString &key, const QString &fallback) const {
    QSqlQuery q(db);
    q.prepare("SELECT value FROM preferences WHERE key=?");
    q.addBindValue(key);
    return q.exec() && q.next() ? q.value(0).toString() : fallback;
}
ApiClient::ApiClient(QUrl url, CacheStore *store, QObject *parent)
    : QObject(parent), base(url), cache(store) {
    retry.setSingleShot(true);
    connect(&retry, &QTimer::timeout, this, &ApiClient::connectStream);
    connect(&socket, &QWebSocket::connected, this, [this] {
        backoff = 1000;
        emit streamChanged(true);
    });
    connect(&socket, &QWebSocket::disconnected, this, [this] {
        emit streamChanged(false);
        if (int(socket.closeCode()) == 4401 && !bearer.isEmpty()) {
            logout();
            emit sessionExpired();
            return;
        }
        if (!bearer.isEmpty()) {
            retry.start(backoff);
            backoff = qMin(30000, backoff * 2);
        }
    });
    connect(&socket, &QWebSocket::textMessageReceived, this, [this](const QString &message) {
        auto event = QJsonDocument::fromJson(message.toUtf8()).object();
        auto id = event.value("id").toVariant().toLongLong();
        if (id > cursor) {
            cursor = id;
            emit eventReceived(event);
        } // Cursor stays session-local; refresh snapshots after reconnect.
    });
}
ApiClient::~ApiClient() {
    // QWebSocket may emit disconnected while being destroyed. Stop it while all
    // member timers/session strings still exist, before reverse-order teardown.
    logout();
    socket.disconnect(this);
    retry.stop();
    manager.disconnect(this);
}
QString ApiClient::errorMessage(const QByteArray &body, int status) {
    const auto o = QJsonDocument::fromJson(body).object();
    if (o.value("error").isObject())
        return o.value("error").toObject().value("message").toString();
    if (o.value("detail").isString())
        return o.value("detail").toString();
    if (o.value("detail").isArray()) {
        QStringList errors;
        for (const auto &v : o.value("detail").toArray()) {
            auto e = v.toObject();
            auto loc = e.value("loc").toArray();
            auto key = loc.isEmpty() ? QString() : loc.last().toString();
            const QMap<QString, QString> fields = {{"phone", "手机号"}, {"code", "验证码"},
                {"username", "管理员账号"}, {"password", "密码"}, {"amount", "金额"},
                {"nickname", "昵称"}, {"initial_soc", "当前电量"}, {"target_soc", "充电上限"},
                {"battery_kwh", "电池容量"}};
            QString msg = e.value("msg").toString();
            if (msg.contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")))
                errors << msg.remove("Value error, ");
            else if (key == "phone") errors << "请输入正确的11位手机号";
            else if (key == "code") errors << "请输入6位数字验证码";
            else errors << fields.value(key, "输入内容") + "不能为空且须符合格式及范围要求";
        }
        return errors.join("；");
    }
    return status ? QString("请求未完成（HTTP %1）").arg(status)
                  : QString("无法连接服务，请检查网络后重试");
}
void ApiClient::request(const QString &method, const QString &path, const QJsonObject &body,
                        QObject *context, Callback done, bool cacheRead) {
    if (method != "GET" && !reachable && !path.startsWith("/auth/")) {
        Reply r;
        r.error = "当前离线，只能查看缓存；请刷新连接后操作";
        done(r);
        return;
    }
    QNetworkRequest req(base.resolved(QUrl(path)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(15000);
    if (!bearer.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + bearer).toUtf8());
    auto reply = manager.sendCustomRequest(
        req, method.toUtf8(),
        method == "GET" || method == "DELETE" ? QByteArray()
                                              : QJsonDocument(body).toJson(QJsonDocument::Compact));
    const QString key = base.toString() + "|" + subject + "|" + sessionRole + "|" + path;
    const int epoch = generation;
    QPointer<QObject> guard(context);
    connect(reply, &QNetworkReply::finished, this, [=] {
        Reply r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto bytes = reply->readAll();
        r.data = QJsonDocument::fromJson(bytes);
        r.ok = r.status >= 200 && r.status < 300;
        reply->deleteLater();
        if (epoch != generation)
            return;
        bool network = r.status != 0;
        if (network != reachable) {
            reachable = network;
            emit connectionChanged(network);
        }
        if (r.ok && method == "GET" && cacheRead && !r.data.isNull())
            cache->put(key, r.data);
        if (!r.ok) {
            r.error = errorMessage(bytes, r.status);
            if (!network && method == "GET" && cacheRead) {
                auto saved = cache->get(key);
                if (saved.ok)
                    r = saved;
            }
        }
        if (r.status == 401 && !path.startsWith("/auth/")) {
            logout();
            emit sessionExpired();
        }
        if (guard)
            done(r);
    });
}
void ApiClient::session(const QString &token) {
    logout();
    bearer = token;
    auto parts = token.split('.');
    auto data = parts.size() > 1
                    ? QJsonDocument::fromJson(
                          QByteArray::fromBase64(parts[1].toUtf8(), QByteArray::Base64UrlEncoding))
                          .object()
                    : QJsonObject();
    // This decoding partitions the local cache only; server validates every JWT.
    subject = data.value("sub").toString("unknown");
    sessionRole = data.value("role").toString();
    connectStream();
}
void ApiClient::logout() {
    ++generation;
    bearer.clear();
    subject = "public";
    sessionRole.clear();
    cursor = 0;
    retry.stop();
    socket.abort();
}
void ApiClient::connectStream() {
    if (bearer.isEmpty())
        return;
    QUrl url = base.resolved(QUrl("/ws"));
    url.setScheme(base.scheme() == "https" ? "wss" : "ws");
    QUrlQuery q;
    q.addQueryItem("token", bearer);
    q.addQueryItem("after", QString::number(cursor));
    url.setQuery(q);
    socket.open(url);
}
