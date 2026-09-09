#include "user_window.h"
#include "station_map.h"
#include "station_recommendations.h"
#include "visuals.h"
#include "charger_picker.h"
#include "avatar_editor.h"
#include <QJsonArray>
#include <QBuffer>
#include <algorithm>
#include <cmath>
namespace {
QString big = "font-size:30px;font-weight:600;";
QString muted = "color:#b3a0c2;font-size:12px;";
} // namespace
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
    auto statusFilter = new QComboBox;
    statusFilter->setObjectName("charger-status-filter");
    statusFilter->addItem("全部状态", "");
    statusFilter->addItem("空闲", "available");
    statusFilter->addItem("充电中", "charging");
    statusFilter->addItem("已预约", "reserved");
    statusFilter->addItem("故障/停用", "faulted");
    auto kindFilter = new QComboBox;
    kindFilter->setObjectName("charger-kind-filter");
    kindFilter->addItem("全部快慢充", "");
    kindFilter->addItem("快充", "fast");
    kindFilter->addItem("慢充", "slow");
    auto filters = new QWidget;
    filters->setObjectName("charger-filters");
    auto filterLayout = new QHBoxLayout(filters);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(8);
    filterLayout->addWidget(limit, 1);
    filterLayout->addWidget(statusFilter, 1);
    filterLayout->addWidget(kindFilter, 1);
    selectionLayout->addWidget(filters);
    auto picker=new ChargerPicker;selectionLayout->addWidget(picker);
    auto pickerState=label("","font-size:11px;color:#bca8ca;");selectionLayout->addWidget(pickerState);
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
    auto tariffRow = new QHBoxLayout;
    tariffRow->addStretch();
    auto tariff = button("查看分时价格", this, [this] { navigate("schedule"); });
    tariff->setProperty("quiet", true);
    tariff->setMinimumHeight(30);
    tariff->setMaximumWidth(128);
    tariff->setStyleSheet("padding:5px 10px;font-size:11px;min-height:20px;");
    tariffRow->addWidget(tariff);
    el->addLayout(tariffRow);
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

    auto hint = label("", "font-size:10px;color:#ef9db4;");
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
    auto applyPickerFilters = [=] {
        picker->setChargers(*previous, *readonly, false,
                            statusFilter->currentData().toString(), kindFilter->currentData().toString());
    };
    connect(statusFilter, &QComboBox::currentIndexChanged, picker, [=](int) { applyPickerFilters(); });
    connect(kindFilter, &QComboBox::currentIndexChanged, picker, [=](int) { applyPickerFilters(); });
    auto load=[=]{
        if(*pending)return;
        *pending=true;
        api->get("/public/stations/"+stationId+"/chargers",box,[=](const Reply &r){
            *pending=false;
            if(!r.ok){*readonly=true;applyPickerFilters();pickerState->setText("状态读取失败，暂不可预约："+r.error);return;}
            const auto items=r.data.array();
            if(items!=*previous || r.cached!=*readonly){*previous=items;*readonly=r.cached;applyPickerFilters();}
            pickerState->setText(r.cached?"离线缓存，暂不可预约":items.isEmpty()?"本站尚未配置电桩":"");
        });
    };
    auto timer=new QTimer(box);connect(timer,&QTimer::timeout,box,load);timer->start(1000);load();

}
void UserWindow::charging() {
    body->addWidget(pageHeading("充电中枢", this, [this] { navigate(chargingOrigin); }));
    QVBoxLayout *l;
    const auto chargingTitle = text(active,"station_name") + " · 电桩 " + text(active,"charger_code");
    auto box = card(active.isEmpty() ? "车辆待充电" : text(active, "status") == "pending_payment" ? "充电账单 · 待支付" : chargingTitle, &l);
    box->setStyleSheet(
        "QFrame#card{background:qradialgradient(cx:.5,cy:.4,radius:.8,fx:.5,fy:.4,stop:0 "
        "#36155d,stop:.6 #26113f,stop:1 #1b112b);border:1px solid #50325f;border-radius:24px;}");
    l->setContentsMargins(17, 14, 17, 14);
    l->setSpacing(9);
    auto ring = new ArtWidget(ArtWidget::Ring);
    ring->setFixedHeight(260);
    batteryArt = ring;
    ring->setValue(batterySoc(), "模拟车辆电量");
    l->addWidget(ring);
    l->addStretch(1);
    if (active.isEmpty()) {
        l->addWidget(label("尚未连接充电桩", muted));
        l->addWidget(button("寻找充电站", this, [this] { navigate("map"); }, true));
    } else {
        chargeState = label(statusText(text(active, "status")), "font-size:18px;color:#e8a7e0;");
        l->addWidget(chargeState);
        const auto energyText=money(number(active, "energy_kwh")) + " kWh";
        const auto costText="¥" + money(number(active, "amount"));
        const auto valueStyle=[](const QString &value) {
            return QString("font-size:%1px;").arg(value.size()>10?14:value.size()>8?16:18);
        };
        chargeEnergy = label(energyText, valueStyle(energyText));
        chargeCost = label(costText, valueStyle(costText));
        chargeTime = label("—", "font-size:18px;");
        for (auto value : {chargeEnergy.data(),chargeCost.data(),chargeTime.data()}) {
            value->setMinimumWidth(0);
            value->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
        }
        auto metrics=new QFrame;metrics->setObjectName("charge-metrics");
        metrics->setStyleSheet("QFrame#charge-metrics{background:#21152f;border:1px solid #604074;border-radius:12px;}");
        auto grid=new QGridLayout(metrics);grid->setContentsMargins(14,12,14,12);
        for(int column=0;column<3;++column)grid->setColumnStretch(column,1);
        grid->addWidget(label("订单电量",muted),0,0);grid->addWidget(label("账单费用",muted),0,1);grid->addWidget(label("预计剩余",muted),0,2);
        grid->addWidget(chargeEnergy,1,0);grid->addWidget(chargeCost,1,1);grid->addWidget(chargeTime,1,2);
        l->addWidget(metrics);
        l->addStretch(1);
        l->addWidget(label("电桩 " + text(active, "charger_code") + " · " +
            QString::number(number(active, "power_kw")) + " kW · 按预约时分时价格计费", muted));
        l->addStretch(1);
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
    box->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    body->addWidget(box,1);
    updateCharging();
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
