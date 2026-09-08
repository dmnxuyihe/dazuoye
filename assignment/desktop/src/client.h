#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSqlDatabase>
#include <QTimer>
#include <QWebSocket>
#include <functional>

struct Reply {
    // 对一次 HTTP/缓存读取结果的统一封装。界面层只需判断 ok，不必直接依赖 QNetworkReply。
    bool ok = false, cached = false;
    // status 为 HTTP 状态码；网络完全不可达时保持 0。
    int status = 0;
    QJsonDocument data;
    QString error, cachedAt;
};
class CacheStore {
    // 使用 Qt SQL 驱动管理一个轻量 SQLite 缓存。它只缓存只读快照和本机偏好，
    // 不承担预约、扣费等权威业务，真正的数据约束始终由服务器负责。
  public:
    explicit CacheStore(const QString &path);
    ~CacheStore();
    bool available() const;
    void put(const QString &key, const QJsonDocument &data);
    Reply get(const QString &key) const;
    void setPreference(const QString &key, const QString &value);
    QString preference(const QString &key, const QString &fallback = {}) const;

  private:
    // Qt 要求每个数据库连接有唯一名称，避免管理员端/用户端实例相互覆盖。
    QString connection;
    QSqlDatabase db;
};
class ApiClient : public QObject {
    Q_OBJECT
  public:
    // 所有请求均异步完成；Callback 在 context 仍存活时才会被调用。
    using Callback = std::function<void(const Reply &)>;
    ApiClient(QUrl base, CacheStore *cache, QObject *parent = nullptr);
    ~ApiClient() override;
    void request(const QString &method, const QString &path, const QJsonObject &body,
                 QObject *context, Callback done, bool cacheRead = true);
    void get(const QString &path, QObject *context, Callback done) {
        // GET 的便捷包装，默认允许网络失败时回退至 SQLite 快照。
        request("GET", path, {}, context, std::move(done));
    }
    void session(const QString &token);
    void logout();
    bool authenticated() const {
        return !bearer.isEmpty();
    }
    bool online() const {
        return reachable;
    }
    QUrl baseUrl() const {
        return base;
    }
    QString identity() const {
        return subject;
    }
    QString role() const {
        return sessionRole;
    }
    static QString errorMessage(const QByteArray &body, int status);
  signals:
    // REST 连通状态变化，用于顶栏“在线/离线”提示。
    void connectionChanged(bool online);
    // WebSocket 收到一条未处理过的业务事件。
    void eventReceived(QJsonObject event);
    // REST 返回 401 或 WebSocket 以 4401 关闭时通知界面重新登录。
    void sessionExpired();
    // WebSocket 本身的连接状态，和普通 HTTP 是否可达是两个概念。
    void streamChanged(bool connected);

  private:
    // 使用当前 JWT 建立 /ws 连接；断线后由 retry 定时器再次调用。
    void connectStream();
    QUrl base;
    CacheStore *cache;
    QNetworkAccessManager manager;
    QWebSocket socket;
    QTimer retry;
    // bearer 只保存在内存；subject/role 仅用于隔离不同用户的本地缓存键。
    QString bearer, subject = "public", sessionRole;
    // generation 是会话世代号，可让旧会话尚未完成的异步响应自动失效。
    bool reachable = true;
    int generation = 0, backoff = 1000;
    // 最近处理的事件 ID，重连时通过 after 参数避免重复消费。
    qint64 cursor = 0;
};
