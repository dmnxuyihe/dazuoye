#include "station_recommendations.h"
#include "ui.h"

#include <algorithm>
#include <cmath>

namespace {
double distanceOf(const QJsonObject &station) {
    const auto value = station.value("distance_km");
    if (value.isNull() || value.isUndefined()) return std::numeric_limits<double>::max();
    bool valid = false;
    const double distance = value.toVariant().toDouble(&valid);
    return valid ? distance : std::numeric_limits<double>::max();
}

QString distanceText(const QJsonObject &station) {
    const double km = distanceOf(station);
    if (!std::isfinite(km) || km == std::numeric_limits<double>::max()) return "定位后显示距离";
    return km < 1 ? QString("%1 m").arg(qRound(km * 1000.0))
                  : QString("%1 km").arg(km, 0, 'f', km < 10 ? 1 : 0);
}
}

StationRecommendations::StationRecommendations(QWidget *parent) : QWidget(parent) {
    setObjectName("station-recommendations");
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);
    hint = label("推荐充电站  ·  上下滑动查看更多", "color:#b3a0c2;font-size:12px;");
    root->addWidget(hint);
    auto scroll = new QScrollArea;
    scroll->setObjectName("recommendation-scroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFixedHeight(286);
    scroll->viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);
    auto host = new QWidget;
    cards = new QVBoxLayout(host);
    cards->setContentsMargins(0, 0, 0, 4);
    cards->setSpacing(10);
    cards->addStretch();
    scroll->setWidget(host);
    root->addWidget(scroll);
}

void StationRecommendations::setStations(const QJsonArray &stations, const QString &selectedId) {
    QList<QJsonObject> sorted;
    for (const auto &value : stations) sorted.append(value.toObject());
    std::sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return distanceOf(a) < distanceOf(b);
    });
    rows = {};
    for (const auto &station : sorted) rows.append(station);
    selected = selectedId;
    rebuild();
}

void StationRecommendations::setSelected(const QString &stationId) {
    if (selected == stationId) return;
    selected = stationId;
    rebuild();
}

void StationRecommendations::rebuild() {
    clearLayout(cards);
    hint->setText(rows.isEmpty() ? "暂无匹配站点，请定位或扩大搜索范围"
                                 : "推荐充电站  ·  按距离由近到远  ·  上下滑动查看更多");
    for (const auto &value : rows) {
        const auto station = value.toObject();
        const QString id = text(station, "id");
        const bool active = id == selected;
        auto card = new QPushButton;
        card->setObjectName("recommendation-card");
        card->setProperty("stationId", id);
        card->setCheckable(true);
        card->setChecked(active);
        card->setCursor(Qt::PointingHandCursor);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        card->setMinimumHeight(104);
        card->setText(QString("%1\n%2  ·  空闲 %3/%4\n%5")
            .arg(text(station, "name", "充电站"), distanceText(station))
            .arg(qRound(number(station, "available_count")))
            .arg(qRound(number(station, "charger_count")))
            .arg(text(station, "address", "点击在地图中查看")));
        card->setStyleSheet(
            "QPushButton#recommendation-card{padding:12px;text-align:left;white-space:normal;"
            "background:#21162d;border:1px solid #493457;border-radius:14px;color:#f8efff;}"
            "QPushButton#recommendation-card:hover{border-color:#9e63bc;background:#2a1939;}"
            "QPushButton#recommendation-card:checked{background:#3b1d51;border:2px solid #d477ee;}"
            "QPushButton#recommendation-card:focus{outline:none;border-color:#e9a2f5;}");
        connect(card, &QPushButton::clicked, this, [this, id] { emit stationSelected(id); });
        cards->addWidget(card);
    }
    cards->addStretch();
}
