#include "user_window.h"
#include "station_map.h"
#include "station_recommendations.h"
#include "visuals.h"
#include "charger_picker.h"
#include "avatar_editor.h"
#include <QJsonArray>
#include <QBuffer>
#include <cmath>
namespace {
QString big = "font-size:30px;font-weight:600;";
QString muted = "color:#b3a0c2;font-size:12px;";
} // namespace
UserWindow::UserWindow(ApiClient *a, CacheStore *c, ApiClient *historicalClient)
    : DesktopWindow(a, c, "ELECTRA · Qt 充电用户端") {
    resize(460, 900);
    setMinimumSize(360, 640);
    root->setProperty("mobile", true);
    centralWidget()->layout()->setContentsMargins(8, 8, 8, 8);
    outer->setContentsMargins(18, 20, 18, 12);
    auto clock = label(QTime::currentTime().toString("HH:mm"), "font-size:12px;font-weight:600;");
    nav->addWidget(clock);
    nav->addStretch();
    nav->addWidget(label("▂▄▆  ▰", "font-size:12px;"));
    footerBar = new QWidget;
    auto footer = new QHBoxLayout(footerBar);
    footer->setContentsMargins(0, 0, 0, 0);
    for (auto pair : QList<QPair<QString, QString>>{{"home", "首页"},
                                                    {"map", "找桩"},
                                                    {"charging", "充电"},
                                                    {"stats", "统计"},
                                                    {"profile", "我的"}}) {
        auto b = new NavButton(pair.second,
                               QMap<QString, QString>{{"home", "grid"},
                                                      {"map", "pin"},
                                                      {"charging", "bolt"},
                                                      {"stats", "chart"},
                                                      {"profile", "user"}}
                                   .value(pair.first),
                               true);
        connect(b, &QPushButton::clicked, this, [=] { navigate(pair.first); });
        b->setCheckable(true);
        b->setObjectName(pair.first);
        footer->addWidget(b);
    }
    outer->insertWidget(2, footerBar);
    Q_UNUSED(historicalClient);
    historyApi = nullptr;
    current = "home";
    connect(api, &ApiClient::sessionExpired, this, [this] {
        if (refreshContext) delete refreshContext;
        events.stop();
        me = {};
        active = {};
        orders = {};
        ledger = {};
        withdrawals = {};
        navigate("profile");
        connection->setText("会话已过期，请重新登录");
    });
    connect(api, &ApiClient::streamChanged, this, [this](bool ready) {
        if (ready)
            events.start();
    });
    events.setSingleShot(true);
    events.setInterval(650);
    connect(&events, &QTimer::timeout, this, &UserWindow::refresh);
    connect(api, &ApiClient::eventReceived, this, [this](const QJsonObject &) { if (!events.isActive()) events.start(); });
    poll.setInterval(1000);
    connect(&poll, &QTimer::timeout, this, [this, clock] {
        clock->setText(QTime::currentTime().toString("HH:mm"));
        updateCharging();
        if (api->authenticated() && !active.isEmpty() &&
            (text(active, "status") == "charging" || text(active, "status") == "reserved" || text(active, "status") == "pending_payment"))
            api->get("/orders/" + text(active, "id"), this, [this](const Reply &r) {
                if (!r.ok) {
                    message(r);
                    return;
                }
                if (text(r.data.object(), "id") != text(active, "id")) return;
                auto previous = text(active, "status");
                active = r.data.object();
                updateCharging();
                if (text(active, "status") != previous) {
                    if (current == "charging") navigate("charging");
                    if (text(active, "stop_reason") == "balance_exhausted")
                        QMessageBox::information(this, "余额不足", "可用余额已用尽，充电已自动停止，请确认支付账单。");
                }
            });
    });
    poll.start();
    auto snapshots = new QTimer(this);
    connect(snapshots, &QTimer::timeout, this, [this] { if (!refreshContext) refresh(); });
    snapshots->start(5000);
    QTimer::singleShot(0, this, &UserWindow::refresh);
}
void UserWindow::refresh() {
    if (refreshContext)
        refreshContext->deleteLater();
    auto ctx = new QObject(this);
    const auto previousStatus = text(active, "status");
    const auto previousStations = stations;
    const auto previousMe = me;
    refreshContext = ctx;
    QSettings settings;const auto prefix="map/"+api->baseUrl().toString();
    QString stationPath="/public/stations";
    if(settings.contains(prefix+"/lat"))stationPath+=QString("?latitude=%1&longitude=%2")
        .arg(settings.value(prefix+"/lat").toDouble(),0,'f',6).arg(settings.value(prefix+"/lon").toDouble(),0,'f',6);
    QStringList paths = {stationPath};
    if (api->authenticated())
        paths << "/me" << "/me/orders?limit=200" << "/me/wallet-entries?limit=200" << "/me/withdrawals";
    auto pending = std::make_shared<int>(paths.size());
    auto failures = std::make_shared<QStringList>();
    auto cachedAt = std::make_shared<QString>();
    for (int i = 0; i < paths.size(); ++i)
        api->get(paths[i], ctx, [=](const Reply &r) {
            message(r);
            if (!r.ok) failures->append(r.error);
            if (r.cached) *cachedAt=r.cachedAt;
            if (r.ok) {
                if (i == 0)
                    stations = r.data.array();
                if (i == 1)
                    me = r.data.object();
                if (i == 2) {
                    orders = r.data.array();
                    auto last = active;
                    active = {};
                    for (const auto &v : orders) {
                        auto o = v.toObject();
                        if (text(o, "status") == "reserved" || text(o, "status") == "charging" || text(o, "status") == "pending_payment") {
                            active = o;
                            break;
                        }
                    }
                    if (active.isEmpty() && !last.isEmpty())
                        for (const auto &v : orders)
                            if (text(v.toObject(), "id") == text(last, "id")) active = v.toObject();
                }
                if (i == 3)
                    ledger = r.data.array();
                if (i == 4)
                    withdrawals = r.data.array();
            }
            if (--*pending == 0) {
                if (selectedStation.isEmpty() && !stations.isEmpty()) {
                    selectedStation = text(stations[0].toObject(), "id");
                    for (const auto &station : stations) {
                        auto o = station.toObject();
                        if (number(o, "longitude") > 113.7 && number(o, "longitude") < 114.7 &&
                            number(o, "latitude") > 22.4 && number(o, "latitude") < 22.95) {
                            selectedStation = text(o, "id");
                            break;
                        }
                    }
                }
                // Event refresh updates data without tearing down charts/maps or scroll position.
                auto stationTerms=[this](const QJsonArray &items) {
                    for(auto item:items) {
                        auto station=item.toObject();
                        if(text(station,"id")==selectedStation)
                            return QJsonObject{{"id",station["id"]},{"name",station["name"]},
                                {"unit_price",station["unit_price"]},{"tariff",station["tariff"]}};
                    }
                    return QJsonObject{};
                };
                if (body->count() <= 1 || current == "profile" || current == "wallet" || current == "withdrawals" || current == "history" ||
                    ((current == "home" || current == "map") && stations != previousStations) ||
                    (current == "station" && stationTerms(stations)!=stationTerms(previousStations)) ||
                    (current == "charging" && text(active, "status") != previousStatus))
                    navigate(current);
                else {
                    updateCharging();
                    if (current == "stats") loadStatistics();
                    if (current == "schedule" && stations != previousStations) navigate(current);
                    if (current == "station" && me != previousMe) navigate(current);
                }
                refreshContext = nullptr;
                if (!failures->isEmpty()) connection->setText("部分数据未更新："+failures->join("；"));
                else if (!cachedAt->isEmpty()) connection->setText("离线只读缓存 · "+*cachedAt);
                ctx->deleteLater();
            }
        });
}
void UserWindow::navigate(const QString &page) {
    if (page == "station" && text(active, "status") == "pending_payment") {
        QMessageBox dialog(QMessageBox::Warning, "请先结算",
                           "您有未完成的充电订单，请先结算", QMessageBox::NoButton, this);
        dialog.addButton("去结算", QMessageBox::AcceptRole);
        dialog.exec();
        navigate("charging");
        return;
    }
    if (page == "schedule" && current != "schedule") scheduleOrigin = current;
    if (page == "charging" && current != "charging")
        chargingOrigin = current == "history" ? "history" : "home";
    auto pageScroll = root->findChild<QScrollArea *>("page-scroll");
    const bool samePage = current == page;
    const int previousScroll = pageScroll ? pageScroll->verticalScrollBar()->value() : 0;
    current = page;
    footerBar->setVisible(true);
    for (auto b : root->findChildren<QPushButton *>())
        if (b->isCheckable())
            b->setChecked(b->objectName() == page);
    statisticsLoader = {}; statisticsHost = nullptr;
    clearLayout(body);
    vehicleTitle = nullptr;
    chargeEnergy = nullptr; chargeCost = nullptr; chargeState = nullptr;
    chargeTime = nullptr; vehicleState = nullptr; batteryArt = nullptr;
    if (page == "map")
        mapPage();
    else if (page == "station")
        stationPage();
    else if (page == "charging")
        charging();
    else if (page == "stats")
        statistics();
    else if (page == "schedule")
        schedule();
    else if (page == "history")
        history();
    else if (page == "profile")
        profile();
    else if (page == "avatar")
        avatarPage();
    else if (page == "wallet")
        wallet();
    else if (page == "withdrawals")
        withdrawalsPage();
    else
        home();
    if (page != "map") body->addStretch();
    if (pageScroll) {
        const bool fixedPage = page == "charging" || page == "map";
        pageScroll->setVerticalScrollBarPolicy(fixedPage ? Qt::ScrollBarAlwaysOff
                                                         : Qt::ScrollBarAsNeeded);
        pageScroll->verticalScrollBar()->setEnabled(!fixedPage);
        if (samePage)
            QTimer::singleShot(0,pageScroll,[pageScroll,previousScroll]{
                pageScroll->verticalScrollBar()->setValue(previousScroll);
            });
        else
            pageScroll->verticalScrollBar()->setValue(0);
    }
}
double UserWindow::batterySoc() const {
    const auto s=text(active,"status");
    if (!active.isEmpty() && (s=="reserved" || s=="charging" || s=="pending_payment") && number(active,"battery_kwh")>0)
        return qBound(0.,number(active,"initial_soc")+number(active,"energy_kwh")/number(active,"battery_kwh")*100.,100.);
    return me.contains("vehicle_soc") ? number(me,"vehicle_soc") : 40.;
}
void UserWindow::updateCharging() {
    const auto s=text(active,"status");
    if (vehicleTitle) vehicleTitle->setText(text(me,"vehicle_name", "").isEmpty() ? "未绑定车辆" : text(me,"vehicle_name"));
    if (batteryArt) {
        batteryArt->setValue(me.contains("vehicle_soc") || !active.isEmpty() ? batterySoc() : -1, "车辆电量（订单模拟）");
        batteryArt->setValues({me.contains("battery_kwh") ? number(me,"battery_kwh") : -1,
                              me.contains("charge_limit") ? number(me,"charge_limit") : -1});
    }
    if (chargeEnergy) chargeEnergy->setText(money(number(active, "energy_kwh")) + " kWh");
    if (chargeCost) chargeCost->setText("¥" + money(number(active, "amount")));
    if (chargeState) chargeState->setText(statusText(s));
    if (vehicleState) vehicleState->setText(active.isEmpty() ? "车辆待充电" : statusText(s) +
        " · " + text(active, "station_name") + " · ¥" + money(number(active, "amount")));
    if (chargeTime) {
        if (s == "reserved") {
            auto until = QDateTime::fromString(text(active, "reserved_until"), Qt::ISODateWithMs);
            auto seconds = qMax<qint64>(0, QDateTime::currentDateTimeUtc().secsTo(until));
            chargeTime->setText(QString("%1分%2秒").arg(seconds / 60).arg(seconds % 60));
        } else if (s == "charging" && number(active, "target_energy_kwh") > 0) {
            double remaining = qMax(0., number(active, "target_energy_kwh") - number(active, "energy_kwh"));
            double seconds = remaining * 3600 / qMax(1., number(active, "power_kw")) /
                qMax(1., number(active, "time_scale"));
            chargeTime->setText(QString("%1分%2秒")
                .arg(int(seconds) / 60).arg(int(seconds) % 60));
        } else if (s == "completed" || s == "paid") chargeTime->setText("已完成");
        else if (s == "pending_payment" || s == "cancelled" || s == "canceled" || s == "stopped")
            chargeTime->setText("已停止");
        else chargeTime->setText("—");
    }
}
