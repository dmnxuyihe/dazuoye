#include "user_window.h"
#include "station_map.h"
#include "station_recommendations.h"
#include "draggable_bottom_sheet.h"
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
    auto host = new DraggableBottomSheet;
    if (auto pageScroll = root->findChild<QScrollArea *>("page-scroll"))
        host->setMinimumHeight(qMax(610, pageScroll->viewport()->height()));
    body->addWidget(host, 1);
    auto panel = host->contentLayout();
    auto sheetTitle = new QHBoxLayout;
    sheetTitle->addWidget(label("附近充电站", "font-size:18px;font-weight:600;"));
    sheetTitle->addStretch();
    sheetTitle->addWidget(label("上下拖动查看", "color:#90799f;font-size:10px;"));
    panel->addLayout(sheetTitle);
    auto search = new QLineEdit;
    search->setPlaceholderText("搜索站点名称或地址");
    panel->addWidget(search);

    auto locationState = label("选择“我的位置”或在地图上选点后，将按距离推荐电站", muted);
    panel->addWidget(locationState);
    auto map = new StationMap;
    map->setApi(api);
    map->setStations(stations, selectedStation);
    host->setBackground(map);

    auto recommendations = new StationRecommendations;
    panel->addWidget(recommendations);
    panel->addStretch();

    auto visibleRows = std::make_shared<QJsonArray>();
    auto showStation = [=](const QString &id) {
        selectedStation = id;
        recommendations->setSelected(id);
    };
    connect(map, &StationMap::stationSelected, recommendations, [=](const QString &id) {
        showStation(id);
        host->expand();
    });
    connect(recommendations, &StationRecommendations::stationSelected, map, [=](const QString &id) {
        showStation(id);
        host->expand();
        map->selectStation(id);
    });
    connect(recommendations, &StationRecommendations::navigationRequested, map,
            [=](const QString &id) { map->navigateToStation(id); });
    connect(recommendations, &StationRecommendations::stationChosen, this,
            [this](const QString &id) { selectedStation = id; navigate("station"); });

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
                if (!candidate.value("distance_km").isNull() &&
                    !candidate.value("distance_km").isUndefined() &&
                    (nearest.value("distance_km").isNull() || nearest.value("distance_km").isUndefined() ||
                     number(candidate, "distance_km") < number(nearest, "distance_km"))) nearest = candidate;
            }
            selectedStation = text(nearest, "id");
        }
        recommendations->setStations(*visibleRows, selectedStation);
        map->setStations(*visibleRows, selectedStation);
        if (!visibleRows->isEmpty()) showStation(selectedStation);
    };
    auto generation = std::make_shared<int>(0);
    connect(map, &StationMap::locationChanged, map, [=](double lat, double lon) {
        locationState->setText(QString("我的位置：%1, %2  ·  正在查找附近电站…")
            .arg(lat, 0, 'f', 5).arg(lon, 0, 'f', 5));
        const int request = ++*generation;
        api->get(QString("/public/stations?latitude=%1&longitude=%2").arg(lat,0,'f',6).arg(lon,0,'f',6), map,
            [=](const Reply &reply) {
                if (request != *generation) return;
                if (!reply.ok) { message(reply); return; }
                stations = reply.data.array();
                locationState->setText(QString("已定位  ·  找到 %1 个站点，推荐结果按距离排序").arg(stations.size()));
                filtered();
            });
    });
    connect(search, &QLineEdit::textChanged, recommendations, [=] { filtered(); });
    mapDataUpdater = filtered;
    filtered();
    map->requestCurrentLocation();
}
