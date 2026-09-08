#include "admin_window.h"
#include <QJsonArray>
#include <QSaveFile>
#include <QtCharts>

namespace {
const QStringList colors = {"#ce7bda", "#efb95d", "#9161cf", "#7ad8c8"};
QChart *baseChart(const QString &title) {
    // 创建具有统一透明背景、标题和图例配色的图表基础对象。
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
    // 包装图表并开启抗锯齿，确保在布局中拥有稳定的最小高度。
    auto w = new QChartView(c);
    w->setRenderHint(QPainter::Antialiasing);
    w->setStyleSheet("background:transparent;border:0;");
    w->setMinimumHeight(height);
    w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return w;
}
QWidget *donut(const QMap<QString, double> &values, const QString &title) {
    // 将分类数值构造成环形饼图，各扇区循环使用主题色。
    auto c = baseChart(title);
    auto s = new QPieSeries;
    s->setHoleSize(.72);
    int i = 0;
    for (auto it = values.begin(); it != values.end(); ++it) {
        auto slice = s->append(it.key() + QString(" · %1").arg(it.value()), it.value());
        slice->setBrush(QColor(colors[i++ % colors.size()]));
        slice->setPen(Qt::NoPen);
    }
    c->addSeries(s);
    return view(c);
}
QWidget *metricCard(const QString &caption, const QString &value, const QString &hint) {
    // 生成 Dashboard 顶部统一样式的指标卡。
    QVBoxLayout *l;
    auto w = card(caption, &l);
    l->addWidget(label(value, "font-size:28px;font-weight:600;"));
    l->addWidget(label(hint, "color:#af98bd;font-size:11px;"));
    return w;
}
void exportRecords(QWidget *parent, const QJsonArray &rows) {
    // 将当前 JSON 记录安全转义后导出为带 UTF-8 BOM 的 CSV，便于 Excel 打开中文。
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
    // 把服务端审计动作代码翻译成用户可读的中文名称。
    static QMap<QString, QString> map = {
        {"order.reserve", "订单已预约"},       {"order.start", "开始充电"},
        {"order.stop", "充电已结算"},          {"order.cancel", "预约已取消"},
        {"wallet.adjustment", "余额已调整"},   {"wallet.refund", "退款已处理"},
        {"console.settings", "运营设置已更新"}};
    return map.value(a, a.replace('.', " · "));
}
} // namespace
AdminWindow::AdminWindow(ApiClient *a, CacheStore *c)
    : DesktopWindow(a, c, "ELECTRA · Qt 运营管理中心") {
    // 构造管理员导航、工具栏，并连接实时事件、会话和刷新定时器。
    resize(1460, 1000);
    setMinimumSize(1060, 720);
    nav->addWidget(label("▰ ELECTRA", "font-size:27px;font-weight:600;"));
    nav->addStretch();
    for (auto pair : QList<QPair<QString, QString>>{{"dashboard", "运营总览"},
                                                    {"station", "站点导航"},
                                                    {"trips", "充电订单"},
                                                    {"history", "操作审计"},
                                                    {"forecast", "负荷预测"}}) {
        auto b = button(pair.second, this, [=] { navigate(pair.first); });
        b->setCheckable(true);
        b->setObjectName(pair.first);
        nav->addWidget(b);
    }
    nav->addStretch();
    nav->addWidget(button("刷新", this, [this] {
        if (api->authenticated())
            refresh();
        else
            login();
    }));
    nav->addWidget(button("设置", this, [this] { settings(); }));
    auto tools = new QHBoxLayout;
    for (auto pair : QList<QPair<QString, QString>>{
             {"users", "用户"}, {"stations", "站点"}, {"chargers", "电桩"}, {"orders", "订单管理"}})
        tools->addWidget(button(pair.second, this, [=] { manager(pair.first); }));
    tools->addStretch();
    tools->addWidget(button("Web 大屏 ↗", this, [this] { openUrl("/ui/dashboard.html"); }));
    outer->insertLayout(2, tools);
    current = "dashboard";
    body->addWidget(label("正在连接管理员服务…"));
    // WebSocket 恢复后安排完整刷新，补齐离线期间的变更。
    connect(api, &ApiClient::streamChanged, this, [this](bool ready) {
        if (ready)
            refreshTimer.start();
    });
    refreshTimer.setSingleShot(true);
    refreshTimer.setInterval(700);
    // timeout 信号直接调用 refresh 槽；单次定时器用来合并密集事件。
    connect(&refreshTimer, &QTimer::timeout, this, &AdminWindow::refresh);
    connect(api, &ApiClient::eventReceived, this,
            [this](const QJsonObject &) { refreshTimer.start(); });
    // JWT 失效信号显示提示并打开管理员手动登录框。
    connect(api, &ApiClient::sessionExpired, this, [this] {
        connection->setText("会话已过期，请重新登录");
        manualLogin();
    });
    QTimer::singleShot(0, this, &AdminWindow::login);
}
void AdminWindow::login() {
    // 优先尝试开发环境模拟管理员登录，失败后提供账号密码入口。
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
    // 收集管理员凭据并换取 JWT；凭据和令牌均不写入本地配置。
    auto d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle("管理员登录");
    auto l = new QVBoxLayout(d);
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
    // 并行拉取各业务数据集，所有请求完成后重绘当前路由。
    if (!api->authenticated())
        return;
    if (refreshContext)
        refreshContext->deleteLater();
    auto ctx = new QObject(this);
    refreshContext = ctx;
    auto remaining = std::make_shared<int>(8);
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
                if (i == 1)
                    chargers = r.data.array();
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
                if (selectedStation.isEmpty() && !stations.isEmpty())
                    selectedStation = text(stations[0].toObject(), "id");
                navigate(current);
                if (!failures->isEmpty())
                    connection->setText("部分数据未更新：" + failures->join("；"));
                ctx->deleteLater();
            }
        });
}
void AdminWindow::navigate(const QString &page) {
    // 管理端路由器：检查登录、清理旧布局并构建目标页面。
    current = page;
    for (auto b : root->findChildren<QPushButton *>())
        if (b->isCheckable())
            b->setChecked(b->objectName() == page);
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
    // 运营总览组合核心指标、地图、排行和状态分布图。
    QVBoxLayout *v, *g, *m;
    auto vehicle = card("我的车辆", &v);
    v->addWidget(label(text(preferences, "vehicle", "Tesla M-3"), "color:#c5afd4;"));
    auto car = new ArtWidget(ArtWidget::Car);
    car->setMinimumHeight(240);
    v->addWidget(car, 1);
    v->addWidget(label("车型与目标为演示配置 · 不参与计费", "color:#9e82b0;font-size:10px;"));
    v->addWidget(row({label("⚡ 60%\n展示电量", "font-size:21px;"),
                      label("215.5 km\n展示续航", "font-size:21px;")}));
    auto goal = card("充电目标", &g);
    auto gauge = new ArtWidget(ArtWidget::Gauge);
    gauge->setValue(number(preferences, "daily_goal"), "每日目标进度");
    g->addWidget(gauge, 1);
    g->addWidget(label(QString("每日 %1%     每周 %2%")
                           .arg(number(preferences, "daily_goal"))
                           .arg(number(preferences, "weekly_goal")),
                       "font-size:16px;color:#ddafd9;"));
    g->addWidget(button("调整展示目标", this, [this] { settings(); }));
    auto mapCard = card("附近充电站", &m);
    auto map = new ArtWidget(ArtWidget::Map);
    map->setStations(stations, selectedStation);
    m->addWidget(map, 1);
    // 地图选站信号更新当前站点并跳到站点导航页。
    connect(map, &ArtWidget::stationSelected, this, [this](const QString &id) {
        selectedStation = id;
        navigate("station");
    });
    m->addWidget(button("查看全部站点 →", this, [this] { navigate("station"); }));
    body->addWidget(row({vehicle, goal, mapCard}, {1, 1, 1}));
    QVBoxLayout *stats, *list;
    auto s = card("充电需求统计", &stats);
    QList<double> values;
    auto daily = analytics.value("daily").toArray();
    for (const auto &d : daily)
        values << number(d.toObject(), "energy");
    auto flow = new ArtWidget(ArtWidget::Flow);
    flow->setMinimumHeight(230);
    flow->setValues({79, 43, 86});
    stats->addWidget(flow);
    stats->addWidget(label("演示分组统计 · 10月 / 11月 / 12月", "color:#ad91be;font-size:11px;"));
    auto nearby = card("站点网络", &list);
    int count = 0;
    for (const auto &r : stations) {
        if (count++ >= 4)
            break;
        auto o = r.toObject();
        auto b = button(text(o, "name") + QString("     %1 桩   ↗").arg(number(o, "charger_count")),
                        this, [=] {
                            selectedStation = text(o, "id");
                            navigate("station");
                        });
        list->addWidget(b);
    }
    list->addWidget(label(QString("共 %1 个业务站点 · 历史数据单独统计").arg(stations.size()),
                          "color:#ae93be;"));
    list->addStretch();
    body->addWidget(row({s, nearby}, {3, 2}));
}
void AdminWindow::stationsPage() {
    // 同步展示站点地图、选择器、详情及相关充电枪。
    body->addWidget(label("站点导航", "font-size:26px;font-weight:600;"));
    QVBoxLayout *left, *right;
    auto a = card("车辆与站点", &left);
    a->setMaximumWidth(370);
    auto car = new ArtWidget(ArtWidget::Car);
    car->setMinimumHeight(400);
    left->addWidget(car);
    left->addWidget(label("定位与道路为站点空间展示；不提供真实路径导航。", "color:#ac8dbb;"));
    auto b = card("深圳充电网络", &right);
    auto map = new ArtWidget(ArtWidget::Map);
    map->setMinimumHeight(420);
    map->setStations(stations, selectedStation);
    right->addWidget(map);
    connect(map, &ArtWidget::stationSelected, this, [this](QString id) {
        selectedStation = id;
        navigate("station");
    });
    auto select = new QComboBox;
    for (const auto &v : stations) {
        auto o = v.toObject();
        select->addItem(text(o, "name"), text(o, "id"));
    }
    select->setCurrentIndex(qMax(0, select->findData(selectedStation)));
    // activated 读取 itemData 中的站点 ID，而不是依赖易变化的显示名称。
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
    // 订单页实现关键字搜索、状态筛选、分页、详情和管理员代操作。
    body->addWidget(label("充电订单", "font-size:26px;font-weight:600;"));
    double energy = 0, amount = 0;
    QMap<QString, double> counts;
    for (const auto &v : orders) {
        auto o = v.toObject();
        energy += number(o, "energy_kwh");
        amount += number(o, "amount");
        counts[statusText(text(o, "status"))]++;
    }
    QVBoxLayout *journey;
    auto journeyCard = card("最近充电行程", &journey);
    auto map = new ArtWidget(ArtWidget::Map);
    map->setStations(stations, selectedStation);
    journey->addWidget(map);
    journey->addWidget(
        label(orders.isEmpty() ? "暂无充电记录" : text(orders[0].toObject(), "station_name"),
              "font-size:18px;"));
    body->addWidget(
        row({journeyCard, donut(counts, "订单状态分布"),
             metricCard("当前页累计电量", money(energy) + " kWh", "累计金额 ¥" + money(amount))},
            {2, 1, 1}));
    auto search = new QLineEdit(orderSearch);
    search->setPlaceholderText("搜索站点、手机号、订单编号，回车查询");
    // 搜索框回车后保存条件、重置分页并重新构建页面。
    connect(search, &QLineEdit::returnPressed, this, [=] {
        orderSearch = search->text();
        navigate("trips");
    });
    auto filter = new QComboBox;
    filter->addItem("全部状态", "all");
    for (auto s : {"reserved", "charging", "completed", "cancelled"})
        filter->addItem(statusText(s), s);
    filter->setCurrentIndex(filter->findData(orderFilter));
    // 筛选项变化后从第一页开始，避免保留旧条件的偏移量。
    connect(filter, &QComboBox::activated, this, [=](int i) {
        orderFilter = filter->itemData(i).toString();
        navigate("trips");
    });
    body->addWidget(
        row({search, filter, button("导出当前页", this, [this] { exportRecords(this, orders); })},
            {3, 1, 1}));
    QVBoxLayout *list;
    auto box = card("订单明细 · 当前页最多 200 条", &list);
    int count = 0;
    for (const auto &v : orders) {
        auto o = v.toObject();
        if (orderFilter != "all" && text(o, "status") != orderFilter)
            continue;
        if (!QJsonDocument(o).toJson().toLower().contains(orderSearch.toLower().toUtf8()))
            continue;
        ++count;
        auto info = label(text(o, "station_name") + "   ·   " + text(o, "charger_code") + "\n" +
                              text(o, "phone") + "   " + text(o, "reserved_at"),
                          "font-size:13px;");
        auto status = label(statusText(text(o, "status")), "color:#c992e1;");
        auto cost = label(money(number(o, "energy_kwh")) + " kWh\n¥" + money(number(o, "amount")));
        auto actions = new QWidget;
        auto al = new QHBoxLayout(actions);
        al->addWidget(button("详情", this, [=] {
            api->get("/admin/orders/" + text(o, "id"), this, [=](const Reply &r) {
                if (r.ok)
                    showDetail(this, "订单详情", r.data.object());
                else
                    message(r);
            });
        }));
        auto state = text(o, "status");
        if (state == "reserved" || state == "charging") {
            QString action = state == "reserved" ? "start" : "stop";
            al->addWidget(button(
                state == "reserved" ? "开始" : "结算", this,
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
        list->addWidget(row({info, status, cost, actions}, {4, 1, 1, 2}));
    }
    if (!count)
        list->addWidget(label("没有匹配的充电订单"));
    body->addWidget(box);
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
    // 展示操作审计记录，支持查看原始详情、导出和分页。
    body->addWidget(label("操作审计", "font-size:26px;font-weight:600;"));
    QMap<QString, double> counts;
    for (const auto &v : logs)
        counts[text(v.toObject(), "action").section('.', 0, 0)]++;
    QVBoxLayout *timeline;
    auto t = card("操作时间线 · UTC", &timeline);
    for (const auto &v : logs) {
        auto o = v.toObject();
        auto item = button("●  " + actionLabel(text(o, "action")) + "\n" + text(o, "created_at") +
                               "  ·  " + text(o, "target_type"),
                           this, [=] { showDetail(this, "完整审计记录", o); });
        item->setStyleSheet("QPushButton{text-align:left;padding:18px;background:#251731;border:"
                            "1px solid #402647;border-left:3px solid #c17ed9;border-radius:10px;}");
        timeline->addWidget(item);
    }
    if (logs.isEmpty())
        timeline->addWidget(label("暂无操作记录"));
    QVBoxLayout *side;
    auto s = card("操作分析", &side);
    side->addWidget(donut(counts, "当前页操作构成"));
    side->addWidget(
        metricCard("审计记录", QString::number(logs.size()), "当前加载记录 · 不补造事件"));
    side->addWidget(button("导出审计 CSV", this, [this] { exportRecords(this, logs); }));
    side->addStretch();
    body->addWidget(row({t, s}, {3, 2}));
    body->addWidget(row({button("上一页", this,
                                [this] {
                                    offsetLogs = qMax(0, offsetLogs - 200);
                                    refresh();
                                }),
                         label(QString("偏移 %1").arg(offsetLogs)), button("下一页", this, [this] {
                             if (logs.size() == 200) {
                                 offsetLogs += 200;
                                 refresh();
                             }
                         })}));
}
void AdminWindow::forecastPage() {
    // 获取区域负荷预测，并用 Qt Charts 绘制预测曲线和评估指标。
    body->addWidget(label("负荷预测", "font-size:26px;font-weight:600;"));
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
        // 区域下拉框变化时更新作用域并重新请求预测。
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
    // 通过通用表单编辑服务端运营配置，成功后刷新快照。
    editForm(this, api, "运营展示设置", "PUT", "/admin/console/settings",
             {{"daily_goal", "每日目标 %"},
              {"weekly_goal", "每周目标 %"},
              {"charge_limit", "充电上限 %"},
              {"vehicle", "车型名称"}},
             preferences, [this] { refresh(); });
}
void AdminWindow::manager(const QString &kind) {
    // 通用 CRUD 对话框按 kind 复用用户、站点、充电枪和订单管理流程。
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
    layout->addWidget(title);
    auto table = new QTableWidget;
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    layout->addWidget(table, 1);
    auto tools = new QHBoxLayout;
    layout->addLayout(tools);
    auto items = std::make_shared<QJsonArray>();
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
                kind == "users"      ? QStringList{"nickname", "phone", "balance", "status"}
                : kind == "stations" ? QStringList{"name", "address", "unit_price", "charger_count"}
                : kind == "chargers"
                    ? QStringList{"station_name", "code", "kind", "power_kw", "status"}
                    : QStringList{"station_name", "charger_code", "phone", "status", "amount"};
            QMap<QString, QString> names = {{"nickname", "昵称"},     {"phone", "手机号"},
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
                        new QTableWidgetItem(statusText(text((*items)[i].toObject(), columns[j]))));
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
            title->setText(
                QString("运营数据管理 · %1 条 · 偏移 %2").arg(items->size()).arg(*offset));
        });
    };
    // Form callbacks hold only the dialog-owned refresh function; release on close.
    // 对话框关闭时清空自引用加载函数，打破捕获链并释放资源。
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
                          {"unit_price", "单价 元/kWh"}};
                values = {{"longitude", "114.05"}, {"latitude", "22.55"}, {"unit_price", "1.20"}};
            }
            if (kind == "chargers") {
                fields = {{"station_id", "所属站点 UUID"},
                          {"code", "电桩编号"},
                          {"kind", "类型 fast / slow"},
                          {"power_kw", "功率 kW"}};
                values = {{"station_id", selectedStation}, {"kind", "fast"}, {"power_kw", "120"}};
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
    if (kind == "users") {
        tools->addWidget(button("冻结 / 解冻", d, [=] {
            auto o = selection();
            if (o.isEmpty())
                return;
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
                 {"start", "开始"}, {"cancel", "取消"}, {"stop", "结算"}})
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
