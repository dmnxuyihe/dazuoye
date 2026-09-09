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

    auto locationState = label("选择“我的位置”或在地图上选点后，将按距离推荐电站", muted);
    body->addWidget(locationState);
    auto map = new StationMap;
    map->setApi(api);
    map->setMinimumHeight(350);
    map->setStations(stations, selectedStation);
    body->addWidget(map);

    auto recommendations = new StationRecommendations;
    body->addWidget(recommendations);
    auto detail = new QFrame;
    detail->setObjectName("station-sheet");
    detail->setStyleSheet("QFrame#station-sheet{background:#251733;border:1px solid #50305f;border-radius:16px;}");
    auto details = new QVBoxLayout(detail);
    auto name = label("", "font-size:20px;font-weight:600;");
    auto meta = label("", "color:#d8c4e5;font-size:12px;");
    auto go = button("导航到这个充电站", this, [] {}, true);
    details->addWidget(name); details->addWidget(meta); details->addWidget(go);
    body->addWidget(detail);

    auto visibleRows = std::make_shared<QJsonArray>();
    auto showStation = [=](const QString &id) {
        selectedStation = id;
        recommendations->setSelected(id);
        map->selectStation(id); // 高亮标记并平滑跳转到站点。
        for (const auto &value : *visibleRows) {
            const auto station = value.toObject();
            if (text(station, "id") != id) continue;
            name->setText(text(station, "name"));
            const bool located = station.value("distance_km").isDouble();
            meta->setText(QString("%1%2  ·  空闲 %3/%4")
                .arg(located ? money(number(station, "distance_km")) + " km  ·  " : "")
                .arg(text(station, "address"))
                .arg(qRound(number(station, "available_count")))
                .arg(qRound(number(station, "charger_count"))));
            break;
        }
    };
    connect(map, &StationMap::stationSelected, recommendations, showStation);
    connect(recommendations, &StationRecommendations::stationSelected, detail, showStation);
    connect(go, &QPushButton::clicked, map, [=] { map->navigateToStation(selectedStation); });

    auto filtered = [=] {
        *visibleRows = {};
        for (const auto &value : stations) {
            const auto station = value.toObject();
            if ((text(station, "name") + text(station, "address"))
                    .contains(search->text().trimmed(), Qt::CaseInsensitive))
                visibleRows->append(station);
        }
        if (!visibleRows->isEmpty() && !std::any_of(visibleRows->begin(), visibleRows->end(), [=](const QJsonValue &v) {
                return text(v.toObject(), "id") == selectedStation;
            })) {
            auto nearest = visibleRows->first().toObject();
            for (const auto &value : *visibleRows) {
                const auto candidate = value.toObject();
                if (candidate.value("distance_km").isDouble() &&
                    (!nearest.value("distance_km").isDouble() ||
                     number(candidate, "distance_km") < number(nearest, "distance_km"))) nearest = candidate;
            }
            selectedStation = text(nearest, "id");
        }
        recommendations->setStations(*visibleRows, selectedStation);
        map->setStations(*visibleRows, selectedStation);
        detail->setVisible(!visibleRows->isEmpty());
        if (!visibleRows->isEmpty()) showStation(selectedStation);
    };
    auto generation = std::make_shared<int>(0);
    connect(map, &StationMap::locationChanged, map, [=](double lat, double lon) {
        locationState->setText(QString("我的位置：%1, %2  ·  正在查找附近电站…")
            .arg(lat, 0, 'f', 5).arg(lon, 0, 'f', 5));
        map->focusLocation(); // 定位完成后先明确展示用户位置。
        const int request = ++*generation;
        api->get(QString("/public/stations?latitude=%1&longitude=%2").arg(lat,0,'f',6).arg(lon,0,'f',6), map,
            [=](const Reply &reply) {
                if (request != *generation) return;
                if (!reply.ok) { message(reply); return; }
                stations = reply.data.array();
                locationState->setText(QString("已定位  ·  找到 %1 个站点，推荐结果按距离排序").arg(stations.size()));
                filtered();
                map->focusLocation();
            });
    });
    connect(search, &QLineEdit::textChanged, recommendations, [=] { filtered(); });
    filtered();
}
