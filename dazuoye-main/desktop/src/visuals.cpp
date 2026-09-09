#include "visuals.h"
#include <cmath>
namespace {
double roundedAxisMaximum(double value) {
    if (value <= 0)
        return 1;
    const double magnitude = std::pow(10., std::floor(std::log10(value)));
    return std::ceil(value / magnitude) * magnitude;
}

double roundedTickStep(double maximum) {
    const double target = maximum / 5.;
    const double magnitude = std::pow(10., std::floor(std::log10(target)));
    return std::ceil(target / magnitude) * magnitude;
}

QString axisNumber(double value, double step) {
    int decimals = 0;
    if (step < 1)
        decimals = qMin(3, qMax(0, int(std::ceil(-std::log10(step)))));
    QString result = QString::number(value, 'f', decimals);
    while (result.contains('.') && result.endsWith('0'))
        result.chop(1);
    if (result.endsWith('.'))
        result.chop(1);
    return result;
}
} // namespace

Segments::Segments(const QStringList &labels, int selected, QWidget *parent) : QWidget(parent) {
    setObjectName("segments");
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(
        "QWidget#segments{background:#1c122b;border:1px solid "
        "#3c294b;border-radius:11px;}QPushButton{background:transparent;border:0;padding:7px "
        "9px;min-height:16px;font-size:12px;color:#bcaaca;border-radius:8px;}QPushButton:checked{"
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #812be2,stop:.5 #b652df,stop:1 "
        "#ed9bdc);color:white;}QPushButton:hover{color:white;}");
    auto l = new QHBoxLayout(this);
    l->setContentsMargins(3, 3, 3, 3);
    l->setSpacing(2);
    auto group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int i = 0; i < labels.size(); ++i) {
        auto b = new QPushButton(labels[i]);
        b->setCheckable(true);
        b->setChecked(i == selected);
        b->setCursor(Qt::PointingHandCursor);
        l->addWidget(b, 1);
        group->addButton(b, i);
    }
    connect(group, &QButtonGroup::idClicked, this, &Segments::changed);
}
QWidget *pageHeading(const QString &title, QObject *context, std::function<void()> back) {
    auto w = new QWidget;
    auto l = new QHBoxLayout(w);
    l->setContentsMargins(0, 4, 0, 9);
    auto b = button("‹", context, std::move(back));
    b->setFixedSize(34, 34);
    b->setStyleSheet("padding:0;min-height:0;background:#1d122b;border:1px solid "
                     "#34223f;border-radius:17px;font-size:23px;");
    l->addWidget(b);
    auto t = label(title, "font-size:16px;font-weight:600;");
    t->setAlignment(Qt::AlignCenter);
    l->addWidget(t, 1);
    auto spacer = new QWidget;
    spacer->setFixedWidth(34);
    l->addWidget(spacer);
    return w;
}
QWidget *detailLine(const QString &icon, const QString &caption, const QString &value) {
    auto w = new QWidget;
    auto l = new QHBoxLayout(w);
    l->setContentsMargins(1, 6, 1, 6);
    l->setSpacing(9);
    auto mark = label("");
    mark->setPixmap(appIcon(icon).pixmap(18, 18));
    l->addWidget(mark);
    l->addWidget(label(caption, "color:#b6a1c5;font-size:12px;"), 1);
    l->addWidget(label(value, "font-size:13px;font-weight:600;"));
    return w;
}
QWidget *amountBar(const QString &caption, double value, double maximum, const QColor &color,
                   const QString &unit) {
    auto w = new QWidget;
    auto l = new QVBoxLayout(w);
    l->setContentsMargins(0, 5, 0, 5);
    l->setSpacing(6);
    auto top = new QHBoxLayout;
    top->addWidget(label(caption, "font-size:11px;"), 1);
    top->addWidget(label(money(value) + " " + unit, "font-size:11px;color:#c5afcf;"));
    l->addLayout(top);
    auto bar = new QProgressBar;
    bar->setRange(0, 1000);
    bar->setValue(maximum > 0 ? qBound(0, int(value / maximum * 1000), 1000) : 0);
    bar->setTextVisible(false);
    bar->setFixedHeight(6);
    bar->setStyleSheet(QString("QProgressBar{border:0;border-radius:3px;background:#4c3c5d;}"
                               "QProgressBar::chunk{border-radius:3px;background:%1;}")
                           .arg(color.name()));
    l->addWidget(bar);
    return w;
}
QWidget *emptyPanel(const QString &title, const QString &description, const QString &icon) {
    QVBoxLayout *l;
    auto box = card("", &l);
    auto art = label("");
    art->setPixmap(appIcon(icon, QColor("#c98ee1")).pixmap(38, 38));
    art->setAlignment(Qt::AlignCenter);
    l->addWidget(art);
    auto t = label(title, "font-size:17px;font-weight:600;");
    t->setAlignment(Qt::AlignCenter);
    l->addWidget(t);
    auto d = label(description, "font-size:12px;color:#a98cbf;");
    d->setAlignment(Qt::AlignCenter);
    l->addWidget(d);
    return box;
}
DataGraphic::DataGraphic(Type t, QWidget *parent) : QWidget(parent), type(t) {
    setMinimumHeight(type == Heatmap ? 132 : 190);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void DataGraphic::setData(QList<QList<double>> s, QStringList l, QString u) {
    series = s;
    labels = l;
    unit = u;
    update();
}
void DataGraphic::setJourney(const QJsonObject &o) {
    order = o;
    update();
}
void DataGraphic::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const double w = width(), h = height();
    QFont font = this->font();
    font.setPixelSize(10);
    p.setFont(font);
    const QList<QColor> colors = {QColor("#e589dc"), QColor("#edcd79"), QColor("#8850d8")};
    if (type == Journey) {
        QPixmap car(":/assets/admin-xray-car.png");
        auto size=car.size().scaled(QSize(int(w*.65),int(h*.5)),Qt::KeepAspectRatio);
        p.drawPixmap(QRectF((w-size.width())/2,8,size.width(),size.height()),car,car.rect());
        const QString state=text(order,"status","");
        const QStringList states={"reserved","charging","pending_payment","completed"};
        const QStringList titles={"已预约","充电中","待支付","已完成"};
        const int stage=states.indexOf(state);
        const double step=(w-44)/3., y=h-65;
        p.setPen(QPen(QColor("#473051"),4));p.drawLine(QPointF(22,y),QPointF(w-22,y));
        if(stage>=0) {p.setPen(QPen(QColor("#d78bdd"),4));p.drawLine(QPointF(22,y),QPointF(22+stage*step,y));}
        for(int i=0;i<4;++i) {
            p.setPen(Qt::NoPen);p.setBrush(QColor(i<=stage?"#d78bdd":"#473051"));p.drawEllipse(QPointF(22+i*step,y),7,7);
            p.setPen(QColor(i==stage?"#f0d4fa":"#a68ab6"));
            p.drawText(QRectF(i*step-4,y+14,52,20),Qt::AlignCenter,titles[i]);
        }
        p.setPen(QColor("#cab4d8"));
        p.drawText(QRectF(0,h-24,w,22),Qt::AlignCenter,order.isEmpty()?"暂无订单":state=="cancelled"?"订单已取消":text(order,"station_name"));
        return;
    }
    if (type == Heatmap) {
        int columns = 24;
        double cw = (w - 28) / columns, ch = (h - 30) / 7;
        double peak = 1;
        for (auto v : series)
            for (double n : v)
                peak = qMax(peak, n);
        for (int r = 0; r < 7; ++r) {
            p.setPen(QColor("#937ba9"));
            p.drawText(QRectF(0, 18 + r * ch, 22, ch), Qt::AlignVCenter, QString::number(r + 1));
            for (int c = 0; c < 24; ++c) {
                double v = series.value(r).value(c);
                QColor color = QColor("#30203e");
                if (v > 0)
                    color = QColor::fromRgbF(.35 + .5 * v / peak, .18 + .3 * v / peak,
                                             .47 + .35 * v / peak);
                p.setPen(Qt::NoPen);
                p.setBrush(color);
                p.drawRoundedRect(QRectF(26 + c * cw, 18 + r * ch, cw - 3, ch - 3), 2, 2);
            }
        }
        p.setPen(QColor("#b59bc7"));
        p.drawText(26, 11, "00:00");
        p.drawText(w / 2 - 12, 11, "12:00");
        p.drawText(w - 38, 11, "23:00");
        return;
    }
    int n = 0;
    double maximum = 0;
    for (auto s : series) {
        n = qMax(n, int(s.size()));
        for (double x : s)
            maximum = qMax(maximum, x);
    }
    if (n == 0) {
        p.setPen(QColor("#a189b2"));
        p.drawText(rect(), Qt::AlignCenter, "暂无对应时段记录");
        return;
    }
    maximum = roundedAxisMaximum(maximum);
    QRectF area(60, 16, w - 73, h - 46);
    const double tickStep = roundedTickStep(maximum);
    QList<double> ticks;
    for (double value = 0; value < maximum; value += tickStep)
        ticks.prepend(value);
    if (ticks.isEmpty() || !qFuzzyCompare(ticks.first() + 1., maximum + 1.))
        ticks.prepend(maximum);
    for (double value : ticks) {
        double y = area.bottom() - value / maximum * area.height();
        p.setPen(QPen(QColor("#493151"), 1, Qt::DashLine));
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.setPen(QColor("#a88dbb"));
        p.drawText(QRectF(0, y - 8, 54, 16), Qt::AlignRight | Qt::AlignVCenter,
                   axisNumber(value, tickStep));
    }
    if (type == GroupedBars) {
        double step = area.width() / n, bw = qMin(6., step / (series.size() + 2));
        for (int i = 0; i < n; i++)
            for (int j = 0; j < series.size(); j++) {
                double value = series[j].value(i);
                double bh = value / maximum * area.height();
                p.setPen(Qt::NoPen);
                p.setBrush(colors[j % 3]);
                p.drawRoundedRect(QRectF(area.left() + i * step + step * .2 + j * (bw + 2),
                                         area.bottom() - bh, bw, bh),
                                  1.5, 1.5);
            }
    } else
        for (int j = 0; j < series.size(); j++) {
            QPainterPath line;
            for (int i = 0; i < series[j].size(); i++) {
                QPointF pt(area.left() + i * area.width() / qMax(1, n - 1),
                           area.bottom() - series[j][i] / maximum * area.height());
                if (i == 0)
                    line.moveTo(pt);
                else
                    line.lineTo(pt);
            }
            auto fill = line;
            fill.lineTo(area.right(), area.bottom());
            fill.lineTo(area.left(), area.bottom());
            QLinearGradient g(0, area.top(), 0, area.bottom());
            auto color = colors[j % 3];
            color.setAlpha(150);
            g.setColorAt(0, color);
            color.setAlpha(3);
            g.setColorAt(1, color);
            p.fillPath(fill, g);
            p.setPen(QPen(colors[j % 3], 2));
            p.drawPath(line);
            for (int i = 0; i < series[j].size(); i++) {
                p.setBrush(colors[j % 3]);
                p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(area.left() + i * area.width() / qMax(1, n - 1),
                                      area.bottom() - series[j][i] / maximum * area.height()),
                              2.5, 2.5);
            }
        }
    p.setPen(QColor("#b59bc7"));
    int stride = qMax(1, int(std::ceil(n / 7.)));
    for (int i = 0; i < n; i += stride) {
        double x = area.left() +
                   (type == GroupedBars ? (i + .5) / n : double(i) / qMax(1, n - 1)) * area.width();
        p.drawText(QRectF(x - 23, h - 24, 46, 18), Qt::AlignCenter,
                   labels.value(i, QString::number(i + 1)));
    }
}
void DataGraphic::mouseMoveEvent(QMouseEvent *e) {
    if (type == Journey)
        return;
    if (type == Heatmap) {
        int c = qBound(0, int((e->position().x() - 26) / qMax(1., (width() - 28.) / 24)), 23),
            r = qBound(0, int((e->position().y() - 18) / qMax(1., (height() - 30.) / 7)), 6);
        setToolTip(
            QString("星期 %1 · %2:00 · %3 次").arg(r + 1).arg(c).arg(series.value(r).value(c)));
        return;
    }
    int n = series.isEmpty() ? 0 : series.first().size();
    if (!n)
        return;
    int i = qBound(0, int((e->position().x() - 60) / qMax(1., width() - 73.) * n), n - 1);
    QStringList values;
    for (auto s : series) {
        const QString value = money(s.value(i));
        values << (unit == "元" ? "消费：¥" + value : value + " " + unit);
    }
    QToolTip::showText(e->globalPosition().toPoint() + QPoint(12, 16),
                       labels.value(i) + "\n" + values.join(" / "), this);
}
