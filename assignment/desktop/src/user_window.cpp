#include "user_window.h"
#include <QJsonArray>
namespace {
QString big = "font-size:30px;font-weight:600;";
QString muted = "color:#b3a0c2;font-size:12px;";
} // namespace
UserWindow::UserWindow(ApiClient *a, CacheStore *c)
    : DesktopWindow(a, c, "ELECTRA · Qt 充电用户端") {
    // 构造导航栏，并建立会话、实时事件和充电轮询相关的信号槽。
    resize(460, 900);
    setMinimumSize(420, 640);
    nav->addWidget(label("▰ ELECTRA", "font-size:22px;font-weight:600;"));
    nav->addStretch();
    nav->addWidget(button("账户", this, [this] { navigate("profile"); }));
    auto footer = new QHBoxLayout;
    for (auto pair : QList<QPair<QString, QString>>{{"home", "首页"},
                                                    {"map", "找桩"},
                                                    {"charging", "充电"},
                                                    {"stats", "统计"},
                                                    {"profile", "我的"}}) {
        auto b = button(pair.second, this, [=] { navigate(pair.first); });
        b->setStyleSheet("QPushButton{padding:8px 9px;}");
        b->setCheckable(true);
        b->setObjectName(pair.first);
        footer->addWidget(b);
    }
    outer->insertLayout(2, footer);
    current = "home";
    // 会话失效信号：清空私有数据并引导用户重新登录。
    connect(api, &ApiClient::sessionExpired, this, [this] {
        me = {};
        active = {};
        orders = {};
        ledger = {};
        navigate("profile");
        connection->setText("会话已过期，请重新登录");
    });
    // 实时通道恢复时启动一次合并刷新，补齐断线期间可能遗漏的数据。
    connect(api, &ApiClient::streamChanged, this, [this](bool ready) {
        if (ready)
            events.start();
    });
    events.setSingleShot(true);
    events.setInterval(650);
    // timeout 直接连接 refresh 槽；单次定时器可合并短时间内的多条事件。
    connect(&events, &QTimer::timeout, this, &UserWindow::refresh);
    connect(api, &ApiClient::eventReceived, this, [this](const QJsonObject &) { events.start(); });
    poll.setInterval(3000);
    // 充电中每三秒拉取活动订单，实时更新电量、费用和状态标签。
    connect(&poll, &QTimer::timeout, this, [this] {
        if (api->authenticated() && !active.isEmpty() && text(active, "status") == "charging")
            api->get("/orders/" + text(active, "id"), this, [this](const Reply &r) {
                if (!r.ok) {
                    message(r);
                    return;
                }
                active = r.data.object();
                if (chargeEnergy)
                    chargeEnergy->setText(money(number(active, "energy_kwh")) + " kWh");
                if (chargeCost)
                    chargeCost->setText("¥" + money(number(active, "amount")));
                if (chargeState)
                    chargeState->setText(statusText(text(active, "status")));
                if (text(active, "status") != "charging")
                    refresh();
            });
    });
    poll.start();
    QTimer::singleShot(0, this, &UserWindow::refresh);
}
void UserWindow::refresh() {
    // 并行获取页面所需快照，待全部请求结束后再重建当前页面。
    if (refreshContext)
        refreshContext->deleteLater();
    auto ctx = new QObject(this);
    refreshContext = ctx;
    QStringList paths = {"/public/stations", "/public/analytics/urbanev"};
    if (api->authenticated())
        paths << "/me" << "/me/orders?limit=200" << "/me/wallet-entries?limit=200";
    auto pending = std::make_shared<int>(paths.size());
    for (int i = 0; i < paths.size(); ++i)
        api->get(paths[i], ctx, [=](const Reply &r) {
            message(r);
            if (r.ok) {
                if (i == 0)
                    stations = r.data.array();
                if (i == 1)
                    analytics = r.data.object();
                if (i == 2)
                    me = r.data.object();
                if (i == 3) {
                    orders = r.data.array();
                    active = {};
                    for (const auto &v : orders) {
                        auto o = v.toObject();
                        if (text(o, "status") == "reserved" || text(o, "status") == "charging") {
                            active = o;
                            break;
                        }
                    }
                }
                if (i == 4)
                    ledger = r.data.array();
            }
            if (--*pending == 0) {
                if (selectedStation.isEmpty() && !stations.isEmpty())
                    selectedStation = text(stations[0].toObject(), "id");
                navigate(current);
                ctx->deleteLater();
            }
        });
}
void UserWindow::navigate(const QString &page) {
    // 清理旧布局并按路由名调用页面构建函数。
    current = page;
    for (auto b : root->findChildren<QPushButton *>())
        if (b->isCheckable())
            b->setChecked(b->objectName() == page);
    clearLayout(body);
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
void UserWindow::home() {
    // 首页汇总车辆、电量、推荐站点和主要快捷操作。
    body->addWidget(label("MG-4 Luxury", "font-size:23px;font-weight:600;"));
    QVBoxLayout *l;
    auto hero = card("车辆能量", &l);
    l->addWidget(
        row({label("⚡ 60%\n展示电量", big), label("215.5 km\n展示续航", "font-size:19px;")}));
    l->addWidget(picture("ev-photo.png", 210));
    l->addWidget(
        label("车辆、电池与续航为展示值；实际费用由服务器结算。", "font-size:10px;color:#a187b2;"));
    body->addWidget(hero);
    QVBoxLayout *m;
    auto mapCard = card("附近充电站", &m);
    auto map = new ArtWidget(ArtWidget::Map);
    map->setMinimumHeight(220);
    map->setStations(stations, selectedStation);
    // 地图控件发出站点 ID 后保存选择并进入详情页。
    connect(map, &ArtWidget::stationSelected, this, [this](QString id) {
        selectedStation = id;
        navigate("station");
    });
    m->addWidget(map);
    m->addWidget(button("查看充电网络 →", this, [this] { navigate("map"); }, true));
    body->addWidget(mapCard);
}
void UserWindow::mapPage() {
    // 找桩页组合可点击地图、搜索框和动态过滤列表。
    body->addWidget(label("发现附近充电站", "font-size:24px;font-weight:600;"));
    auto map = new ArtWidget(ArtWidget::Map);
    map->setMinimumHeight(360);
    map->setStations(stations, selectedStation);
    connect(map, &ArtWidget::stationSelected, this, [this](QString id) {
        selectedStation = id;
        navigate("station");
    });
    body->addWidget(map);
    auto search = new QLineEdit;
    search->setPlaceholderText("搜索站点名称或地址");
    body->addWidget(search);
    auto list = new QWidget;
    auto ll = new QVBoxLayout(list);
    ll->setContentsMargins(0, 0, 0, 0);
    auto render = [=](const QString &query) {
        clearLayout(ll);
        int count = 0;
        for (const auto &v : stations) {
            auto o = v.toObject();
            if (!(text(o, "name") + text(o, "address")).contains(query, Qt::CaseInsensitive))
                continue;
            count++;
            auto b = button(text(o, "name") + "\n" +
                                QString("%1 / %2 空闲接口    ¥%3 / kWh")
                                    .arg(number(o, "available_count"))
                                    .arg(number(o, "charger_count"))
                                    .arg(money(number(o, "unit_price"))),
                            list, [=] {
                                selectedStation = text(o, "id");
                                navigate("station");
                            });
            b->setStyleSheet("QPushButton{text-align:left;padding:16px;}");
            ll->addWidget(b);
        }
        if (!count)
            ll->addWidget(label("没有匹配的站点"));
    };
    // textChanged 信号每次输入变化都调用 render，实现即时过滤。
    connect(search, &QLineEdit::textChanged, list, render);
    render("");
    body->addWidget(list);
}
void UserWindow::stationPage() {
    // 展示选中站点、充电枪状态，并发起预约等服务端操作。
    QJsonObject station;
    for (const auto &v : stations)
        if (text(v.toObject(), "id") == selectedStation)
            station = v.toObject();
    body->addWidget(
        row({button("‹ 返回", this, [this] { navigate("map"); }),
             label(text(station, "name", "请选择站点"), "font-size:20px;font-weight:600;")},
            {1, 3}));
    if (station.isEmpty())
        return;
    QVBoxLayout *life;
    auto energy = card("能量生活", &life);
    auto car = new ArtWidget(ArtWidget::TopCar);
    car->setMinimumHeight(170);
    life->addWidget(car);
    life->addWidget(row(
        {label("40%\n展示电量", "font-size:18px;"), label("134 km\n展示续航", "font-size:18px;")}));
    auto limit = new QComboBox;
    for (int n : {70, 80, 90, 100})
        limit->addItem(QString("展示充电上限：%1%").arg(n), n);
    limit->setCurrentIndex(
        qMax(0, limit->findData(cache->preference("charge_limit", "70").toInt())));
    // activated 只响应用户主动选择，将充电上限保存为本机偏好。
    connect(limit, &QComboBox::activated, this,
            [=](int i) { cache->setPreference("charge_limit", limit->itemData(i).toString()); });
    life->addWidget(limit);
    body->addWidget(energy);
    QVBoxLayout *detail;
    auto box = card("选择充电接口", &detail);
    detail->addWidget(label(text(station, "address"), muted));
    detail->addWidget(label("¥" + money(number(station, "unit_price")) + " / kWh", big));
    auto host = new QWidget;
    auto hl = new QVBoxLayout(host);
    hl->setContentsMargins(0, 0, 0, 0);
    detail->addWidget(host);
    body->addWidget(box);
    api->get("/public/stations/" + selectedStation + "/chargers", host, [=](const Reply &r) {
        message(r);
        if (!r.ok) {
            hl->addWidget(label(r.error));
            return;
        }
        chargers = r.data.array();
        if (chargers.isEmpty())
            hl->addWidget(label("暂无接口"));
        for (const auto &v : chargers) {
            auto o = v.toObject();
            QString key = uid();
            auto b = button(
                text(o, "code") + "  ·  " + QString::number(number(o, "power_kw")) + " kW  ·  " +
                    statusText(text(o, "status")),
                host,
                [=] {
                    if (!api->authenticated()) {
                        login();
                        return;
                    }
                    command(this, api, "POST", "/orders",
                            {{"charger_id", text(o, "id")}, {"idempotency_key", key}},
                            [this](const Reply &r) {
                                active = r.data.object();
                                navigate("charging");
                            });
                },
                true);
            b->setEnabled(text(o, "status") == "available" && !r.cached);
            hl->addWidget(b);
        }
    });
}
void UserWindow::charging() {
    // 根据活动订单状态显示预约信息或实时充电控制面板。
    body->addWidget(label("充电中心", "font-size:23px;font-weight:600;"));
    QVBoxLayout *l;
    auto box = card(active.isEmpty() ? "准备充电" : text(active, "station_name"), &l);
    auto ring = new ArtWidget(ArtWidget::Ring);
    ring->setMinimumHeight(310);
    ring->setValue(36, "车辆电池展示值");
    l->addWidget(ring);
    if (active.isEmpty()) {
        l->addWidget(label("暂无活动订单。选择可用电桩开始预约。", muted));
        l->addWidget(button("寻找充电站", this, [this] { navigate("map"); }, true));
    } else {
        chargeState = label(statusText(text(active, "status")), "font-size:18px;color:#e8a7e0;");
        l->addWidget(chargeState);
        chargeEnergy = label(money(number(active, "energy_kwh")) + " kWh", "font-size:22px;");
        chargeCost = label("¥" + money(number(active, "amount")), "font-size:22px;");
        l->addWidget(row({chargeEnergy, chargeCost}));
        l->addWidget(label("实际订单电量                 当前费用", muted));
        auto s = text(active, "status");
        if (s == "reserved") {
            l->addWidget(button("⚡ 开始充电", this, [this] { orderCommand("start"); }, true));
            l->addWidget(button("取消预约", this, [this] { orderCommand("cancel"); }));
        } else if (s == "charging")
            l->addWidget(button("停止充电并结算", this, [this] { orderCommand("stop"); }, true));
        else
            l->addWidget(button("查看订单历史", this, [this] { navigate("history"); }));
    }
    body->addWidget(box);
    body->addWidget(label("订单电量按服务端功率与时间模拟，费用与状态以服务端为准。", muted));
}
void UserWindow::orderCommand(const QString &action) {
    // 向当前活动订单发送开始、停止或取消命令，成功后刷新权威状态。
    if (active.isEmpty())
        return;
    command(this, api, "POST", "/orders/" + text(active, "id") + "/" + action, {},
            [this](const Reply &r) {
                active = r.data.object();
                navigate("charging");
            });
}
void UserWindow::statistics() {
    // 聚合历史订单的费用、电量及趋势；未登录时显示登录提示。
    body->addWidget(label("充电统计", "font-size:24px;font-weight:600;"));
    if (!api->authenticated()) {
        body->addWidget(label("登录后查看真实订单统计", muted));
        body->addWidget(button("登录账户", this, [this] { login(); }, true));
        return;
    }
    double energy = 0, amount = 0;
    QMap<QString, double> days;
    for (const auto &v : orders) {
        auto o = v.toObject();
        if (text(o, "status") != "completed")
            continue;
        energy += number(o, "energy_kwh");
        amount += number(o, "amount");
        days[text(o, "ended_at").left(10)] += number(o, "energy_kwh");
    }
    QVBoxLayout *l;
    auto box = card("充电使用情况", &l);
    l->addWidget(row({label(money(energy) + " kWh\n累计电量", "font-size:22px;"),
                      label("¥" + money(amount) + "\n累计费用", "font-size:22px;")}));
    auto spark = new ArtWidget(ArtWidget::Spark);
    spark->setValues(days.values());
    spark->setMinimumHeight(210);
    l->addWidget(spark);
    l->addWidget(label("按有记录日期统计 · 最近200条订单 · 仅已完成订单", muted));
    body->addWidget(box);
    QVBoxLayout *cost;
    auto c = card("站点费用概览", &cost);
    QMap<QString, double> amounts;
    for (auto v : orders) {
        auto o = v.toObject();
        if (text(o, "status") == "completed")
            amounts[text(o, "station_name")] += number(o, "amount");
    }
    for (auto it = amounts.begin(); it != amounts.end(); ++it)
        cost->addWidget(label(it.key() + "    ¥" + money(it.value())));
    if (amounts.isEmpty())
        cost->addWidget(label("暂无已完成订单"));
    body->addWidget(c);
    body->addWidget(button("查看历史时段电价", this, [this] { navigate("schedule"); }));
}
void UserWindow::schedule() {
    // 绘制服务端提供的 24 小时用能曲线和分时建议。
    body->addWidget(label("充电时段", "font-size:24px;font-weight:600;"));
    body->addWidget(label("UrbanEV 历史典型日电价 · 不作为当前站点计费依据", muted));
    QVBoxLayout *l;
    auto box = card("24 小时电价参考", &l);
    for (const auto &v : analytics.value("hourly_profile").toArray()) {
        auto o = v.toObject();
        auto s = QString("%1:00 — %2:00\n电价 ¥%3 · 服务费 ¥%4")
                     .arg(int(number(o, "hour")), 2, 10, QChar('0'))
                     .arg((int(number(o, "hour")) + 1) % 24, 2, 10, QChar('0'))
                     .arg(money(number(o, "electricity_price")), money(number(o, "service_price")));
        auto item =
            label(s, "padding:15px;border-left:3px solid #b978de;border-bottom:1px solid #39233f;");
        l->addWidget(item);
    }
    body->addWidget(box);
}
void UserWindow::profile() {
    // 账户页展示资料、钱包余额与账本，并提供充值和资料修改入口。
    body->addWidget(label("我的账户", "font-size:25px;font-weight:600;"));
    if (!api->authenticated()) {
        body->addWidget(picture("ev-photo.png", 220));
        body->addWidget(label("登录后预约充电、查询账单与管理钱包。", muted));
        body->addWidget(button("手机号验证码登录", this, [this] { login(); }, true));
        body->addWidget(button("刷新网络连接", this, [this] { refresh(); }));
        return;
    }
    QVBoxLayout *l;
    auto box = card(text(me, "nickname", "充电用户"), &l);
    l->addWidget(label(text(me, "phone"), muted));
    l->addWidget(label("¥" + money(number(me, "balance")), "font-size:36px;font-weight:600;"));
    l->addWidget(label("钱包可用余额", muted));
    l->addWidget(button(
        "钱包充值", this,
        [this] {
            editForm(this, api, "钱包充值", "POST", "/wallet/recharges",
                     {{"amount", "充值金额（元）"}}, {{"idempotency_key", uid()}},
                     [this] { refresh(); });
        },
        true));
    body->addWidget(box);
    body->addWidget(button("编辑昵称", this, [this] {
        editForm(this, api, "个人资料", "PATCH", "/me", {{"nickname", "昵称"}},
                 {{"nickname", me.value("nickname")}}, [this] { refresh(); });
    }));
    body->addWidget(button("订单历史 →", this, [this] { navigate("history"); }));
    body->addWidget(button("钱包流水 →", this, [this] {
        auto d = new QDialog(this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->setWindowTitle("钱包流水");
        auto l = new QVBoxLayout(d);
        for (auto v : ledger) {
            auto o = v.toObject();
            l->addWidget(label(statusText(text(o, "entry_type")) + "  ¥" +
                               money(number(o, "amount")) + "\n" + text(o, "created_at")));
        }
        if (ledger.isEmpty())
            l->addWidget(label("暂无流水"));
        d->resize(450, 600);
        d->show();
    }));
    body->addWidget(button("刷新账户", this, [this] { refresh(); }));
    body->addWidget(button("退出登录", this, [this] {
        api->logout();
        me = {};
        active = {};
        orders = {};
        ledger = {};
        navigate("profile");
    }));
}
void UserWindow::history() {
    // 按时间列出历史订单，支持继续请求单条订单详情。
    body->addWidget(label("充电订单历史", "font-size:24px;font-weight:600;"));
    if (!api->authenticated()) {
        body->addWidget(button("请先登录", this, [this] { login(); }, true));
        return;
    }
    if (orders.isEmpty())
        body->addWidget(label("暂无充电记录", muted));
    for (const auto &v : orders) {
        auto o = v.toObject();
        QVBoxLayout *l;
        auto box = card(text(o, "station_name"), &l);
        l->addWidget(label(statusText(text(o, "status")) + "   ·   " + text(o, "charger_code"),
                           "color:#d195df;"));
        l->addWidget(
            label(money(number(o, "energy_kwh")) + " kWh     ¥" + money(number(o, "amount")),
                  "font-size:20px;"));
        l->addWidget(label(text(o, "reserved_at"), muted));
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
    // 两阶段验证码登录：先申请 OTP，再提交验证码换取 JWT 会话。
    auto d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle("手机号登录");
    auto l = new QVBoxLayout(d);
    l->addWidget(label("欢迎回来", "font-size:25px;"));
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
