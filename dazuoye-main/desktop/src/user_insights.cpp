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

QFrame *statisticsSummary(QLabel **energyValue, QLabel **amountValue, QLabel **orderValue) {
    auto frame = new QFrame;
    frame->setObjectName("personal-statistics");
    frame->setStyleSheet(
        "QFrame#personal-statistics{background:#21152f;border:1px solid #604074;"
        "border-radius:12px;}QFrame#statistics-divider{background:#4b365b;border:0;}"
    );
    auto grid = new QGridLayout(frame);
    grid->setContentsMargins(14, 12, 14, 12);
    grid->setHorizontalSpacing(12);

    const QString valueStyle = "font-size:18px;font-weight:600;color:#f6edff;";
    const QStringList captions = {"充电量（kWh）", "消费金额", "订单数"};
    QLabel **values[] = {energyValue, amountValue, orderValue};
    for (int i = 0; i < 3; ++i) {
        const int column = i * 2;
        auto caption = label(captions[i], muted);
        caption->setAlignment(Qt::AlignCenter);
        grid->addWidget(caption, 0, column);
        *values[i] = label("—", valueStyle);
        (*values[i])->setAlignment(Qt::AlignCenter);
        (*values[i])->setMinimumWidth(0);
        grid->addWidget(*values[i], 1, column);
        grid->setColumnStretch(column, 1);
        if (i < 2) {
            auto divider = new QFrame;
            divider->setObjectName("statistics-divider");
            divider->setFixedWidth(1);
            grid->addWidget(divider, 0, column + 1, 2, 1);
        }
    }
    return frame;
}
} // namespace
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
    QLabel *energyValue = nullptr, *amountValue = nullptr, *orderValue = nullptr;
    auto totals = statisticsSummary(&energyValue, &amountValue, &orderValue);
    auto graph = new DataGraphic(DataGraphic::GroupedBars); graph->setFixedHeight(250);
    auto breakdown = new QWidget; auto details = new QVBoxLayout(breakdown);
    details->setContentsMargins(0,0,0,0);
    rows->addWidget(graph); rows->addWidget(totals); rows->addWidget(breakdown);
    body->addWidget(host);
    auto generation = std::make_shared<int>(0);
    statisticsLoader = [=] {
        const int epoch = ++*generation;
        api->get(QString("/me/statistics?period=")+(statsPeriod ? "all":"month"),host,[=](const Reply &r) {
            if (epoch != *generation) return;
            if (!r.ok || r.cached) message(r);
            if (!r.ok) {
                energyValue->setText("读取失败");
                amountValue->setText("—");
                orderValue->setText("—");
                return;
            }
            const auto data = r.data.object(); const auto daily = data.value("daily").toArray();
            double energy=0,amount=0; int count=0; QList<double> values; QStringList dates;
            for (auto v : daily) { auto o=v.toObject(); energy+=number(o,"energy_kwh");amount+=number(o,"amount");count+=int(number(o,"orders"));
                values<<number(o,"amount");dates<<text(o,"day").mid(5); }
            graph->setData({values},dates,"元");
            const QString energyText = money(energy);
            const int energyFontSize = energyText.size() > 12 ? 13 : energyText.size() > 9 ? 15
                                                                          : energyText.size() > 6 ? 17
                                                                                                : 18;
            energyValue->setStyleSheet(
                QString("font-size:%1px;font-weight:600;color:#f6edff;").arg(energyFontSize));
            energyValue->setText(energyText);
            amountValue->setText("¥" + money(amount));
            orderValue->setText(QString::number(count) + " 笔");
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

