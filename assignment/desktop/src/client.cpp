#include "client.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QNetworkReply>
#include <QPointer>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrlQuery>
#include <QUuid>
#include <QVariant>

CacheStore::CacheStore(const QString &path) : connection(QUuid::createUuid().toString()) {
    // 先创建父目录，再用具名连接打开 SQLite；表不存在时就地初始化。
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
    // removeDatabase 前必须先释放当前 QSqlDatabase 句柄，否则 Qt 会报告连接仍在使用。
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection);
}
bool CacheStore::available() const {
    return db.isOpen();
}
void CacheStore::put(const QString &key, const QJsonDocument &data) {
    // INSERT OR REPLACE 使同一个 API/用户组合始终只保留最新快照。
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
    // 命中缓存时同时返回保存时间，界面可明确提示这是离线数据。
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
    // 偏好设置同样采用 upsert，避免先查询再决定插入或更新。
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
    // 单次定时器避免多个重连周期重叠；每次断线后再显式安排下一次。
    retry.setSingleShot(true);
    // 信号 timeout 连接到成员槽 connectStream：到期时重新建立 WebSocket。
    connect(&retry, &QTimer::timeout, this, &ApiClient::connectStream);
    // connected 信号表示实时通道握手成功：重置指数退避并更新界面状态。
    connect(&socket, &QWebSocket::connected, this, [this] {
        backoff = 1000;
        emit streamChanged(true);
    });
    // disconnected 信号统一处理鉴权失效与普通网络断线。
    connect(&socket, &QWebSocket::disconnected, this, [this] {
        emit streamChanged(false);
        if (int(socket.closeCode()) == 4401 && !bearer.isEmpty()) {
            logout();
            emit sessionExpired();
            return;
        }
        if (!bearer.isEmpty()) {
            // 普通断线按 1、2、4…秒退避，最多等待 30 秒，避免持续轰击服务器。
            retry.start(backoff);
            backoff = qMin(30000, backoff * 2);
        }
    });
    // 每条文本帧是一条 JSON 业务事件；游标过滤重复或乱序到达的旧事件。
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
    // 兼容业务 error、FastAPI 字符串 detail 和参数校验 detail 数组三种响应格式。
    const auto o = QJsonDocument::fromJson(body).object();
    if (o.value("error").isObject())
        return o.value("error").toObject().value("message").toString();
    if (o.value("detail").isString())
        return o.value("detail").toString();
    if (o.value("detail").isArray()) {
        QStringList errors;
        for (const auto &v : o.value("detail").toArray()) {
            auto e = v.toObject();
            errors << e.value("msg").toString();
        }
        return errors.join("；");
    }
    return status ? QString("请求未完成（HTTP %1）").arg(status)
                  : QString("无法连接服务，请检查网络后重试");
}
void ApiClient::request(const QString &method, const QString &path, const QJsonObject &body,
                        QObject *context, Callback done, bool cacheRead) {
    // 离线时禁止可能修改服务器状态的操作；登录接口例外，允许它尝试恢复连通性。
    if (method != "GET" && !reachable && !path.startsWith("/auth/")) {
        Reply r;
        r.error = "当前离线，只能查看缓存；请刷新连接后操作";
        done(r);
        return;
    }
    // resolved 可在保留 base 主机/端口的同时安全拼接 API 相对路径。
    QNetworkRequest req(base.resolved(QUrl(path)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(15000);
    if (!bearer.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + bearer).toUtf8());
    auto reply = manager.sendCustomRequest(
        req, method.toUtf8(),
        method == "GET" || method == "DELETE" ? QByteArray()
                                              : QJsonDocument(body).toJson(QJsonDocument::Compact));
    // 缓存键包含服务地址、用户、角色和路径，防止跨环境或跨账号看到错误快照。
    const QString key = base.toString() + "|" + subject + "|" + sessionRole + "|" + path;
    const int epoch = generation;
    // QPointer 在窗口/页面销毁后自动变空，防止异步回调访问悬空界面对象。
    QPointer<QObject> guard(context);
    // finished 是网络请求的完成信号；lambda 读取响应、更新缓存并回调界面。
    connect(reply, &QNetworkReply::finished, this, [=] {
        Reply r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto bytes = reply->readAll();
        r.data = QJsonDocument::fromJson(bytes);
        r.ok = r.status >= 200 && r.status < 300;
        reply->deleteLater();
        // 登录/退出会增加 generation，旧世代响应不再污染新会话界面。
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
    // 切换会话前停止旧连接；随后只解析 JWT 的公开载荷来划分缓存。
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
    // 清空所有会话态并立即中止实时连接；不会把 JWT 写入磁盘。
    ++generation;
    bearer.clear();
    subject = "public";
    sessionRole.clear();
    cursor = 0;
    retry.stop();
    socket.abort();
}
void ApiClient::connectStream() {
    // HTTP(S) 与 WS(S) 协议一一对应，并携带 token 与事件游标。
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
