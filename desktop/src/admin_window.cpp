#include "admin_window.h"
#include "station_map.h"
#include "visuals.h"
#include <QJsonArray>
#include <QSaveFile>
#include <QtCharts>

namespace {
const QStringList colors = {"#ce7bda", "#efb95d", "#9161cf", "#7ad8c8"};
QChart *baseChart(const QString &title) {
    auto c = new QChart;
    c->setTitle(title);
    c->setTitleBrush(QColor("#bba4c9"));
    c->setBackgroundBrush(Qt::transparent);
    c->setBackgroundRoundness(0);
    c->setMargins(QMargins(0, 0, 0, 0));
    c->legend()->setLabelColor(QColor("#c6afd2"));
    c->legend()->setAlignment(Qt::AlignBottom);
    return c;
}
QChartView *view(QChart *c, int height = 220) {
    auto w = new QChartView(c);
    w->setRenderHint(QPainter::Antialiasing);
    w->setStyleSheet("background:transparent;border:0;");
    w->setMinimumHeight(height);
    w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return w;
}
QWidget *donut(const QMap<QString, double> &values, const QString &title) {
    QVBoxLayout *layout;
    auto panel = card(title, &layout);
    auto c = baseChart("");
    c->legend()->hide();
    auto slices = new QPieSeries;
    slices->setHoleSize(.76);
    slices->setPieSize(.9);
    double total = 0;
    for (double v : values)
        total += v;
    auto legend = new QWidget;
    auto key = new QVBoxLayout(legend);
    key->setContentsMargins(0, 0, 0, 0);
    key->setSpacing(10);
    int i = 0;
    for (auto it = values.begin(); it != values.end(); ++it) {
        auto color = QColor(colors[i++ % colors.size()]);
        auto slice = slices->append(it.key(), it.value());
        slice->setBrush(color);
        slice->setPen(Qt::NoPen);
        QObject::connect(slice, &QPieSlice::hovered, panel, [slice](bool hover) {
            slice->setExploded(hover);
            slice->setExplodeDistanceFactor(.035);
        });
        auto r = new QHBoxLayout;
        r->setSpacing(6);
        r->addWidget(label("●", QString("color:%1;").arg(color.name())));
        auto textcol = new QVBoxLayout;
        textcol->setSpacing(3);
        textcol->addWidget(label(it.key(), "font-size:11px;"));
        textcol->addWidget(
            label(QString::number(it.value()) + " 条", "font-size:9px;color:#947ca7;"));
        r->addLayout(textcol, 1);
        r->addWidget(label(QString::number(total ? it.value() / total * 100 : 0, 'f', 0) + "%",
                           "font-size:12px;font-weight:600;"));
        key->addLayout(r);
    }
    if (!total) {
        auto empty = slices->append("暂无记录", 1);
        empty->setBrush(QColor("#352242"));
        empty->setPen(Qt::NoPen);
        key->addWidget(label("暂无记录", "color:#a78eb8;"));
    }
    c->addSeries(slices);
    auto chart = view(c, 205);
    auto ring = new QWidget;
    auto grid = new QGridLayout(ring);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->addWidget(chart, 0, 0);
    auto value = label(QString::number(total) + "\n总记录", "font-size:18px;font-weight:600;");
    value->setAlignment(Qt::AlignCenter);
    value->setAttribute(Qt::WA_TransparentForMouseEvents);
    grid->addWidget(value, 0, 0, Qt::AlignCenter);
    layout->addWidget(row({ring, legend}, {3, 2}));
    return panel;
}
QWidget *metricCard(const QString &caption, const QString &value, const QString &hint) {
    QVBoxLayout *l;
    auto w = card(caption, &l);
    l->addWidget(label(value, "font-size:28px;font-weight:600;"));
    l->addWidget(label(hint, "color:#af98bd;font-size:11px;"));
    return w;
}
void exportRecords(QWidget *parent, const QJsonArray &rows) {
    auto path = QFileDialog::getSaveFileName(parent, "导出当前记录", QString(), "CSV (*.csv)");
    if (path.isEmpty() || rows.isEmpty())
        return;
    auto keys = rows[0].toObject().keys();
    auto quote = [](QString s) {
        if (!s.isEmpty() && QString("=+-@\t\r").contains(s[0]))
            s.prepend('\'');
        s.replace('"', "\"\"");
        return '"' + s + '"';
    };
    QByteArray data = "\xEF\xBB\xBF";
    QStringList cells;
    for (const auto &k : keys)
        cells << quote(k);
    data += cells.join(',').toUtf8() + "\n";
    for (const auto &v : rows) {
        cells.clear();
        auto o = v.toObject();
        for (const auto &k : keys)
            cells << quote(text(o, k, ""));
        data += cells.join(',').toUtf8() + "\n";
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
        QMessageBox::warning(parent, "导出失败", "无法写入目标文件");
}
QString actionLabel(QString a) {
    static QMap<QString, QString> map = {
        {"order.reserve", "订单已预约"},       {"order.start", "开始充电"},
        {"order.stop", "充电已停止"},          {"order.cancel", "预约已取消"},
        {"wallet.adjustment", "余额已调整"},   {"wallet.refund", "退款已处理"},
        {"console.settings", "运营设置已更新"}};
    return map.contains(a) ? map.value(a) : a.replace('.', " · ");
}
} // namespace
AdminWindow::AdminWindow(ApiClient *a, CacheStore *c)
    : DesktopWindow(a, c, "ELECTRA · Qt 运营管理中心") {
    resize(1460, 1000);
    setMinimumSize(1060, 720);
    auto brand = label("");
    brand->setPixmap(appIcon("brand").pixmap(32, 32));
    nav->addWidget(brand);
    nav->addWidget(label("ELECTRA", "font-size:27px;font-weight:600;"));
    auto navigation = new QFrame;
    navigation->setObjectName("navigation");
    auto pills = new QHBoxLayout(navigation);
    pills->setContentsMargins(5, 5, 5, 5);
    pills->setSpacing(2);
    nav->addStretch();
    for (auto pair : QList<QPair<QString, QString>>{{"dashboard", "运营总览"},
                                                    {"station", "站点导航"},
                                                    {"trips", "充电订单"},
                                                    {"history", "操作审计"},
                                                    {"forecast", "负荷预测"}}) {
        auto b = new NavButton(pair.second, QMap<QString, QString>{{"dashboard", "grid"},
                                                                   {"station", "pin"},
                                                                   {"trips", "car"},
                                                                   {"history", "history"},
                                                                   {"forecast", "chart"}}
                                                .value(pair.first));
        connect(b, &QPushButton::clicked, this, [=] { navigate(pair.first); });
        b->setCheckable(true);
        b->setObjectName(pair.first);
        pills->addWidget(b);
    }
    nav->addWidget(navigation);
    nav->addStretch();
    auto refreshButton = button("", this, [this] {
        if (api->authenticated())
            refresh();
        else
            login();
    });
    refreshButton->setIcon(appIcon("history"));
    refreshButton->setToolTip("刷新数据");
    refreshButton->setAccessibleName("刷新数据");
    nav->addWidget(refreshButton);
    auto settingsButton = button("", this, [this] { settings(); });
    settingsButton->setIcon(appIcon("settings"));
    settingsButton->setToolTip("运营设置");
    settingsButton->setAccessibleName("运营设置");
    nav->addWidget(settingsButton);
    nav->addWidget(button("退出登录", this, [this] {
        api->logout(); refreshTimer.stop();
        if (refreshContext) delete refreshContext;
        for (auto d : findChildren<QDialog *>()) d->close();
        stations = {}; chargers = {}; users = {}; orders = {}; logs = {};
        summary = {}; preferences = {}; analytics = {};
        navigate("dashboard");
    }));
    for (auto b : {refreshButton, settingsButton}) {
        b->setFixedSize(39, 39);
        b->setStyleSheet("padding:0;min-height:0;background:#1b1028;border:1px solid "
                         "#493251;border-radius:19px;");
    }
    auto tools = new QHBoxLayout;
    for (auto pair : QList<QPair<QString, QString>>{
             {"users", "用户"}, {"stations", "站点"}, {"chargers", "电桩"}})
        tools->addWidget(button(pair.second, this, [=] { manager(pair.first); }));
    tools->addWidget(button("提现审核",this,[this]{withdrawalsPage();}));
    tools->addStretch();
    tools->addWidget(button("Web 大屏 ↗", this, [this] { openUrl("/ui/dashboard.html"); }));
    for (int i = 0; i < tools->count(); ++i)
        if (auto w = tools->itemAt(i)->widget())
            w->setProperty("quiet", true);
    outer->insertLayout(2, tools);
    current = "dashboard";
    body->addWidget(label("正在连接管理员服务…"));
    connect(api, &ApiClient::streamChanged, this, [this](bool ready) {
        if (ready)
            refreshTimer.start();
    });
    refreshTimer.setSingleShot(true);
    refreshTimer.setInterval(700);
    connect(&refreshTimer, &QTimer::timeout, this, &AdminWindow::refresh);
    connect(api, &ApiClient::eventReceived, this,
            [this](const QJsonObject &) { if (!refreshTimer.isActive()) refreshTimer.start(); });
    connect(api, &ApiClient::sessionExpired, this, [this] {
        if (refreshContext) delete refreshContext;
        stations = {}; chargers = {}; users = {}; orders = {}; logs = {};
        summary = {}; preferences = {}; analytics = {};
        for (auto d : findChildren<QDialog *>()) d->close();
        navigate(current);
        connection->setText("会话已过期，请重新登录");
        manualLogin();
    });
    auto poll = new QTimer(this);
    connect(poll, &QTimer::timeout, this, [this] {
        if (api->authenticated() && !refreshContext) refresh();
    });
    poll->start(5000);
    QTimer::singleShot(0, this, &AdminWindow::login);
}
void AdminWindow::login() {
    api->request("POST", "/auth/console", {}, this, [this](const Reply &r) {
        message(r);
        if (r.ok) {
            api->session(text(r.data.object(), "access_token"));
            refresh();
        } else {
            clearLayout(body);
            body->addWidget(label("管理员自动登录未启用或连接不可用"));
            body->addWidget(button("使用管理员账号登录", this, [this] { manualLogin(); }, true));
            body->addStretch();
        }
    });
}
void AdminWindow::manualLogin() {
    auto d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle("管理员登录");
    auto l = new QVBoxLayout(d);
    l->addWidget(dialogHeader(d, label("管理员登录", "font-size:22px;")));
    auto user = new QLineEdit;
    user->setPlaceholderText("管理员账号");
    auto pass = new QLineEdit;
    pass->setPlaceholderText("密码");
    pass->setEchoMode(QLineEdit::Password);
    l->addWidget(user);
    l->addWidget(pass);
    auto err = label("");
    l->addWidget(err);
    l->addWidget(button(
        "登录", d,
        [=] {
            api->request("POST", "/auth/admin/login",
                         {{"username", user->text()}, {"password", pass->text()}}, d,
                         [=](const Reply &r) {
                             if (!r.ok) {
                                 err->setText(r.error);
                                 return;
                             }
                             api->session(text(r.data.object(), "access_token"));
                             d->accept();
                             refresh();
                         });
        },
        true));
    d->show();
}
void AdminWindow::refresh() {
    if (!api->authenticated())
        return;
    if (refreshContext) return;
    const auto previousStations=stations, previousChargers=chargers, previousOrders=orders, previousLogs=logs;
    auto ctx = new QObject(this);
    refreshContext = ctx;
    auto remaining = std::make_shared<int>(8);
    auto chargerSnapshotKnown = std::make_shared<bool>(false);
    auto failures = std::make_shared<QStringList>();
    QStringList paths = {"/admin/stations",
                         "/admin/chargers",
                         "/admin/users?limit=200",
                         QString("/admin/orders?limit=200&offset=%1").arg(offsetOrders),
                         QString("/admin/ops-logs?limit=200&offset=%1").arg(offsetLogs),
                         "/admin/stats/summary",
                         "/admin/console/settings",
                         "/admin/console/analytics"};
    for (int i = 0; i < paths.size(); ++i)
        api->get(paths[i], ctx, [=](const Reply &r) {
            if (r.ok) {
                if (i == 0)
                    stations = r.data.array();
                if (i == 1) {
                    chargers = r.data.array();
                    *chargerSnapshotKnown = true;
                }
                if (i == 2)
                    users = r.data.array();
                if (i == 3)
                    orders = r.data.array();
                if (i == 4)
                    logs = r.data.array();
                if (i == 5)
                    summary = r.data.object();
                if (i == 6)
                    preferences = r.data.object();
                if (i == 7)
                    analytics = r.data.object();
            } else
                failures->append(r.error);
            message(r);
            if (--*remaining == 0) {
                // The admin station response has no available_count; derive it from the charger
                // snapshot.
                QMap<QString, int> available;
                if (*chargerSnapshotKnown)
                    for (const auto &v : chargers) {
                        auto o = v.toObject();
                        if (text(o, "status") == "available")
                            available[text(o, "station_id")]++;
                    }
                for (int index = 0; index < stations.size(); ++index) {
                    auto o = stations[index].toObject();
                    if (*chargerSnapshotKnown)
                        o["available_count"] = available.value(text(o, "id"));
                    else
                        o.remove("available_count");
                    stations[index] = o;
                }

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
                if (current == "dashboard" && dashboardLoader) dashboardLoader();
                else if ((current == "station" && (stations != previousStations || chargers != previousChargers)) ||
                         (current == "trips" && (orders != previousOrders || chargers != previousChargers)) ||
                         (current == "history" && logs != previousLogs) || body->count() <= 1) {
                    auto scroll = findChild<QScrollArea *>("page-scroll");
                    const int position = scroll->verticalScrollBar()->value();
                    navigate(current);
                    QTimer::singleShot(0, this, [scroll,position] { scroll->verticalScrollBar()->setValue(position); });
                }
                refreshContext = nullptr;
                if (!failures->isEmpty())
                    connection->setText("部分数据未更新：" + failures->join("；"));
                ctx->deleteLater();
            }
        });
}
void AdminWindow::navigate(const QString &page) {
    current = page;
    for (auto b : root->findChildren<QPushButton *>())
        if (b->isCheckable())
            b->setChecked(b->objectName() == page);
    dashboardLoader = {};
    clearLayout(body);
    if (!api->authenticated()) {
        body->addWidget(button("管理员登录", this, [this] { manualLogin(); }, true));
        return;
    }
    if (page == "station")
        stationsPage();
    else if (page == "trips")
        ordersPage();
    else if (page == "history")
        auditPage();
    else if (page == "forecast")
        forecastPage();
    else
        dashboard();
    body->addStretch();
}
void AdminWindow::dashboard() {
    QVBoxLayout *vehicleLayout, *goalLayout, *m;
    auto vehicle = card("运营站点", &vehicleLayout);
    auto car = new ArtWidget(ArtWidget::Car); car->setObjectName("fleet-occupancy");
    car->setFixedHeight(205); vehicleLayout->addWidget(car,1);
    auto fleetText = label(""); fleetText->setObjectName("fleet-summary");
    fleetText->setAlignment(Qt::AlignCenter); vehicleLayout->addWidget(fleetText);
    vehicleLayout->addWidget(label("已预约与充电设备 / 全部设备 · 插画表示设备占用率", "font-size:10px;color:#aa91bb;"));
    auto goal = card("设备运行状态", &goalLayout);
    auto gauge = new ArtWidget(ArtWidget::Gauge); gauge->setObjectName("device-gauge");
    gauge->setFixedHeight(205); goalLayout->addWidget(gauge,1);
    auto deviceText = label(""); deviceText->setObjectName("device-summary");
    deviceText->setAlignment(Qt::AlignCenter); goalLayout->addWidget(deviceText);
    goalLayout->addWidget(label("外弧：可用率    内弧：充电率 · 全部设备", "font-size:10px;color:#aa91bb;"));
    auto mapCard = card("充电站地图", &m);
    auto map = new StationMap;
    map->setApi(api); map->setCompact(true); map->setFixedHeight(225);
    connect(map, &StationMap::stationSelected, this, [this](QString id) {
        selectedStation = id; navigate("station");
    });
    m->addWidget(map,1);
    auto explore = button("探索充电网络   →", this, [this] { navigate("station"); });
    explore->setProperty("quiet", true); m->addWidget(explore);
    auto top = row({vehicle,goal,mapCard}); top->setObjectName("dashboard-top");
    top->setFixedHeight(340); body->addWidget(top);
    QVBoxLayout *stats, *list;
    auto s = card("充电营收统计", &stats);
    auto period = new QComboBox; period->addItem("近7天",7); period->addItem("近30天",30);
    period->setObjectName("revenue-period"); period->setCurrentIndex(period->findData(revenueDays));
    stats->itemAt(0)->layout()->addWidget(period);
    auto revenueSummary = label(""); revenueSummary->setObjectName("revenue-summary");
    revenueSummary->setStyleSheet("font-size:14px;color:#d8b9e6;"); stats->addWidget(revenueSummary);
    auto chart = new ArtWidget(ArtWidget::Flow); chart->setFixedHeight(245); stats->addWidget(chart,1);
    auto revenueTotal = label(""); stats->addWidget(revenueTotal);
    auto generation = std::make_shared<int>(0);
    auto loadRevenue = [=] {
        int epoch = ++*generation;
        api->get("/admin/stats/revenue?days=" + QString::number(revenueDays), s, [=](const Reply &r) {
            if (epoch != *generation) return;
            if (!r.ok || r.cached) message(r);
            if (!r.ok) { revenueTotal->setText(r.error); return; }
            const auto data=r.data.array(); QList<double> totals(3,0); QStringList dates;
            double total=0; int ordersCount=0;
            for(int bucket=0;bucket<3;++bucket) {
                int from=bucket*data.size()/3, until=(bucket+1)*data.size()/3;
                for(int i=from;i<until;++i) { auto o=data[i].toObject(); totals[bucket]+=number(o,"revenue"); ordersCount+=number(o,"orders"); }
                total+=totals[bucket];
                dates << (from<until ? text(data[from].toObject(),"date").mid(5)+"–"+text(data[until-1].toObject(),"date").mid(5) : "—");
            }
            chart->setValues(totals,dates);
            revenueTotal->setText(QString("区间合计 ¥%1 · %2 笔已支付订单 · 北京时间").arg(money(total)).arg(ordersCount));
        });
    };
    connect(period, &QComboBox::activated, s, [=](int) { revenueDays=period->currentData().toInt(); loadRevenue(); });
    auto nearby = card("站点列表", &list);
    auto headings = new QHBoxLayout;
    headings->setContentsMargins(10, 0, 10, 0);
    headings->addWidget(label("站点位置", "color:#967da7;font-size:10px;"), 3);
    headings->addWidget(label("充电接口", "color:#967da7;font-size:10px;"), 2);
    headings->addWidget(label("营业 / 操作", "color:#967da7;font-size:10px;"), 2);
    list->addLayout(headings);
    auto filter = new QComboBox;
    filter->addItem("全部站点");
    filter->addItem("空闲站点");
    filter->setMaximumWidth(115);
    list->itemAt(0)->layout()->addWidget(filter);
    auto stationHost = new QWidget;
    auto stationRows = new QVBoxLayout(stationHost);
    stationRows->setContentsMargins(0, 0, 0, 0);
    stationRows->setSpacing(9);
    list->addWidget(stationHost);
    auto renderStations = [=](int filterIndex) {
        clearLayout(stationRows);
        int count = 0;
        for (const auto &r : stations) {
            if (count >= 4)
                break;
            auto o = r.toObject();
            if (filterIndex == 1 && number(o, "available_count") <= 0)
                continue;
            auto item = new QFrame;
            item->setObjectName("station-row");
            if (count % 2)
                item->setStyleSheet(
                    "QFrame#station-row{background:transparent;border-color:transparent;}");
            ++count;
            auto line = new QHBoxLayout(item);
            line->setContentsMargins(9, 8, 9, 8);
            line->setSpacing(9);
            auto thumbnail = label("");
            QPixmap art(38, 38);
            art.fill(Qt::transparent);
            {
                QPainter p(&art);
                p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
                QPainterPath clip;
                clip.addEllipse(1, 1, 36, 36);
                p.setClipPath(clip);
                p.fillRect(art.rect(), QColor("#42394f"));
                QPixmap car(":/assets/ev-photo.png");
                p.drawPixmap(QRectF(-4, -8, 53, 53), car, car.rect());
            }
            thumbnail->setPixmap(art);
            thumbnail->setFixedSize(38, 38);
            line->addWidget(thumbnail);
            auto info = new QVBoxLayout;
            info->setSpacing(4);
            auto name = label(text(o, "name"), "font-size:12px;font-weight:600;");
            info->addWidget(name);
            info->addWidget(label(text(o, "address"), "font-size:9px;color:#a58daf;"));
            line->addLayout(info, 3);
            auto connectors = new QVBoxLayout;
            connectors->setSpacing(5);
            connectors->addWidget(label(QString("● %1 空闲接口").arg(text(o, "available_count")),
                                        "font-size:11px;color:#d9afe8;"));
            connectors->addWidget(label(QString("¥%1 / kWh").arg(money(number(o, "unit_price"))),
                                        "font-size:10px;color:#aa93ba;"));
            line->addLayout(connectors, 2);
            auto last = new QVBoxLayout;
            last->setSpacing(4);
            last->addWidget(label(QString("%1 台充电设备").arg(number(o, "charger_count")),
                                  "font-size:10px;color:#a58daf;"));
            auto go = button("查看站点 ↗", this, [=] {
                selectedStation = text(o, "id");
                navigate("station");
            });
            go->setStyleSheet("QPushButton{padding:4px 9px;min-height:0;border:1px solid "
                              "#9261a5;border-radius:12px;background:transparent;font-size:10px;}"
                              "QPushButton:hover{background:#553166;}");
            last->addWidget(go);
            line->addLayout(last, 2);
            stationRows->addWidget(item);
        }
        if (!count)
            stationRows->addWidget(
                emptyPanel("暂无空闲站点", "可切换全部站点查看设备状态", "plug"));
    };
    connect(filter, &QComboBox::activated, stationHost, renderStations);
    filter->setCurrentIndex(stationFilter);
    connect(filter,&QComboBox::activated,stationHost,[this](int i){stationFilter=i;});
    list->addStretch();
    auto bottom = row({s, nearby}); bottom->setObjectName("dashboard-bottom");
    bottom->setFixedHeight(390); body->addWidget(bottom);
    dashboardLoader = [=] {
        const double total=chargers.size(); int available=0, charging=0, reserved=0;
        for (auto v:chargers) {const auto state=text(v.toObject(),"status"); available+=state=="available";charging+=state=="charging";reserved+=state=="reserved";}
        car->setValue(total ? (charging+reserved)*100./total : -1,"设备占用率");
        gauge->setValue(total ? available*100./total : -1,"设备可用率");
        gauge->setValues({total ? charging*100./total : 0});
        fleetText->setText(QString("%1 个站点       %2 台设备       %3 台占用").arg(stations.size()).arg(int(total)).arg(charging+reserved));
        deviceText->setText(total ? QString("%1% 可用       %2% 充电中").arg(money(available*100./total),money(charging*100./total)) : "暂无设备数据");
        revenueSummary->setText(QString("今日 ¥%1    本月 ¥%2    总营收 ¥%3").arg(money(number(summary,"today_revenue")),money(number(summary,"month_revenue")),money(number(summary,"total_revenue"))));
        map->setStations(stations,selectedStation); renderStations(filter->currentIndex()); loadRevenue();
    };
    dashboardLoader();
}
void AdminWindow::stationsPage() {
    body->addWidget(label("站点导航", "font-size:26px;font-weight:600;"));
    QVBoxLayout *left, *right;
    auto a = card("车辆与站点", &left);
    a->setMaximumWidth(370);
    auto car = new ArtWidget(ArtWidget::Car);
    car->setFixedHeight(210);
    left->addWidget(car);
    for (const auto &v : stations) {
        auto o = v.toObject();
        if (text(o, "id") != selectedStation)
            continue;
        const double total = number(o,"charger_count");
        car->setValue(total ? number(o,"available_count")*100./total : -1,"所选站点可用接口比例");
        left->addWidget(label("可用接口比例", "color:#ac8dbb;font-size:11px;"));
        left->addWidget(label(text(o, "name"), "font-size:22px;font-weight:600;"));
        left->addWidget(label(text(o, "address"), "color:#b9a0c9;"));
        left->addWidget(row({valueBlock(text(o, "available_count"), "个", "空闲接口"),
                             valueBlock(money(number(o, "unit_price")), "元/kWh", "充电单价")}));
    }
    left->addStretch();
    left->addWidget(
        label("拖动平移 · 滚轮缩放\n◎ 当前城市 · 全 全部站点", "color:#ac8dbb;font-size:11px;"));
    auto b = card("站点地图", &right);
    auto map = new StationMap;
    map->setApi(api);
    map->setMinimumHeight(420);
    map->setStations(stations, selectedStation);
    right->addWidget(map);
    connect(map, &StationMap::stationSelected, this, [this](QString id) {
        selectedStation = id;
        navigate("station");
    });
    auto select = new QComboBox;
    for (const auto &v : stations) {
        auto o = v.toObject();
        select->addItem(text(o, "name"), text(o, "id"));
    }
    select->setCurrentIndex(qMax(0, select->findData(selectedStation)));
    connect(select, &QComboBox::activated, this, [=](int i) {
        selectedStation = select->itemData(i).toString();
        navigate("station");
    });
    right->addWidget(select);
    for (const auto &v : stations) {
        auto o = v.toObject();
        if (text(o, "id") != selectedStation)
            continue;
        right->addWidget(label(text(o, "address"), "font-size:16px;"));
        right->addWidget(label(QString("%1 台电桩 · 在线 %2 台 · ¥%3 / kWh")
                                   .arg(number(o, "charger_count"))
                                   .arg(number(o, "online_count"))
                                   .arg(money(number(o, "unit_price"))),
                               "color:#d4a9e0;"));
    }
    right->addWidget(button("维护站点与设备", this, [this] { manager("chargers"); }, true));
    body->addWidget(row({a, b}, {1, 3}));
}
void AdminWindow::ordersPage() {
    double energy = 0, amount = 0;
    QMap<QString, double> counts;
    QMap<QString, double> days;
    for (auto v : orders) {
        auto o = v.toObject();
        energy += number(o, "energy_kwh");
        amount += number(o, "amount");
        counts[statusText(text(o, "status"))]++;
        days[text(o, "reserved_at").left(10)] += number(o, "energy_kwh");
    }
    QJsonObject recent = orders.isEmpty() ? QJsonObject() : orders[0].toObject();
    for (auto v : orders)
        if ((!selectedOrder.isEmpty() && text(v.toObject(),"id")==selectedOrder) || (selectedOrder.isEmpty() && text(v.toObject(), "status") == "charging")) {
            recent = v.toObject();
            break;
        }
    QVBoxLayout *journey;
    auto journeyCard = card("所选订单", &journey);
    auto art = new DataGraphic(DataGraphic::Journey);
    art->setFixedHeight(205);
    art->setObjectName("order-progress");
    art->setJourney(recent);
    journey->addWidget(art);
    if (!recent.isEmpty()) {
        auto detail=button("查看该订单", this, [=] { showDetail(this,"订单详情",recent); });
        detail->setObjectName("selected-order-detail");
        journey->addWidget(detail);
    }
    journey->addWidget(row({valueBlock(money(number(recent, "energy_kwh")), "kWh", "电量"),
                            valueBlock(money(number(recent, "amount")), "元", "结算金额")}));
    QVBoxLayout *trend;
    auto trendCard = card("充电能量趋势", &trend);
    auto graph = new DataGraphic(DataGraphic::Trend);
    QStringList dates;
    for (auto d : days.keys())
        dates << d.mid(5);
    graph->setData({days.values()}, dates, "kWh");
    trend->addWidget(graph);
    trend->addWidget(row({valueBlock(money(energy), "kWh", "当前页电量"),
                          valueBlock(QString::number(orders.size()), "笔", "订单数量")}));
    auto overview=row({journeyCard, donut(counts, "订单状态分布"), trendCard}, {12, 10, 11});
    overview->setFixedHeight(410);
    body->addWidget(overview);
    auto search = new QLineEdit(orderSearch);
    search->setPlaceholderText("搜索站点、手机号、订单编号，回车查询");
    connect(search, &QLineEdit::returnPressed, this, [=] {
        orderSearch = search->text();
        navigate("trips");
    });
    const QStringList states = {"all", "charging", "reserved", "pending_payment", "completed", "cancelled"};
    auto filter = new Segments({"全部", "充电中", "已预约", "待支付", "已完成", "已取消"},
                               qMax(0, states.indexOf(orderFilter)));
    connect(filter, &Segments::changed, this, [=](int i) {
        orderFilter = states[i];
        navigate("trips");
    });
    body->addWidget(
        row({filter, search, button("导出当前页 ↗", this, [this] { exportRecords(this, orders); })},
            {4, 2, 1}));
    QVBoxLayout *list;
    auto box = card("最近充电订单", &list);
    list->itemAt(0)->layout()->addWidget(
        button("＋ 新建订单", this, [this] { manager("orders"); }, true));
    auto tableHost = new QWidget;
    auto records = new QVBoxLayout(tableHost);
    records->setContentsMargins(0, 0, 0, 0);
    records->setSpacing(0);
    auto tableScroll = new QScrollArea;
    tableScroll->setWidgetResizable(true);
    tableScroll->setWidget(tableHost);
    tableScroll->setMinimumHeight(340);
    list->addWidget(tableScroll);
    records->addWidget(
        row({label("站点 / 用户", "color:#9780a8;"), label("状态", "color:#9780a8;"),
             label("电量 / 金额", "color:#9780a8;"), label("订单操作", "color:#9780a8;")},
            {4, 1, 1, 2}));
    int count = 0;
    for (const auto &v : orders) {
        auto o = v.toObject();
        if (orderFilter != "all" && text(o, "status") != orderFilter)
            continue;
        if (!QJsonDocument(o).toJson().toLower().contains(orderSearch.toLower().toUtf8()))
            continue;
        ++count;
        auto info = label(text(o, "station_name") + "   ·   " + text(o, "charger_code") + "\n" +
                              text(o, "phone") + "   " + localDateTime(text(o, "reserved_at")),
                          "font-size:13px;");
        auto status = label(statusText(text(o, "status")), "color:#c992e1;");
        auto cost = label(money(number(o, "energy_kwh")) + " kWh\n¥" + money(number(o, "amount")));
        auto actions = new QWidget;
        auto al = new QHBoxLayout(actions);
        al->addWidget(button("详情", this, [=] {
            selectedOrder=text(o,"id");
            navigate("trips");
            api->get("/admin/orders/" + text(o, "id"), this, [=](const Reply &r) {
                if (r.ok)
                    showDetail(this, "订单详情", r.data.object());
                else
                    message(r);
            });
        }));
        auto state = text(o, "status");
        if(state=="pending_payment") al->addWidget(label("等待用户确认付款","color:#edcd79;"));
        if (state == "reserved" || state == "charging") {
            QString action = state == "reserved" ? "start" : "stop";
            al->addWidget(button(
                state == "reserved" ? "开始" : "停止充电", this,
                [=] {
                    command(this, api, "POST", "/admin/orders/" + text(o, "id") + "/" + action, {},
                            [this](const Reply &) { refresh(); });
                },
                true));
            if (state == "reserved")
                al->addWidget(button("取消", this, [=] {
                    command(this, api, "POST", "/admin/orders/" + text(o, "id") + "/cancel", {},
                            [this](const Reply &) { refresh(); });
                }));
        }
        auto record = row({info, status, cost, actions}, {4, 1, 1, 2});
        record->setObjectName("order-row");
        record->setStyleSheet(
            QString("QWidget#order-row{background:%1;border-bottom:1px solid #382444;}")
                .arg(count % 2 ? "#281a36" : "#1c1228"));
        record->layout()->setContentsMargins(10, 12, 10, 12);
        records->addWidget(record);
    }
    if (!count)
        records->addWidget(emptyPanel("没有匹配的充电订单", "可调整状态筛选或搜索条件", "car"));
    records->addStretch();
    QVBoxLayout *summaryLayout, *equipment;
    auto summaryCard = card("订单概览", &summaryLayout);
    summaryLayout->addWidget(detailLine("car", "当前页订单", QString::number(orders.size()) + " 笔"));
    summaryLayout->addWidget(detailLine("bolt", "当前页电量", money(energy) + " kWh"));
    summaryLayout->addWidget(detailLine("chart", "订单金额", "¥" + money(amount)));
    auto equipmentCard = card("设备状态", &equipment);
    QMap<QString, double> devices;
    for (auto v : chargers)
        devices[statusText(text(v.toObject(), "status"))]++;
    int colorIndex = 0;
    for (auto it = devices.begin(); it != devices.end(); ++it)
        equipment->addWidget(amountBar(it.key(), it.value(), chargers.size(),
                                       QColor(colors[colorIndex++ % colors.size()]), "台"));
    equipment->addWidget(button("管理电桩 ↗", this, [this] { manager("chargers"); }, true));
    auto aside = new QWidget;
    auto asideLayout = new QVBoxLayout(aside);
    asideLayout->setContentsMargins(0, 0, 0, 0);
    asideLayout->addWidget(summaryCard);
    asideLayout->addWidget(equipmentCard);
    asideLayout->addStretch();
    body->addWidget(row({box, aside}, {3, 1}));
    body->addWidget(row({button("上一页", this,
                                [this] {
                                    offsetOrders = qMax(0, offsetOrders - 200);
                                    refresh();
                                }),
                         label(QString("偏移 %1 · %2 条记录").arg(offsetOrders).arg(orders.size())),
                         button("下一页", this, [this] {
                             if (orders.size() == 200) {
                                 offsetOrders += 200;
                                 refresh();
                             }
                         })}));
}
void AdminWindow::auditPage() {
    QMap<QString, double> counts, days;
    QList<QList<double>> activity(7, QList<double>(24, 0));
    QSet<QString> operators;
    int moneyEvents = 0;
    const QMap<QString, QString> types = {{"order", "充电订单"},   {"wallet", "钱包资金"},
                                          {"charger", "电桩设备"}, {"station", "站点"},
                                          {"user", "用户"},        {"console", "系统设置"}};
    for (auto v : logs) {
        auto o = v.toObject();
        QString kind = text(o, "action").section('.', 0, 0);
        counts[types.value(kind, kind)]++;
        days[QDateTime::fromString(text(o,"created_at"),Qt::ISODate).toOffsetFromUtc(8*3600).date().toString(Qt::ISODate)]++;
        operators.insert(text(o, "admin_id"));
        if (kind == "wallet")
            moneyEvents++;
        auto date = QDateTime::fromString(text(o, "created_at"), Qt::ISODateWithMs);
        if (!date.isValid())
            date = QDateTime::fromString(text(o, "created_at"), Qt::ISODate);
        if (date.isValid()) {
            date = date.toOffsetFromUtc(8*3600);
            activity[date.date().dayOfWeek() - 1][date.time().hour()]++;
        }
    }
    body->addWidget(
        row({metricCard("审计事件", QString::number(logs.size()), "当前页事务记录"),
             metricCard("操作类型", QString::number(counts.size()), "按业务对象分类"),
             metricCard("资金操作", QString::number(moneyEvents), "充值 / 调账 / 退款"),
             metricCard("操作账户", QString::number(operators.size()), "不同管理员标识")}));
    QVBoxLayout *timeline;
    auto timelineCard = card("操作时间线", &timeline);
    timeline->itemAt(0)->layout()->addWidget(
        button("导出 CSV ↗", this, [this] { exportRecords(this, logs); }));
    auto filter = new QComboBox;
    filter->addItem("全部事件", "all");
    for (auto it = types.begin(); it != types.end(); ++it)
        filter->addItem(it.value(), it.key());
    auto search = new QLineEdit;
    search->setPlaceholderText("搜索操作、时间、对象…");
    timeline->addWidget(row({filter, search}, {1, 2}));
    auto content = new QWidget;
    auto list = new QVBoxLayout(content);
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(8);
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    scroll->setMinimumHeight(590);
    timeline->addWidget(scroll);
    QVBoxLayout *selectedLayout;
    auto selectedCard = card("事件详情", &selectedLayout);
    auto selectedLabel = label("选择左侧事件查看详情", "font-size:12px;color:#b9a1c8;");
    selectedLayout->addWidget(selectedLabel);
    selectedLayout->addStretch();
    auto render = [=] {
        clearLayout(list);
        QString lastDate;
        int count = 0;
        for (auto v : logs) {
            auto o = v.toObject();
            QString action = text(o, "action");
            if (filter->currentData() != "all" &&
                action.section('.', 0, 0) != filter->currentData().toString())
                continue;
            if (!QString::fromUtf8(QJsonDocument(o).toJson())
                     .contains(search->text(), Qt::CaseInsensitive))
                continue;
            count++;
            const auto localTime=QDateTime::fromString(text(o,"created_at"),Qt::ISODate).toOffsetFromUtc(8*3600);
            QString day = localTime.date().toString(Qt::ISODate);
            if (day != lastDate) {
                list->addWidget(
                    label(day + " · 北京时间", "font-size:12px;color:#a48ab7;padding:9px 0;"));
                lastDate = day;
            }
            auto item = new QFrame;
            item->setObjectName("event-row");
            item->setStyleSheet("QFrame#event-row{background:#23162f;border:1px solid "
                                "#3f294d;border-radius:12px;}");
            auto r = new QHBoxLayout(item);
            r->setContentsMargins(13, 13, 13, 13);
            r->setSpacing(12);
            auto icon = label("");
            icon->setPixmap(
                appIcon(action.startsWith("wallet") ? "chart" : "history", QColor("#da9ee3"))
                    .pixmap(23, 23));
            r->addWidget(icon);
            auto textcol = new QVBoxLayout;
            textcol->setSpacing(5);
            textcol->addWidget(label(actionLabel(action), "font-size:14px;font-weight:600;"));
            textcol->addWidget(
                label(types.value(action.section('.', 0, 0), action.section('.', 0, 0)) + " · " +
                          text(o, "target_id").left(18),
                      "font-size:10px;color:#a88eba;"));
            r->addLayout(textcol, 1);
            r->addWidget(label(localTime.time().toString("HH:mm:ss"), "color:#a88eba;font-size:11px;"));
            r->addWidget(button("查看 ↗", content, [=] {
                selectedLabel->setText(actionLabel(action) + "\n\n时间  " + localDateTime(text(o, "created_at")) +
                                       "\n\n对象  " + text(o, "target_type") + "\n" +
                                       text(o, "target_id"));
                showDetail(this, "审计事件", o);
            }));
            list->addWidget(item);
        }
        if (!count)
            list->addWidget(emptyPanel("暂无匹配事件", "更换事件类型或搜索词"));
        list->addStretch();
    };
    connect(filter, &QComboBox::activated, content, [=](int) { render(); });
    connect(search, &QLineEdit::textChanged, content, [=](const QString &) { render(); });
    render();
    auto side = new QWidget;
    auto sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->addWidget(donut(counts, "操作类型分布"));
    QVBoxLayout *heat;
    auto heatCard = card("操作活跃度 · 北京时间", &heat);
    auto grid = new DataGraphic(DataGraphic::Heatmap);
    grid->setData(activity);
    heat->addWidget(grid);
    heat->addWidget(label("星期 × 小时 · 当前页事件数", "font-size:10px;color:#a489b9;"));
    sideLayout->addWidget(heatCard);
    QVBoxLayout *trend;
    auto trendCard = card("操作趋势", &trend);
    auto chart = new DataGraphic(DataGraphic::GroupedBars);
    chart->setMinimumHeight(140);
    QStringList dates;
    for (auto d : days.keys())
        dates << d.mid(5);
    chart->setData({days.values()}, dates, "次");
    trend->addWidget(chart);
    sideLayout->addWidget(trendCard);
    sideLayout->addWidget(selectedCard);
    body->addWidget(row({timelineCard, side}, {3, 2}));
    body->addWidget(row({button("上一页", this,
                                [this] {
                                    offsetLogs = qMax(0, offsetLogs - 200);
                                    refresh();
                                }),
                         label(QString("偏移 %1 · 每页最多 200 条").arg(offsetLogs)),
                         button("下一页", this, [this] {
                             if (logs.size() == 200) {
                                 offsetLogs += 200;
                                 refresh();
                             }
                         })}));
}
void AdminWindow::forecastPage() {
    body->addWidget(label("ENERGY INTELLIGENCE / 历史实验",
                          "font-size:10px;letter-spacing:2px;color:#a18bb9;"));
    body->addWidget(label("负荷预测", "font-size:30px;font-weight:600;"));
    body->addWidget(
        label("从历史充电节律，观察未来 24 小时的能源需求。", "font-size:12px;color:#b09ac2;"));
    auto host = new QWidget;
    auto layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(label("正在读取历史预测…"));
    body->addWidget(host);
    api->get("/admin/console/forecast?scope=" + forecastScope, host, [=](const Reply &r) {
        clearLayout(layout);
        message(r);
        if (!r.ok) {
            layout->addWidget(label(r.error));
            return;
        }
        auto d = r.data.object();
        if (!d.value("ready").toBool()) {
            layout->addWidget(label(text(d, "message")));
            return;
        }
        auto select = new QComboBox;
        for (const auto &v : d.value("scopes").toArray()) {
            auto o = v.toObject();
            select->addItem(text(o, "label"), text(o, "id"));
        }
        select->setCurrentIndex(select->findData(forecastScope));
        connect(select, &QComboBox::activated, host, [=](int i) {
            forecastScope = select->itemData(i).toString();
            navigate("forecast");
        });
        layout->addWidget(row(
            {label("历史实验 · 非实时   数据截止 " + text(d, "cutoff"), "color:#e3b0df;"), select},
            {3, 1}));
        auto history = d.value("history").toArray(), pred = d.value("prediction").toArray();
        double total = 0, peak = 0;
        for (auto v : pred) {
            total += v.toDouble();
            peak = qMax(peak, v.toDouble());
        }
        layout->addWidget(
            row({metricCard("未来 24 小时电量", money(total) + " kWh", text(d, "label")),
                 metricCard("预测小时峰值", money(peak), "kWh / 小时 · 历史估算口径"),
                 metricCard("已选用模型", text(d, "model"),
                            QString("经验范围测试覆盖率 %1%").arg(number(d, "test_coverage")))}));
        QVBoxLayout *chartLayout;
        auto box = card("充电需求趋势 · 最近48小时与未来24小时", &chartLayout);
        auto chart = baseChart("小时电量 kWh；横轴 48 为预测开始");
        auto a = new QLineSeries;
        a->setName("历史");
        auto b = new QLineSeries;
        b->setName("预测");
        auto lo = new QLineSeries;
        auto hi = new QLineSeries;
        for (int i = 0; i < history.size(); i++)
            a->append(i, history[i].toDouble());
        for (int i = 0; i < pred.size(); i++) {
            b->append(i + 48, pred[i].toDouble());
            lo->append(i + 48, d.value("lower").toArray()[i].toDouble());
            hi->append(i + 48, d.value("upper").toArray()[i].toDouble());
        }
        a->setPen(QPen(QColor("#dc98e4"), 2.5));
        b->setPen(QPen(QColor("#85dfcf"), 3));
        auto band = new QAreaSeries(hi, lo);
        band->setName("经验误差范围");
        band->setBrush(QColor("#554f2769"));
        band->setPen(Qt::NoPen);
        chart->addSeries(band);
        chart->addSeries(a);
        chart->addSeries(b);
        chart->createDefaultAxes();
        for (auto axis : chart->axes()) {
            axis->setLabelsColor(QColor("#b6a0c6"));
            axis->setGridLineColor(QColor("#352342"));
        }
        chartLayout->addWidget(view(chart, 340));
        layout->addWidget(box);
        QList<QWidget *> metrics;
        for (const auto &v : d.value("metrics").toArray()) {
            auto m = v.toObject();
            metrics << metricCard(
                text(m, "model"), QString::number(number(m, "wape"), 'f', 3) + "% WAPE",
                QString("测试 MAE %1 · 验证 MAE %2")
                    .arg(money(number(m, "mae")), money(number(m, "validation_mae"))));
        }
        layout->addWidget(row(metrics));
        layout->addWidget(label(text(d, "protocol") + "\n" + text(d, "limitations"),
                                "color:#aa91ba;font-size:12px;"));
    });
}
void AdminWindow::settings() {
    showDetail(this, "平台运营信息", summary);
}
void AdminWindow::withdrawalsPage() {
    if(!api->authenticated()){manualLogin();return;}
    auto d=new QDialog(this);d->setAttribute(Qt::WA_DeleteOnClose);d->resize(980,560);
    auto l=new QVBoxLayout(d);l->addWidget(dialogHeader(d,label("提现审核 · 演示到账")));
    auto filter=new QComboBox;filter->addItem("待审核","pending");filter->addItem("模拟已到账","paid");filter->addItem("已驳回","rejected");l->addWidget(filter);
    auto table=new QTableWidget;l->addWidget(table);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto items=std::make_shared<QJsonArray>();auto load=std::make_shared<std::function<void()>>();
    *load=[=]{auto state=filter->currentData().toString();api->get("/admin/withdrawals?status="+state,d,[=](const Reply &r){
        if(state!=filter->currentData().toString())return;
        if(!r.ok){QMessageBox::warning(d,"读取失败",r.error);return;}*items=r.data.array();
        QStringList keys={"phone","nickname","amount","destination","status","requested_at","review_note"};
        table->setColumnCount(keys.size());table->setHorizontalHeaderLabels({"手机号","昵称","金额","演示收款账户","状态","申请时间","审核意见"});table->setRowCount(items->size());
        for(int i=0;i<items->size();++i)for(int j=0;j<keys.size();++j)table->setItem(i,j,new QTableWidgetItem(statusText(text((*items)[i].toObject(),keys[j]))));
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    });};
    connect(d,&QDialog::finished,this,[load]{*load={};});
    connect(filter,&QComboBox::activated,d,[=](int){if(*load)(*load)();});
    for(bool approve:{true,false})l->addWidget(button(approve?"审核通过并模拟到账":"驳回并解冻",d,[=]{
        int i=table->currentRow();if(i<0||i>=items->size())return;auto o=(*items)[i].toObject();if(text(o,"status")!="pending")return;
        bool ok=false;auto note=QInputDialog::getText(d,"审核意见",approve?"确认模拟到账，请填写审核意见":"填写驳回原因",QLineEdit::Normal,approve?"核对通过，模拟到账":"",&ok);
        if(!ok||note.trimmed().isEmpty())return;
        command(d,api,"POST","/admin/withdrawals/"+text(o,"id")+"/review",{{"approve",approve},{"note",note.trimmed()}},[=](const Reply &){if(*load)(*load)();refresh();});
    }));
    (*load)();d->show();
}
void AdminWindow::tariffDialog(const QJsonObject &station,std::function<void()> after) {
    auto d=new QDialog(this);d->setAttribute(Qt::WA_DeleteOnClose);d->resize(650,500);
    auto l=new QVBoxLayout(d);l->addWidget(dialogHeader(d,label("分时价格 · "+text(station,"name"))));
    l->addWidget(label("北京时间0–24点连续覆盖；新预约生效，已有订单保持预约时价格。"));
    auto table=new QTableWidget(0,4);table->setHorizontalHeaderLabels({"开始小时","结束小时","电费 元/kWh","服务费 元/kWh"});l->addWidget(table);
    auto periods=station.value("tariff").toArray();
    if(periods.isEmpty()&&station.value("tariff").isString())periods=QJsonDocument::fromJson(text(station,"tariff").toUtf8()).array();
    auto populate=[=](const QJsonArray &rows){table->setRowCount(rows.size());for(int i=0;i<rows.size();++i){auto o=rows[i].toObject();QStringList keys={"start_hour","end_hour","electricity_price","service_price"};for(int j=0;j<4;++j)table->setItem(i,j,new QTableWidgetItem(text(o,keys[j])));}};
    if(periods.isEmpty())periods.append(QJsonObject{{"start_hour",0},{"end_hour",24},{"electricity_price",station.value("unit_price")},{"service_price",0}});
    populate(periods);
    l->addWidget(button("应用峰谷示例",d,[=]{double base=number(station,"unit_price");populate(QJsonArray{
        QJsonObject{{"start_hour",0},{"end_hour",7},{"electricity_price",QString::number(base*.6,'f',2)},{"service_price",0}},
        QJsonObject{{"start_hour",7},{"end_hour",18},{"electricity_price",QString::number(base,'f',2)},{"service_price",0}},
        QJsonObject{{"start_hour",18},{"end_hour",22},{"electricity_price",QString::number(base*1.3,'f',2)},{"service_price",0}},
        QJsonObject{{"start_hour",22},{"end_hour",24},{"electricity_price",QString::number(base*.6,'f',2)},{"service_price",0}}});}));
    l->addWidget(row({button("新增时段",d,[=]{int i=table->rowCount();table->insertRow(i);for(int j=0;j<4;++j)table->setItem(i,j,new QTableWidgetItem("0"));}),button("删除选中时段",d,[=]{if(table->currentRow()>=0)table->removeRow(table->currentRow());})}));
    l->addWidget(button("确认保存",d,[=]{QJsonArray rows;for(int i=0;i<table->rowCount();++i){QJsonObject o;QStringList keys={"start_hour","end_hour","electricity_price","service_price"};for(int j=0;j<4;++j){if(!table->item(i,j))return;o[keys[j]]=table->item(i,j)->text();}rows.append(o);}
        command(d,api,"PUT","/admin/stations/"+text(station,"id")+"/tariff",{{"periods",rows}},[=](const Reply &){d->accept();after();});},true));
    d->show();
}
void AdminWindow::manager(const QString &kind) {
    if (!api->authenticated()) {
        manualLogin();
        return;
    }
    auto d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle("运营数据管理");
    d->resize(1100, 650);
    auto layout = new QVBoxLayout(d);
    auto title = label("数据管理 · " + QMap<QString, QString>{{"users", "用户"},
                                                              {"stations", "站点"},
                                                              {"chargers", "电桩"},
                                                              {"orders", "订单"}}
                                           .value(kind),
                       "font-size:23px;");
    layout->addWidget(dialogHeader(d, title));
    auto search = new QLineEdit;
    search->setPlaceholderText("筛选当前页：站点、编号、手机号或状态");
    layout->addWidget(search);
    auto stationFilter=new QComboBox;stationFilter->addItem("全部站点","");
    for(auto v:stations){auto o=v.toObject();stationFilter->addItem(text(o,"name"),text(o,"id"));}
    auto stateFilter=new QComboBox;stateFilter->addItem("全部状态","");
    for(auto state:QStringList{"available","reserved","charging","faulted"})stateFilter->addItem(statusText(state),state);
    auto typeFilter=new QComboBox;typeFilter->addItem("全部类型","");typeFilter->addItem("快充","fast");typeFilter->addItem("慢充","slow");
    auto minimumPower=new QDoubleSpinBox;minimumPower->setRange(0,1000);minimumPower->setPrefix("最低功率 ");minimumPower->setSuffix(" kW");
    auto maximumPower=new QDoubleSpinBox;maximumPower->setRange(0,1000);maximumPower->setValue(1000);maximumPower->setPrefix("最高功率 ");maximumPower->setSuffix(" kW");
    auto filterRow=row({stationFilter,stateFilter,typeFilter,minimumPower,maximumPower});filterRow->setVisible(kind=="chargers");layout->addWidget(filterRow);
    auto table = new QTableWidget;
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    layout->addWidget(table, 1);
    auto records = std::make_shared<QJsonArray>();
    auto applyFilters=[=] {
        for(int i=0;i<table->rowCount();++i){
            auto o=(*records)[i].toObject();QString all;
            for(int j=0;j<table->columnCount();++j)if(table->item(i,j))all+=table->item(i,j)->text()+" ";
            bool visible=all.contains(search->text().trimmed(),Qt::CaseInsensitive);
            if(kind=="chargers") visible=visible && (stationFilter->currentIndex()==0 || text(o,"station_id")==stationFilter->currentData().toString())
                && (stateFilter->currentIndex()==0 || text(o,"status")==stateFilter->currentData().toString())
                && (typeFilter->currentIndex()==0 || text(o,"kind")==typeFilter->currentData().toString())
                && number(o,"power_kw")>=minimumPower->value() && number(o,"power_kw")<=maximumPower->value();
            table->setRowHidden(i,!visible);
        }
    };
    connect(search,&QLineEdit::textChanged,table,[=](const QString &){applyFilters();});
    for(auto choice:{stationFilter,stateFilter,typeFilter})connect(choice,&QComboBox::activated,table,[=](int){applyFilters();});
    connect(minimumPower,&QDoubleSpinBox::valueChanged,table,[=](double){applyFilters();});
    connect(maximumPower,&QDoubleSpinBox::valueChanged,table,[=](double){applyFilters();});
    auto stationDetails=new QTableWidget;stationDetails->setVisible(kind=="stations");stationDetails->setMinimumHeight(160);
    stationDetails->setEditTriggers(QAbstractItemView::NoEditTriggers);layout->addWidget(stationDetails);
    auto tools = new QHBoxLayout;
    layout->addLayout(tools);
    auto items = records;
    auto offset = std::make_shared<int>(0);
    auto load = std::make_shared<std::function<void()>>();
    auto selection = [=]() {
        int i = table->currentRow();
        return i >= 0 && i < items->size() ? (*items)[i].toObject() : QJsonObject();
    };
    *load = [=] {
        QString path = "/admin/" + kind;
        if (kind == "users" || kind == "orders")
            path += QString("?limit=200&offset=%1").arg(*offset);
        api->get(path, d, [=](const Reply &r) {
            if (!r.ok) {
                QMessageBox::warning(d, "读取失败", r.error);
                return;
            }
            *items = r.data.array();
            QStringList columns =
                kind == "users"      ? QStringList{"id", "nickname", "phone", "balance", "status", "created_at"}
                : kind == "stations" ? QStringList{"id", "name", "address", "longitude", "latitude", "unit_price", "charger_count", "online_rate"}
                : kind == "chargers"
                    ? QStringList{"station_name", "code", "kind", "power_kw", "status", "total_sessions", "total_minutes"}
                    : QStringList{"station_name", "charger_code", "phone", "status", "amount"};
            QMap<QString, QString> names = {{"id", "编号"}, {"created_at", "注册时间"}, {"longitude", "经度"}, {"latitude", "纬度"}, {"online_rate", "在线率 %"}, {"total_sessions", "累计次数"}, {"total_minutes", "累计分钟"}, {"nickname", "昵称"},     {"phone", "手机号"},
                                            {"balance", "余额"},      {"status", "状态"},
                                            {"name", "站点"},         {"address", "地址"},
                                            {"unit_price", "单价"},   {"charger_count", "电桩数"},
                                            {"station_name", "站点"}, {"code", "电桩编号"},
                                            {"kind", "类型"},         {"power_kw", "功率 kW"},
                                            {"charger_code", "电桩"}, {"amount", "金额"}};
            QStringList headings;
            for (auto k : columns)
                headings << names.value(k, k);
            table->setColumnCount(columns.size());
            table->setHorizontalHeaderLabels(headings);
            table->setRowCount(items->size());
            for (int i = 0; i < items->size(); i++)
                for (int j = 0; j < columns.size(); j++)
                    table->setItem(
                        i, j,
                        new QTableWidgetItem(columns[j].endsWith("_at")
                            ? localDateTime(text((*items)[i].toObject(), columns[j]))
                            : statusText(text((*items)[i].toObject(), columns[j]))));
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
            table->horizontalHeader()->setStretchLastSection(true);applyFilters();
            title->setText(
                QString("运营数据管理 · %1 条 · 第 %2 页").arg(items->size()).arg(*offset / 200 + 1));
        });
    };
    connect(table,&QTableWidget::cellClicked,d,[=](int,int){
        if(kind!="stations")return;
        auto o=selection();auto id=text(o,"id");
        stationDetails->setProperty("stationId",id);
        api->get("/public/stations/"+id+"/chargers",d,[=](const Reply &r){
            if(stationDetails->property("stationId").toString()!=id)return;
            if(!r.ok){message(r);return;}auto rows=r.data.array();
            stationDetails->setColumnCount(5);stationDetails->setHorizontalHeaderLabels({"电桩编号","状态","类型","功率 kW","累计次数"});stationDetails->setRowCount(rows.size());
            QStringList keys={"code","status","kind","power_kw","total_sessions"};
            for(int i=0;i<rows.size();++i)for(int j=0;j<keys.size();++j)stationDetails->setItem(i,j,new QTableWidgetItem(statusText(text(rows[i].toObject(),keys[j]))));
            stationDetails->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        });
    });
    connect(table, &QTableWidget::cellDoubleClicked, d, [=](int, int) {
        auto o = selection(); if (o.isEmpty()) return;
        if (kind == "users" || kind == "stations") {
            const auto path = kind == "users" ? "/admin/users/" + text(o,"id") + "/wallet"
                : "/public/stations/" + text(o,"id") + "/chargers";
            api->get(path, d, [=](const Reply &r) {
                if (r.ok) showDetail(d, kind == "users" ? "用户钱包流水" : "站点电桩状态", {{"记录", r.data.array()}});
                else message(r);
            });
        } else showDetail(d, "记录详情", o);
    });
    // Form callbacks hold only the dialog-owned refresh function; release on close.
    connect(d, &QDialog::finished, this, [load] { *load = {}; });
    auto after = [this, load] {
        if (*load)
            (*load)();
        refresh();
    };
    tools->addWidget(button(
        "新增", d,
        [=] {
            QList<QPair<QString, QString>> fields;
            QJsonObject values;
            if (kind == "users")
                fields = {{"phone", "手机号"}, {"nickname", "昵称"}};
            if (kind == "stations") {
                fields = {{"name", "站点名称"},
                          {"address", "地址"},
                          {"longitude", "经度"},
                          {"latitude", "纬度"},
                          {"unit_price", "单价 元/kWh"}, {"initial_chargers", "初始电桩数量（0–100）"}};
                values = {{"longitude", "114.05"}, {"latitude", "22.55"}, {"unit_price", "1.20"}, {"initial_chargers", 0}};
            }
            if (kind == "chargers") {
                auto form = new QDialog(d); form->setAttribute(Qt::WA_DeleteOnClose);
                auto rows = new QVBoxLayout(form);
                rows->addWidget(dialogHeader(form, label("新增电桩")));
                auto choices = new QComboBox;
                for (auto v : stations) { auto o = v.toObject(); choices->addItem(text(o,"name"), text(o,"id")); }
                auto type = new QComboBox; type->addItem("快充", "fast"); type->addItem("慢充", "slow");
                auto power = new QDoubleSpinBox; power->setRange(1, 1000); power->setValue(120); power->setSuffix(" kW");
                rows->addWidget(choices); rows->addWidget(type); rows->addWidget(power);
                rows->addWidget(label("编号由系统按站点名称自动生成"));
                rows->addWidget(button("确认保存", form, [=] {
                    if (choices->currentIndex() < 0) return;
                    command(form, api, "POST", "/admin/chargers",
                        {{"station_id",choices->currentData().toString()}, {"kind",type->currentData().toString()}, {"power_kw",power->value()}},
                        [=](const Reply &) { form->accept(); after(); });
                }, true));
                form->show(); return;
            }
            if (kind == "orders") {
                fields = {{"user_id", "用户 UUID"}, {"charger_id", "电桩 UUID"}};
                values = {{"idempotency_key", uid()}};
            }
            editForm(d, api, "新增记录", "POST", "/admin/" + kind, fields, values, after);
        },
        true));
    tools->addWidget(button("编辑", d, [=] {
        auto o = selection();
        if (o.isEmpty())
            return;
        QList<QPair<QString, QString>> fields;
        if (kind == "users")
            fields = {{"phone", "手机号"}, {"nickname", "昵称"}};
        if (kind == "stations")
            fields = {{"name", "站点名称"},
                      {"address", "地址"},
                      {"longitude", "经度"},
                      {"latitude", "纬度"},
                      {"unit_price", "单价"}};
        if (kind == "chargers")
            fields = {{"code", "电桩编号"}, {"kind", "类型 fast / slow"}, {"power_kw", "功率 kW"}};
        if (fields.isEmpty()) {
            showDetail(d, "订单由状态机控制", o);
            return;
        }
        QJsonObject values;
        for (auto f : fields)
            values[f.first] = o[f.first];
        editForm(d, api, "修改资料", "PATCH", "/admin/" + kind + "/" + text(o, "id"), fields,
                 values, after);
    }));
    tools->addWidget(button("详情 / 编号", d, [=] {
        auto o = selection();
        if (!o.isEmpty())
            showDetail(d, "记录详情", o);
    }));
    if (kind != "orders")
        tools->addWidget(button("删除", d, [=] {
            auto o = selection();
            if (o.isEmpty())
                return;
            if (QMessageBox::question(d, "删除记录",
                                      "确认删除所选记录？存在业务历史的对象由服务器阻止删除。") !=
                QMessageBox::Yes)
                return;
            command(d, api, "DELETE", "/admin/" + kind + "/" + text(o, "id"), {},
                    [after](const Reply &) { after(); });
        }));
    if(kind=="stations") {
        tools->addWidget(button("分时价格",d,[=]{auto o=selection();if(!o.isEmpty())tariffDialog(o,after);}));
        tools->addWidget(button("解析附近地址",d,[=]{
            auto o=selection();if(o.isEmpty())return;
            api->get(QString("/public/map/reverse?latitude=%1&longitude=%2").arg(number(o,"latitude"),0,'f',6).arg(number(o,"longitude"),0,'f',6),d,[=](const Reply &r){
                if(!r.ok){QMessageBox::warning(d,"地址解析失败",r.error);return;}
                editForm(d,api,"核对坐标附近地址","PATCH","/admin/stations/"+text(o,"id"),{{"address","附近地址（核对后保存）"}},
                         {{"address",r.data.object().value("address")},{"address_verified",true}},after);
            });
        }));
    }
    if (kind == "users") {
        tools->addWidget(button("冻结 / 解冻", d, [=] {
            auto o = selection();
            if (o.isEmpty())
                return;
            if (QMessageBox::question(d, "确认账号状态变更", "确认" + QString(text(o, "status") == "active" ? "冻结" : "解冻") + "用户 " + text(o, "phone") + "？") != QMessageBox::Yes) return;
            command(d, api, "PATCH", "/admin/users/" + text(o, "id") + "/status",
                    {{"status", text(o, "status") == "active" ? "frozen" : "active"}},
                    [after](const Reply &) { after(); });
        }));
        tools->addWidget(button("钱包调账", d, [=] {
            auto o = selection();
            if (o.isEmpty())
                return;
            editForm(d, api, "钱包调账", "POST",
                     "/admin/users/" + text(o, "id") + "/wallet-adjustments",
                     {{"amount", "金额（可为负）"}, {"reason", "调账原因"}},
                     {{"idempotency_key", uid()}}, after);
        }));
        tools->addWidget(button("钱包账本", d, [=] {
            auto o = selection();
            if (o.isEmpty())
                return;
            api->get("/admin/users/" + text(o, "id") + "/wallet", d, [=](const Reply &r) {
                if (r.ok)
                    showDetail(d, "钱包账本", {{"钱包流水", r.data.array()}});
                else
                    message(r);
            });
        }));
    }
    if (kind == "chargers") {
        tools->addWidget(button("故障 / 恢复", d, [=] {
            auto o = selection();
            if (o.isEmpty())
                return;
            command(d, api, "PATCH", "/admin/chargers/" + text(o, "id") + "/status",
                    {{"status", text(o, "status") == "faulted" ? "available" : "faulted"}},
                    [after](const Reply &) { after(); });
        }));
        tools->addWidget(button("远程重启", d, [=] {
            auto o = selection();
            if (o.isEmpty())
                return;
            command(d, api, "POST", "/admin/chargers/" + text(o, "id") + "/restart", {},
                    [after](const Reply &) { after(); });
        }));
    }
    if (kind == "orders") {
        for (auto pair : QList<QPair<QString, QString>>{
                 {"start", "开始"}, {"cancel", "取消"}, {"stop", "停止并生成账单"}})
            tools->addWidget(button(pair.second, d, [=] {
                auto o = selection();
                if (o.isEmpty())
                    return;
                command(d, api, "POST", "/admin/orders/" + text(o, "id") + "/" + pair.first, {},
                        [after](const Reply &) { after(); });
            }));
        tools->addWidget(button("退款", d, [=] {
            auto o = selection();
            if (o.isEmpty())
                return;
            editForm(d, api, "部分退款", "POST", "/admin/orders/" + text(o, "id") + "/refund",
                     {{"amount", "退款金额"}, {"reason", "退款原因"}}, {{"idempotency_key", uid()}},
                     after);
        }));
    }
    auto footer = new QHBoxLayout;
    layout->addLayout(footer);
    footer->addWidget(button("上一页", d, [=] {
        *offset = qMax(0, *offset - 200);
        (*load)();
    }));
    footer->addWidget(button("下一页", d, [=] {
        if (items->size() == 200 && (kind == "users" || kind == "orders")) {
            *offset += 200;
            (*load)();
        }
    }));
    footer->addWidget(button("导出 CSV", d, [=] { exportRecords(d, *items); }));
    (*load)();
    d->show();
}
