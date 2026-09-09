#include "station_recommendations.h"
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
class SingleLineFitLabel final : public QLabel {
  public:
    SingleLineFitLabel(const QString &text, int maximumPixelSize, int minimumPixelSize,
                       QFont::Weight weight = QFont::Normal, QWidget *parent = nullptr)
        : QLabel(text, parent), maximumPixelSize(maximumPixelSize),
          minimumPixelSize(minimumPixelSize) {
        baseFont = font();
        baseFont.setWeight(weight);
        setAlignment(Qt::AlignCenter);
        setWordWrap(false);
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        fitFont();
    }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        fitFont();
    }

  private:
    void fitFont() {
        const int availableWidth = contentsRect().width();
        int pixelSize = maximumPixelSize;
        QFont fitted = baseFont;
        for (; pixelSize > minimumPixelSize; --pixelSize) {
            fitted.setPixelSize(pixelSize);
            if (availableWidth <= 0 || QFontMetrics(fitted).horizontalAdvance(text()) <= availableWidth)
                break;
        }
        fitted.setPixelSize(qMax(minimumPixelSize, pixelSize));
        setFont(fitted);
        setFixedHeight(QFontMetrics(fitted).height() + 2);
    }

    QFont baseFont;
    int maximumPixelSize;
    int minimumPixelSize;
};

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
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);
    hint = label("附近充电站", "color:#b3a0c2;font-size:12px;");
    root->addWidget(hint);
    summary = new QPushButton("选择充电站");
    summary->setObjectName("station-summary");
    summary->setCursor(Qt::PointingHandCursor);
    summary->setStyleSheet(
        "QPushButton#station-summary{padding:11px 13px;text-align:left;background:#21162d;"
        "border:1px solid #493457;border-radius:12px;color:#f8efff;font-weight:600;}"
        "QPushButton#station-summary:hover{border-color:#9e63bc;background:#2a1939;}");
    root->addWidget(summary);
    scroll = new QScrollArea;
    scroll->setObjectName("recommendation-scroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scroll->setVisible(false);
    scroll->viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);
    auto host = new QWidget;
    cards = new QVBoxLayout(host);
    cards->setContentsMargins(0, 0, 0, 4);
    cards->setSpacing(10);
    scroll->setWidget(host);
    root->addWidget(scroll, 1);
    connect(summary, &QPushButton::clicked, this, [this] {
        expanded = !expanded;
        scrollSelectionIntoView = expanded;
        rebuild();
    });
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
    const int previousScroll = scroll->verticalScrollBar()->value();
    clearLayout(cards);
    hint->setText(rows.isEmpty() ? "暂无匹配站点，请定位或扩大搜索范围" :
                                  "附近充电站  ·  按距离由近到远");
    QJsonObject selectedStation;
    for (const auto &value : rows)
        if (text(value.toObject(), "id") == selected) selectedStation = value.toObject();
    summary->setText(selectedStation.isEmpty() ? "选择充电站"
        : QString("%1    %2    空闲 %3/%4    %5 元/kWh    %6")
            .arg(text(selectedStation, "name"), distanceText(selectedStation))
            .arg(qRound(number(selectedStation, "available_count")))
            .arg(qRound(number(selectedStation, "charger_count")))
            .arg(money(number(selectedStation, "unit_price")))
            .arg(expanded ? "⌃" : "⌄"));
    summary->setVisible(!expanded);
    scroll->setVisible(expanded && !rows.isEmpty());
    QPointer<QWidget> activeCard;
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
        card->setMinimumHeight(54);
        card->setText(QString("%1    %2    空闲 %3/%4    %5 元/kWh")
            .arg(text(station, "name", "充电站"), distanceText(station))
            .arg(qRound(number(station, "available_count")))
            .arg(qRound(number(station, "charger_count")))
            .arg(money(number(station, "unit_price"))));
        card->setStyleSheet(
            "QPushButton#recommendation-card{padding:12px;text-align:left;white-space:normal;"
            "background:#21162d;border:1px solid #493457;border-radius:14px;color:#f8efff;}"
            "QPushButton#recommendation-card:hover{border-color:#9e63bc;background:#2a1939;}"
            "QPushButton#recommendation-card:checked{background:#3b1d51;border:2px solid #d477ee;}"
            "QPushButton#recommendation-card:focus{outline:none;border-color:#e9a2f5;}");
        connect(card, &QPushButton::clicked, this, [this, id] {
            if (selected == id && expanded) {
                expanded = false;
                rebuild();
                return;
            }
            selected = id;
            expanded = true;
            scrollSelectionIntoView = true;
            emit stationSelected(id);
            QTimer::singleShot(0, this, [this] { rebuild(); });
        });
        cards->addWidget(card);
        if (active) activeCard = card;
        if (active && expanded) {
            auto detail = new QFrame;
            detail->setObjectName("station-expanded-card");
            detail->setStyleSheet(
                "QFrame#station-expanded-card{background:#251733;border:1px solid #50305f;border-radius:14px;}"
                "QFrame#metric-cell{border:0;background:transparent;}"
                "QFrame#metric-divider{background:#50305f;border:0;}");
            auto detailLayout = new QVBoxLayout(detail);
            detailLayout->setContentsMargins(14, 13, 14, 14);
            detailLayout->setSpacing(12);
            detailLayout->addWidget(label(text(station, "address", "暂无详细地址"),
                                          "color:#bba8c8;font-size:11px;"));
            auto metrics = new QWidget;
            auto metricRow = new QHBoxLayout(metrics);
            metricRow->setContentsMargins(0, 2, 0, 2);
            metricRow->setSpacing(10);
            auto addMetric = [&](const QString &caption, const QString &metric,
                                 const QString &objectPrefix = QString(),
                                 int captionMaximumSize = 10, int captionMinimumSize = 8) {
                auto cell = new QFrame; cell->setObjectName("metric-cell");
                auto layout = new QVBoxLayout(cell); layout->setContentsMargins(4,0,4,0); layout->setSpacing(5);
                auto captionLabel = new SingleLineFitLabel(caption, captionMaximumSize, captionMinimumSize);
                auto metricLabel = new SingleLineFitLabel(metric, 16, 10, QFont::DemiBold);
                captionLabel->setStyleSheet("color:#a991ba;");
                metricLabel->setStyleSheet("color:#fff4ff;");
                if (!objectPrefix.isEmpty()) {
                    captionLabel->setObjectName(objectPrefix + "-caption");
                    metricLabel->setObjectName(objectPrefix + "-value");
                }
                layout->addWidget(captionLabel);
                layout->addWidget(metricLabel);
                metricRow->addWidget(cell,1);
            };
            auto divider = [&] { auto line=new QFrame; line->setObjectName("metric-divider"); line->setFixedWidth(1); metricRow->addWidget(line); };
            addMetric("距离", distanceText(station)); divider();
            addMetric("空闲电桩", QString("%1/%2").arg(qRound(number(station,"available_count"))).arg(qRound(number(station,"charger_count")))); divider();
            addMetric("价格（/kWh）", money(number(station,"unit_price")) + " ¥", "base-price", 9, 7);
            detailLayout->addWidget(metrics);
            auto actions = new QHBoxLayout;
            auto choose=button("选择此充电站",detail,[]{},true);
            auto navigate=button("导航到此站",detail,[]{});
            actions->addWidget(choose,2); actions->addWidget(navigate,1); detailLayout->addLayout(actions);
            connect(choose,&QPushButton::clicked,this,[this,id]{emit stationChosen(id);});
            connect(navigate,&QPushButton::clicked,this,[this,id]{emit navigationRequested(id);});
            cards->addWidget(detail);
        }
    }
    cards->addStretch();
    const bool shouldScroll = std::exchange(scrollSelectionIntoView, false);
    QMetaObject::invokeMethod(scroll, [this, previousScroll, activeCard, shouldScroll] {
        scroll->verticalScrollBar()->setValue(previousScroll);
        if (!shouldScroll || !activeCard) return;
        auto animation = new QPropertyAnimation(scroll->verticalScrollBar(), "value", scroll);
        animation->setDuration(240);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        animation->setStartValue(scroll->verticalScrollBar()->value());
        animation->setEndValue(qMin(activeCard->y(), scroll->verticalScrollBar()->maximum()));
        connect(animation, &QPropertyAnimation::finished, animation, &QObject::deleteLater);
        animation->start();
    }, Qt::QueuedConnection);
}
