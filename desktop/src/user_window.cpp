#include "user_window.h"
#include "station_map.h"
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
        paths << "/me" << "/me/orders?limit=200" << "/me/wallet-entries?limit=200";
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
                if (body->count() <= 1 || current == "profile" || current == "history" ||
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
    if (page == "schedule" && current != "schedule") scheduleOrigin = current;
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
    else
        home();
    body->addStretch();
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
            chargeTime->setText(QString("预约剩余 %1分%2秒").arg(seconds / 60).arg(seconds % 60));
        } else if (s == "charging" && number(active, "target_energy_kwh") > 0) {
            double remaining = qMax(0., number(active, "target_energy_kwh") - number(active, "energy_kwh"));
            double seconds = remaining * 3600 / qMax(1., number(active, "power_kw")) /
                qMax(1., number(active, "time_scale"));
            chargeTime->setText(QString("预计 %1分%2秒后达到上限（模拟时间）")
                .arg(int(seconds) / 60).arg(int(seconds) % 60));
        } else chargeTime->setText(s == "completed" ? "结算完成 · " + statusText(text(active, "stop_reason")) : "");
    }
}
void UserWindow::home() {
    auto title = new QHBoxLayout;
    vehicleTitle = label("未绑定车辆", "font-size:23px;font-weight:600;");
    vehicleTitle->setObjectName("vehicle-title");
    title->addWidget(vehicleTitle);
    title->addStretch();
    auto account = button("", this, [this] { navigate("profile"); });
    account->setIcon(appIcon("car"));
    account->setFixedSize(34, 34);
    account->setStyleSheet(
        "padding:0;min-height:0;border:1px solid #35243f;border-radius:17px;background:#1d122b;");
    title->addWidget(account);
    auto notice = button("", this, [this] { navigate("history"); });
    notice->setIcon(appIcon("bell"));
    notice->setFixedSize(34, 34);
    notice->setStyleSheet(
        "padding:0;min-height:0;border:1px solid #35243f;border-radius:17px;background:#1d122b;");
    title->addWidget(notice);
    body->addLayout(title);
    auto hero = new ArtWidget(ArtWidget::UserHero);
    batteryArt = hero;
    hero->setValue(batterySoc(), "模拟车辆电量");
    hero->setFixedHeight(312);
    body->addWidget(hero);
    vehicleState = label("");
    vehicleState->setObjectName("home-order-state");
    updateCharging();
    QVBoxLayout *m;
    auto mapCard = card("附近充电站", &m);
    m->setContentsMargins(12, 12, 12, 12);
    m->setSpacing(10);
    auto map = new StationMap;
    map->setApi(api);
    map->setFixedHeight(175);
    map->setCompact(true);
    map->setStations(stations, selectedStation);
    connect(map, &StationMap::stationSelected, this, [this](QString id) {
        selectedStation = id;
        navigate("station");
    });
    m->addWidget(map);
    auto explore = button("探索附近站点     ↗", this, [this] { navigate("map"); });
    explore->setProperty("quiet",true);
    m->addWidget(explore);
    body->addWidget(mapCard);
    body->addWidget(button("ϟ  智能充电     →", this, [this] { navigate("charging"); }, true));
    body->addWidget(vehicleState);
    body->addWidget(
        label("车辆电量按订单模拟更新 · 费用按实际订单结算", "font-size:9px;color:#8f759f;"));
}
void UserWindow::mapPage() {
    body->addWidget(pageHeading("发现充电网络", this, [this] { navigate("home"); }));
    auto search = new QLineEdit;
    search->setPlaceholderText("搜索站点名称或地址");
    body->addWidget(search);
    auto canvas = new QWidget;
    auto layers = new QGridLayout(canvas);
    layers->setContentsMargins(0, 0, 0, 0);
    auto map = new StationMap;
    map->setApi(api);
    map->setMinimumHeight(350);
    map->setStations(stations, selectedStation);
    layers->addWidget(map, 0, 0);
    auto detail = new QFrame;
    detail->setObjectName("station-sheet");
    detail->setStyleSheet(
        "QFrame#station-sheet{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #2a1942,stop:1 "
        "#1a1129);border:1px solid #50305f;border-radius:16px;}");
    auto l = new QVBoxLayout(detail);
    l->setContentsMargins(17, 17, 17, 17);
    l->setSpacing(8);
    auto name = label("", "font-size:21px;font-weight:600;");
    auto address = label("", muted);
    auto availability = label("", "font-size:13px;");
    auto price = label("", "font-size:13px;color:#e7b1e7;");
    l->addWidget(name);
    l->addWidget(address);
    l->addSpacing(8);
    l->addWidget(availability);
    l->addWidget(price);
    l->addWidget(button("ϟ  选择充电接口", this, [this] { navigate("station"); }, true));
    layers->addWidget(detail, 1, 0);
    layers->setVerticalSpacing(0);
    auto showStation = [=](const QString &id) {
        selectedStation = id;
        for (auto v : stations) {
            auto o = v.toObject();
            if (text(o, "id") != id)
                continue;
            name->setText(text(o, "name"));
            address->setText(text(o, "address"));
            availability->setText(QString("%1 / %2 个空闲接口")
                                      .arg(number(o, "available_count"))
                                      .arg(number(o, "charger_count")));
            price->setText(
                QString("基础价 ¥%1 / kWh · 分时价格见接口页").arg(money(number(o, "unit_price"))));
        }
        map->setStations(stations, id);
    };
    connect(map, &StationMap::stationSelected, canvas, showStation);
    showStation(selectedStation);
    body->addWidget(canvas);
    auto results = new QWidget;
    auto list = new QVBoxLayout(results);
    list->setContentsMargins(0, 0, 0, 0);
    body->insertWidget(2, results);
    results->setMaximumHeight(180);
    auto resultChoices = new QComboBox;
    resultChoices->setPlaceholderText("搜索结果：选择站点查看详情");
    list->addWidget(resultChoices);
    connect(resultChoices, &QComboBox::activated, canvas, [=](int i) {
        showStation(resultChoices->itemData(i).toString());
    });
    auto radius=new QComboBox;
    radius->addItem("不限距离",0);radius->addItem("5 km 内",5);radius->addItem("10 km 内",10);radius->addItem("50 km 内",50);
    list->addWidget(radius);
    auto filtered=[=] {
        resultChoices->clear();QJsonArray visible;
        for(auto v:stations){auto o=v.toObject();
            if(!(text(o,"name")+text(o,"address")).contains(search->text().trimmed(),Qt::CaseInsensitive))continue;
            const double km=radius->currentData().toDouble();
            const bool located=o.contains("distance_km")&&!o.value("distance_km").isNull();
            if(km>0&&(!located||number(o,"distance_km")>km))continue;
            visible.append(o);
            resultChoices->addItem(text(o,"name")+(located?" · "+money(number(o,"distance_km"))+" km":""),text(o,"id"));
        }
        resultChoices->setPlaceholderText(visible.isEmpty()?"无匹配站点；请定位或扩大范围":"选择站点查看详情");
        detail->setVisible(!visible.isEmpty());
        if(!visible.isEmpty())showStation(text(visible.first().toObject(),"id"));
        map->setStations(visible,selectedStation);
    };
    auto generation=std::make_shared<int>(0);
    connect(map,&StationMap::locationChanged,canvas,[=](double lat,double lon) {
        const int request=++*generation;
        api->get(QString("/public/stations?latitude=%1&longitude=%2").arg(lat,0,'f',6).arg(lon,0,'f',6),canvas,[=](const Reply &r){
            if(request!=*generation)return;
            if(!r.ok){message(r);return;}stations=r.data.array();filtered();
        });
    });
    connect(search,&QLineEdit::textChanged,results,[=]{filtered();});
    connect(radius,&QComboBox::currentIndexChanged,results,[=]{filtered();});
    filtered();
}
void UserWindow::stationPage() {
    QJsonObject station;
    for (auto v : stations)
        if (text(v.toObject(), "id") == selectedStation)
            station = v.toObject();
    body->addWidget(
        pageHeading(text(station, "name", "选择充电站"), this, [this] { navigate("map"); }));
    if (station.isEmpty()) {
        body->addWidget(emptyPanel("请选择站点", "从地图选择站点后查看充电接口", "pin"));
        return;
    }
    auto modes = new Segments({"经济模式", "快速充电"}, chargeMode);
    connect(modes, &Segments::changed, this, [this](int n) {
        chargeMode = n;
        navigate("station");
    });
    body->addWidget(modes);
    body->addWidget(button("查看分时价格",this,[this]{navigate("schedule");}));
    body->addWidget(label("经济模式优先慢充，快速模式仅可选择空闲快充；下方展示本站全部电桩。", muted));
    QVBoxLayout *life;
    auto energy = card(me.isEmpty()?"我的车辆":text(me,"vehicle_name","我的车辆"), &life);
    energy->setStyleSheet("QFrame#card{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 "
                          "#241a37,stop:1 #2d1745);border:1px solid #392847;border-radius:14px;}");
    auto car = new ArtWidget(ArtWidget::TopCar);
    car->setFixedHeight(140);
    auto vehicleMetrics=new QWidget;auto metricsLayout=new QVBoxLayout(vehicleMetrics);metricsLayout->setContentsMargins(0,0,0,0);
    metricsLayout->addWidget(valueBlock(me.isEmpty()?"—":money(batterySoc()), "%", "账户模拟电量"));
    metricsLayout->addWidget(valueBlock(me.contains("battery_kwh")?QString::number(number(me,"battery_kwh")):"—", "kWh", "电池容量"));
    life->addWidget(row({car,vehicleMetrics},{3,2}));
    auto limit = new QComboBox;
    for (int n : {50, 60, 70, 80, 90, 100})
        limit->addItem(QString("充电上限：%1%").arg(n), n);
    const int preferredLimit=int(me.contains("charge_limit")?number(me,"charge_limit"):80);
    if(limit->findData(preferredLimit)<0)limit->addItem(QString("充电上限：%1%").arg(preferredLimit),preferredLimit);
    limit->setCurrentIndex(limit->findData(preferredLimit));
    body->addWidget(energy);
    QVBoxLayout *selectionLayout;
    auto selectionBox=card("选择电桩",&selectionLayout);
    selectionLayout->addWidget(limit);
    selectionLayout->addWidget(label("绿：空闲  橙：充电中  蓝：已预约  红：故障/停用","font-size:10px;color:#c6b5d1;"));
    auto picker=new ChargerPicker;selectionLayout->addWidget(picker);
    auto pickerState=label("正在读取电桩状态…","font-size:11px;color:#bca8ca;");selectionLayout->addWidget(pickerState);
    body->addWidget(selectionBox);
    QVBoxLayout *details;
    auto box = card("", &details);
    auto estimateEnergy = label(""), estimateTotal = label("");
    auto estimated = new QWidget;
    auto el = new QVBoxLayout(estimated);
    el->setContentsMargins(0, 0, 0, 0);
    auto amountRow = [&](const QString &caption, QLabel *value) {
        auto r = new QHBoxLayout;
        r->addWidget(label(caption, "font-size:12px;color:#bca8ca;"), 1);
        value->setStyleSheet("font-size:13px;font-weight:600;");
        r->addWidget(value);
        el->addLayout(r);
    };
    amountRow("ϟ  预估充电量", estimateEnergy);
    el->addWidget(
        detailLine("bolt", "基础单价（分时表优先）", "¥" + money(number(station, "unit_price")) + " / kWh"));
    amountRow("费用范围（以实际账单为准）", estimateTotal);
    details->addWidget(estimated);
    auto updateEstimate = [=] {
        double kwh = qMax(0., (limit->currentData().toInt() - batterySoc()) * (me.contains("battery_kwh") ? number(me,"battery_kwh") : 60.) / 100.);
        estimateEnergy->setText(me.isEmpty() ? "登录后读取" : money(kwh) + " kWh");
        double low=number(station,"unit_price"),high=low;
        auto periods=station["tariff"].toArray();
        if(!periods.isEmpty()) {low=1e9;high=0;for(auto p:periods){double price=number(p.toObject(),"electricity_price")+number(p.toObject(),"service_price");low=qMin(low,price);high=qMax(high,price);}}
        estimateTotal->setText(me.isEmpty() ? "登录后计算" : "¥"+money(kwh*low)+(low==high?"":" – "+money(kwh*high)));
        cache->setPreference("charge_limit", limit->currentData().toString());
    };
    connect(limit, &QComboBox::activated, box, [=](int) { updateEstimate(); });
    updateEstimate();

    auto hint =
        label("电量按绑定车辆容量估算，服务端按实际时段价格生成账单。", "font-size:10px;color:#9f87b1;");
    details->addWidget(hint);
    auto reserve = new QPushButton("ϟ  预约并准备充电");
    reserve->setProperty("primary", true);
    reserve->setCursor(Qt::PointingHandCursor);
    auto reserving=std::make_shared<bool>(false);
    connect(reserve, &QPushButton::clicked, box, [=] {
        if(*reserving)return;
        if (!api->authenticated()) {
            login();
            return;
        }
        if (picker->selectedId().isEmpty())
            return;
        if (limit->currentData().toInt() <= batterySoc()) {
            hint->setText("充电上限必须高于当前电量，请调整上限或在车辆资料中更新电量。");
            return;
        }
        reserve->setEnabled(false);
        *reserving=true;picker->setEnabled(false);
        api->request("POST", "/orders",
                     {{"charger_id", picker->selectedId()}, {"idempotency_key", uid()},
                      {"initial_soc", batterySoc()}, {"target_soc", limit->currentData().toInt()}, {"battery_kwh", me.contains("battery_kwh") ? number(me,"battery_kwh") : 60}},
                     box, [=](const Reply &r) {
                         *reserving=false;picker->setEnabled(true);
                         if (r.ok) {
                             active = r.data.object();
                             navigate("charging");
                         } else {
                             hint->setText(r.error);
                             reserve->setEnabled(!picker->selectedId().isEmpty());
                         }
                     });
    });
    reserve->setEnabled(false);
    details->addWidget(reserve);
    body->addWidget(box);
    picker->selectionChanged=[=]{reserve->setEnabled(!*reserving&&!picker->selectedId().isEmpty());};
    const auto stationId=selectedStation;
    auto pending=std::make_shared<bool>(false);
    auto previous=std::make_shared<QJsonArray>();
    auto readonly=std::make_shared<bool>(true);
    auto load=[=]{
        if(*pending)return;
        *pending=true;
        api->get("/public/stations/"+stationId+"/chargers",box,[=](const Reply &r){
            *pending=false;
            if(!r.ok){picker->setChargers(*previous,true,chargeMode==1);*readonly=true;pickerState->setText("状态读取失败，暂不可预约："+r.error);return;}
            const auto items=r.data.array();
            if(items!=*previous || r.cached!=*readonly){picker->setChargers(items,r.cached,chargeMode==1);*previous=items;*readonly=r.cached;}
            pickerState->setText(r.cached?"离线缓存 · 暂不可预约":items.isEmpty()?"本站尚未配置电桩":picker->selectedId().isEmpty()?"请选择空闲电桩；若均不可用，请切换模式或站点。":"已选电桩以加粗边框标记 · 状态每秒更新");
        });
    };
    auto timer=new QTimer(box);connect(timer,&QTimer::timeout,box,load);timer->start(1000);load();

}
void UserWindow::charging() {
    body->addWidget(pageHeading("充电中枢", this, [this] { navigate("home"); }));
    QVBoxLayout *l;
    auto box = card(active.isEmpty() ? "车辆待充电" : text(active, "status") == "pending_payment" ? "充电账单 · 待支付" : text(active, "station_name"), &l);
    box->setStyleSheet(
        "QFrame#card{background:qradialgradient(cx:.5,cy:.4,radius:.8,fx:.5,fy:.4,stop:0 "
        "#36155d,stop:.6 #26113f,stop:1 #1b112b);border:1px solid #50325f;border-radius:24px;}");
    l->setContentsMargins(17, 20, 17, 20);
    l->setSpacing(14);
    auto ring = new ArtWidget(ArtWidget::Ring);
    ring->setFixedHeight(260);
    batteryArt = ring;
    ring->setValue(batterySoc(), "模拟车辆电量");
    l->addWidget(ring);
    if (active.isEmpty()) {
        l->addWidget(label("尚未连接充电桩", muted));
        l->addWidget(button("寻找充电站", this, [this] { navigate("map"); }, true));
    } else {
        chargeState = label(statusText(text(active, "status")), "font-size:18px;color:#e8a7e0;");
        l->addWidget(chargeState);
        chargeEnergy = label(money(number(active, "energy_kwh")) + " kWh", "font-size:22px;");
        chargeCost = label("¥" + money(number(active, "amount")), "font-size:22px;");
        auto metrics=new QFrame;metrics->setObjectName("charge-metrics");
        metrics->setStyleSheet("QFrame#charge-metrics{background:#21152f;border:1px solid #604074;border-radius:12px;}");
        auto grid=new QGridLayout(metrics);grid->setContentsMargins(14,12,14,12);
        grid->addWidget(label("订单电量",muted),0,0);grid->addWidget(label("账单费用",muted),0,1);
        grid->addWidget(chargeEnergy,1,0);grid->addWidget(chargeCost,1,1);
        l->addWidget(metrics);
        l->addWidget(label("电桩 " + text(active, "charger_code") + " · " +
            QString::number(number(active, "power_kw")) + " kW · 按预约时分时价格计费", muted));
        chargeTime = label("");
        l->addWidget(chargeTime);
        l->addWidget(button("查看分时账单明细", this, [this]{showDetail(this,"账单明细",{{"分时计费",active.value("billing_detail")}});}));
        auto s = text(active, "status");
        if (s == "reserved") {
            l->addWidget(button("⚡ 开始充电", this, [this] { orderCommand("start"); }, true));
            l->addWidget(button("取消预约", this, [this] { orderCommand("cancel"); }));
        } else if (s == "charging")
            l->addWidget(button("停止充电", this, [this] { orderCommand("stop"); }, true));
        else if (s == "pending_payment") {
            l->addWidget(label("充电已停止，费用不再增加。确认付款后从钱包扣款。", muted));
            auto pay = button("确认支付 ¥" + money(number(active,"amount")), this,[this]{orderCommand("pay");},true);
            pay->setObjectName("pay-order"); l->addWidget(pay);
        }
        else
            l->addWidget(button("查看订单历史", this, [this] { navigate("history"); }));
    }
    body->addWidget(box);
    updateCharging();
    body->addWidget(label("订单电量按服务端功率与时间模拟，费用与状态以服务端为准。", muted));
}
void UserWindow::orderCommand(const QString &action) {
    if (active.isEmpty())
        return;
    const auto orderId = text(active,"id");
    if (action == "pay" && QMessageBox::question(this,"确认付款",
        "确认从钱包支付 ¥" + money(number(active,"amount")) + "？") != QMessageBox::Yes) return;
    command(this, api, "POST", "/orders/" + orderId + "/" + action, {},
            [this](const Reply &r) {
                active = r.data.object();
                me["balance"] = active.value("balance"); me["held_balance"] = active.value("held_balance");
                me["available_balance"] = active.value("available_balance");
                if (active.contains("vehicle_soc")) me["vehicle_soc"] = active.value("vehicle_soc");
                navigate("charging");
            });
}
void UserWindow::statistics() {
    body->addWidget(pageHeading("个人充电账单",this,[this]{navigate("home");}));
    auto period = new Segments({"本月账单","全部历史"},statsPeriod);
    connect(period,&Segments::changed,this,[this](int n){statsPeriod=n;navigate("stats");});
    body->addWidget(period);
    if (!api->authenticated()) {
        body->addWidget(button("登录查看账单",this,[this]{login();},true)); return;
    }
    auto host = new QWidget;
    statisticsHost = host;
    auto rows = new QVBoxLayout(host); rows->setContentsMargins(0,0,0,0);
    auto totals = label("正在读取个人账单…"); totals->setObjectName("personal-statistics");
    auto graph = new DataGraphic(DataGraphic::GroupedBars); graph->setFixedHeight(250);
    auto breakdown = new QWidget; auto details = new QVBoxLayout(breakdown);
    details->setContentsMargins(0,0,0,0);
    rows->addWidget(graph); rows->addWidget(totals); rows->addWidget(breakdown);
    rows->addWidget(label("按付款时间统计 · 北京时间 · 全量个人订单",muted));
    body->addWidget(host);
    auto generation = std::make_shared<int>(0);
    statisticsLoader = [=] {
        const int epoch = ++*generation;
        api->get(QString("/me/statistics?period=")+(statsPeriod ? "all":"month"),host,[=](const Reply &r) {
            if (epoch != *generation) return;
            if (!r.ok || r.cached) message(r);
            if (!r.ok) { totals->setText(r.error); return; }
            const auto data = r.data.object(); const auto daily = data.value("daily").toArray();
            double energy=0,amount=0; int count=0; QList<double> values; QStringList dates;
            for (auto v : daily) { auto o=v.toObject(); energy+=number(o,"energy_kwh");amount+=number(o,"amount");count+=int(number(o,"orders"));
                values<<number(o,"amount");dates<<text(o,"day").mid(5); }
            graph->setData({values},dates,"元");
            totals->setText(QString("%1 kWh    ¥%2    %3 笔已支付订单").arg(money(energy),money(amount)).arg(count));
            clearLayout(details);
            if (daily.isEmpty()) details->addWidget(label("暂无已支付账单，完成充电并付款后显示统计。",muted));
            auto stations=data.value("stations").toArray(); double max=0;
            for (auto v:stations) max=qMax(max,number(v.toObject(),"amount"));
            for (auto v:stations) {auto o=v.toObject();details->addWidget(amountBar(text(o,"station_name"),number(o,"amount"),max,QColor("#df87d8"),"元"));}
        });
    };
    loadStatistics();
    body->addWidget(button("查看订单与待支付账单",this,[this]{navigate("history");}));
}
void UserWindow::loadStatistics() {
    if (statisticsHost && statisticsLoader) statisticsLoader();
}
void UserWindow::schedule() {
    body->addWidget(pageHeading("站点分时计费",this,[this]{navigate(scheduleOrigin);}));
    auto choices=new QComboBox;
    for (auto v:stations) { auto o=v.toObject();choices->addItem(text(o,"name"),text(o,"id")); }
    choices->setCurrentIndex(choices->findData(selectedStation));
    body->addWidget(choices);
    connect(choices,&QComboBox::activated,this,[=](int){selectedStation=choices->currentData().toString();navigate("schedule");});
    QJsonObject station;
    for (auto v:stations) if (text(v.toObject(),"id")==selectedStation) station=v.toObject();
    if (station.isEmpty()) {body->addWidget(emptyPanel("暂无站点","请先刷新站点数据","pin"));return;}
    auto periods=station.value("tariff").toArray();
    if (periods.isEmpty()) periods.append(QJsonObject{{"start_hour",0},{"end_hour",24},{"electricity_price",station.value("unit_price")},{"service_price",0}});
    QList<double> prices(24,number(station,"unit_price"));
    for (auto v:periods) {auto p=v.toObject();for (int hour=int(number(p,"start_hour"));hour<int(number(p,"end_hour"))&&hour<24;++hour)
        if(hour>=0)prices[hour]=number(p,"electricity_price")+number(p,"service_price");}
    const double low=*std::min_element(prices.begin(),prices.end()), high=*std::max_element(prices.begin(),prices.end());
    auto period = new Segments({"全天","最低价","最高价"},tariffMode);
    body->addWidget(period);
    body->addWidget(row({valueBlock(money(low),"元/kWh","最低综合单价"),valueBlock(money(high),"元/kWh","最高综合单价")}));
    auto graph=new DataGraphic(DataGraphic::GroupedBars); graph->setFixedHeight(260);
    body->addWidget(graph);
    auto bands=new QWidget; auto rows=new QVBoxLayout(bands);rows->setContentsMargins(0,0,0,0);body->addWidget(bands);
    auto render=[=](int mode) {
        tariffMode=mode; QStringList hours; QList<double> selected;
        for(int hour=0;hour<24;++hour) {
            hours<<QString::number(hour);
            selected << ((mode==0 || (mode==1&&qFuzzyCompare(prices[hour]+1,low+1)) || (mode==2&&qFuzzyCompare(prices[hour]+1,high+1))) ? prices[hour] : 0);
        }
        graph->setData({selected},hours,"元/kWh"); clearLayout(rows);
        for(auto v:periods) {auto p=v.toObject();double price=number(p,"electricity_price")+number(p,"service_price");
            if((mode==1&&!qFuzzyCompare(price+1,low+1))||(mode==2&&!qFuzzyCompare(price+1,high+1)))continue;
            rows->addWidget(amountBar(QString("%1:00–%2:00").arg(int(number(p,"start_hour")),2,10,QChar('0')).arg(int(number(p,"end_hour")),2,10,QChar('0')),
                price,high,QColor(price==low?"#7ad8c8":"#d983e1"),"元/kWh"));
        }
    };
    connect(period,&Segments::changed,bands,render);render(tariffMode);
    body->addWidget(button("查看电费与服务费明细",this,[this,periods]{showDetail(this,"分时计费明细",{{"计费时段",periods}});}));
    body->addWidget(label("当前有效价格。预约时保存价格，跨时段分别计费；后续调价不影响已有订单。",muted));
}
void UserWindow::profile() {
    body->addWidget(pageHeading("个人中心", this, [this] { navigate("home"); }));
    QVBoxLayout *l;
    auto wallet = card("", &l);
    wallet->setStyleSheet(
        "QFrame#card{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #422158,stop:.5 "
        "#2b183d,stop:1 #1a112a);border:1px solid #684073;border-radius:20px;}");
    auto header = new QHBoxLayout;
    auto avatar = label(api->authenticated() ? text(me, "nickname", "E").left(1) : "E",
                        "font-size:23px;font-weight:600;background:#9455b4;border-radius:24px;");
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setFixedSize(48, 48);
    auto avatarData = QByteArray::fromBase64(text(me,"avatar_data","").toUtf8());
    if (!avatarData.isEmpty()) { QPixmap pix; pix.loadFromData(avatarData,"PNG"); if (!pix.isNull()) avatar->setPixmap(pix.scaled(48,48,Qt::KeepAspectRatio,Qt::SmoothTransformation)); }
    header->addWidget(avatar);
    auto identity = new QVBoxLayout;
    identity->setSpacing(4);
    identity->addWidget(
        label(api->authenticated() ? text(me, "nickname", "充电用户") : "欢迎来到 ELECTRA",
              "font-size:18px;font-weight:600;"));
    identity->addWidget(label(api->authenticated() ? text(me, "phone") : "让每一次出发，充满能量",
                              "font-size:11px;color:#baa1c9;"));
    header->addLayout(identity, 1);
    l->addLayout(header);
    l->addSpacing(18);
    if (!api->authenticated()) {
        l->addWidget(picture("ev-photo.png", 160));
        l->addWidget(button("手机号验证码登录", this, [this] { login(); }, true));
        body->addWidget(wallet);
        body->addWidget(
            emptyPanel("开启你的充电旅程", "登录后预约充电、查看账单与管理钱包。", "car"));
        body->addWidget(button("刷新网络连接", this, [this] { refresh(); }));
        return;
    }
    l->addWidget(label("钱包可用余额", "font-size:12px;color:#c1a7d0;"));
    l->addWidget(label("¥ " + money(me.contains("available_balance") ? number(me, "available_balance") : number(me,"balance")), "font-size:38px;font-weight:600;"));
    l->addWidget(label("总余额 ¥"+money(number(me,"balance"))+" · 冻结 ¥"+money(number(me,"held_balance")),muted));
    l->addWidget(button(
        "钱包充值", this,
        [this] {
            editForm(this, api, "钱包充值", "POST", "/wallet/recharges",
                     {{"amount", "充值金额（元）"}}, {{"idempotency_key", uid()}},
                     [this] { refresh(); });
        },
        true));
    body->addWidget(wallet);
    QVBoxLayout *menu;
    auto panel = card("账户与服务", &menu);
    menu->setSpacing(2);
    auto entry = [&](const QString &name, const QString &icon, std::function<void()> fn) {
        auto b = button(name + "     ›", this, fn);
        b->setIcon(appIcon(icon));
        b->setIconSize(QSize(20, 20));
        b->setStyleSheet("QPushButton{text-align:left;padding:17px "
                         "8px;background:transparent;border:0;border-bottom:1px solid "
                         "#382442;border-radius:0;}QPushButton:hover{background:#352041;}");
        menu->addWidget(b);
    };
    entry("个人资料", "users", [this] {
        editForm(this, api, "个人资料", "PATCH", "/me", {{"nickname", "昵称"}},
                 {{"nickname", me.value("nickname")}}, [this] { refresh(); });
    });
    entry("车辆绑定与电量", "car", [this] {
        editForm(this,api,"绑定车辆（账户同步）","PUT","/me/vehicle",
            {{"vehicle_name","车型"},{"vehicle_plate","车牌"},{"battery_kwh","电池容量 kWh"},{"vehicle_soc","当前电量 %"},{"charge_limit","默认充电上限 %"}},
            {{"vehicle_name",me.value("vehicle_name")},{"vehicle_plate",me.value("vehicle_plate")},{"battery_kwh",me.contains("battery_kwh")?me.value("battery_kwh"):QJsonValue(60)},
             {"vehicle_soc",batterySoc()},{"charge_limit",me.contains("charge_limit")?me.value("charge_limit"):QJsonValue(80)}},
            [this]{active={};refresh();});
    });
    entry("更换头像", "user", [this] {
        QImage currentAvatar;currentAvatar.loadFromData(QByteArray::fromBase64(text(me,"avatar_data").toLatin1()),"PNG");
        showAvatarEditor(this,api,[this](const QJsonObject &account){me=account;navigate("profile");},currentAvatar);
    });
    entry("申请提现", "chart", [this] {
        editForm(this,api,"申请提现（审核后模拟到账）","POST","/wallet/withdrawals",
            {{"amount","提现金额（元）"},{"destination","演示收款账户"}},{{"idempotency_key",uid()},{"destination","演示钱包"}},[this]{refresh();});
    });
    entry("提现进度", "chart", [this] {
        api->get("/me/withdrawals",this,[this](const Reply &r){if(r.ok)showDetail(this,"提现申请与审核记录",{{"提现记录",r.data.array()}});else message(r);});
    });
    entry("订单历史", "history", [this] { navigate("history"); });
    entry("钱包流水", "chart", [this] {
        auto d = new QDialog(this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->setWindowTitle("钱包流水");
        auto l = new QVBoxLayout(d);
        l->addWidget(dialogHeader(d, label("钱包收支", "font-size:22px;font-weight:600;")));
        auto scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto content = new QWidget;
        auto rows = new QVBoxLayout(content);
        for (auto v : ledger) {
            auto o = v.toObject();
            rows->addWidget(detailLine("chart", statusText(text(o, "entry_type")),
                                       "¥" + money(number(o, "amount"))));
            rows->addWidget(label(localDateTime(text(o, "created_at")), "font-size:10px;color:#957ba8;"));
        }
        if (ledger.isEmpty())
            rows->addWidget(emptyPanel("暂无钱包流水", "充值或结算后会在此记录。", "chart"));
        rows->addStretch();
        scroll->setWidget(content);
        l->addWidget(scroll);
        d->resize(440, 550);
        d->show();
    });
    entry("分时用电", "bolt", [this] { navigate("schedule"); });
    entry("刷新账户", "settings", [this] { refresh(); });
    body->addWidget(panel);
    auto logout = button("退出登录", this, [this] {
        api->logout();
        if (refreshContext) delete refreshContext;
        events.stop();
        me = {};
        active = {};
        orders = {};
        ledger = {};
        navigate("profile");
    });
    logout->setProperty("quiet", true);
    body->addWidget(logout);
}
void UserWindow::history() {
    body->addWidget(pageHeading("我的充电订单", this, [this] { navigate("profile"); }));
    if (!api->authenticated()) {
        body->addWidget(button("请先登录", this, [this] { login(); }, true));
        return;
    }
    auto filters = new Segments({"全部订单", "进行中", "已完成", "待支付"}, historyFilter);
    connect(filters, &Segments::changed, this, [this](int n) {
        historyFilter = n;
        navigate("history");
    });
    body->addWidget(filters);
    if (orders.isEmpty())
        body->addWidget(
            emptyPanel("还没有充电旅程", "完成首次充电后，这里会显示电量、费用和订单状态", "car"));
    for (const auto &v : orders) {
        auto o = v.toObject();
        if (historyFilter == 1 && text(o, "status") != "reserved" &&
            text(o, "status") != "charging")
            continue;
        if (historyFilter == 3 && text(o,"status") != "pending_payment") continue;
        if (historyFilter == 2 && text(o, "status") != "completed")
            continue;
        QVBoxLayout *l;
        auto box = card(text(o, "station_name"), &l);
        l->addWidget(label(statusText(text(o, "status")) + "   ·   " + text(o, "charger_code"),
                           "color:#d195df;"));
        l->addWidget(
            label(money(number(o, "energy_kwh")) + " kWh     ¥" + money(number(o, "amount")),
                  "font-size:20px;"));
        l->addWidget(label(localDateTime(text(o, "reserved_at")), muted));
        l->addWidget(button("查看订单", this, [=] {
            api->get("/orders/" + text(o, "id"), this, [this](const Reply &r) {
                if (r.ok) {
                    active = r.data.object();
                    navigate("charging");
                } else
                    message(r);
            });
        }));
        body->addWidget(box);
    }
    body->addWidget(label("最近200条订单", muted));
}
void UserWindow::login() {
    auto d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle("手机号登录");
    auto l = new QVBoxLayout(d);
    l->addWidget(dialogHeader(d, label("欢迎回来", "font-size:25px;")));
    auto phone = new QLineEdit;
    phone->setObjectName("phone");
    phone->setPlaceholderText("11位手机号");
    auto code = new QLineEdit;
    code->setObjectName("otp");
    code->setPlaceholderText("6位验证码");
    l->addWidget(phone);
    l->addWidget(code);
    auto err = label("验证码由服务器发送；开发环境可能返回测试验证码。", muted);
    l->addWidget(err);
    auto send = button("获取验证码", d, [=] {
        api->request("POST", "/auth/otp/request", {{"phone", phone->text()}}, d,
                     [=](const Reply &r) {
                         if (!r.ok) {
                             err->setText(r.error);
                             return;
                         }
                         auto dev = text(r.data.object(), "development_code", "");
                         err->setText(dev.isEmpty() ? "验证码已发送" : "开发验证码：" + dev);
                     });
    });
    l->addWidget(send);
    l->addWidget(button(
        "登录", d,
        [=] {
            api->request("POST", "/auth/otp/verify",
                         {{"phone", phone->text()}, {"code", code->text()}}, d,
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
    d->resize(400, 330);
    d->show();
}
