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
    bool ok = false, cached = false;
    int status = 0;
    QJsonDocument data;
    QString error, cachedAt;
};
class CacheStore {
  public:
    explicit CacheStore(const QString &path);
    ~CacheStore();
    bool available() const;
    void put(const QString &key, const QJsonDocument &data);
    Reply get(const QString &key) const;
    void setPreference(const QString &key, const QString &value);
    QString preference(const QString &key, const QString &fallback = {}) const;

  private:
    QString connection;
    QSqlDatabase db;
};
class ApiClient : public QObject {
    Q_OBJECT
  public:
    using Callback = std::function<void(const Reply &)>;
    ApiClient(QUrl base, CacheStore *cache, QObject *parent = nullptr);
    ~ApiClient() override;
    virtual void request(const QString &method, const QString &path, const QJsonObject &body,
                         QObject *context, Callback done, bool cacheRead = true);
    void get(const QString &path, QObject *context, Callback done) {
        request("GET", path, {}, context, std::move(done));
    }
    Reply cached(const QString &path) const;
    virtual void session(const QString &token);
    void logout();
    virtual bool authenticated() const {
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
    void connectionChanged(bool online);
    void eventReceived(QJsonObject event);
    void sessionExpired();
    void streamChanged(bool connected);

  private:
    void connectStream();
    QUrl base;
    CacheStore *cache;
    QNetworkAccessManager manager;
    QWebSocket socket;
    QTimer retry;
    QString bearer, subject = "public", sessionRole;
    bool reachable = true;
    int generation = 0, backoff = 1000;
    qint64 cursor = 0;
};
