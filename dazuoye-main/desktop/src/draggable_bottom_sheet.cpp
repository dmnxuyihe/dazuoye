#include "draggable_bottom_sheet.h"

DraggableBottomSheet::DraggableBottomSheet(QWidget *parent) : QWidget(parent) {
    setObjectName("map-sheet-host");
    setMinimumHeight(610);
    sheet = new QFrame(this);
    sheet->setObjectName("map-bottom-sheet");
    sheet->setStyleSheet(
        "QFrame#map-bottom-sheet{background:#171021;border:1px solid #51315e;"
        "border-top-left-radius:22px;border-top-right-radius:22px;}"
        "QWidget#sheet-grab-area{background:transparent;}");
    auto shell = new QVBoxLayout(sheet);
    shell->setContentsMargins(14, 7, 14, 12);
    shell->setSpacing(5);
    grabArea = new QWidget;
    grabArea->setObjectName("sheet-grab-area");
    grabArea->setFixedHeight(28);
    grabArea->setCursor(Qt::SizeVerCursor);
    auto grabLayout = new QVBoxLayout(grabArea);
    grabLayout->setContentsMargins(0, 4, 0, 8);
    auto handle = new QFrame;
    handle->setFixedSize(46, 5);
    handle->setStyleSheet("background:#765585;border-radius:2px;");
    grabLayout->addWidget(handle, 0, Qt::AlignHCenter);
    grabArea->installEventFilter(this);
    handle->installEventFilter(this);
    shell->addWidget(grabArea);
    auto scroll = new QScrollArea;
    scroll->setObjectName("sheet-scroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto contentHost = new QWidget;
    content = new QVBoxLayout(contentHost);
    content->setContentsMargins(2, 2, 2, 6);
    content->setSpacing(10);
    scroll->setWidget(contentHost);
    shell->addWidget(scroll, 1);
    animation = new QPropertyAnimation(sheet, "geometry", this);
    animation->setDuration(260);
    animation->setEasingCurve(QEasingCurve::OutCubic);
}

void DraggableBottomSheet::setBackground(QWidget *widget) {
    background = widget;
    widget->setParent(this);
    widget->lower();
    widget->setGeometry(rect());
}

void DraggableBottomSheet::expand() { snap(Expanded); }

int DraggableBottomSheet::yFor(Position target) const {
    if (target == Collapsed) return height() - 48;
    if (target == Expanded) return 34;
    return qMax(210, height() - qMin(330, int(height() * .43)));
}

void DraggableBottomSheet::moveSheet(int y) {
    const int top = qBound(yFor(Expanded), y, yFor(Collapsed));
    sheet->setGeometry(8, top, qMax(0, width() - 16), height() - top + 22);
    sheet->raise();
}

void DraggableBottomSheet::snap(Position target) {
    position = target;
    animation->stop();
    animation->setStartValue(sheet->geometry());
    animation->setEndValue(QRect(8, yFor(target), qMax(0, width() - 16),
                                 height() - yFor(target) + 22));
    animation->start();
}

void DraggableBottomSheet::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (background) background->setGeometry(rect());
    if (!dragging) moveSheet(yFor(position));
}

bool DraggableBottomSheet::eventFilter(QObject *watched, QEvent *event) {
    if (watched != grabArea && watched->parent() != grabArea)
        return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::MouseButtonPress) {
        auto mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) return false;
        animation->stop();
        dragging = true;
        pressGlobal = mouse->globalPosition().toPoint();
        pressY = sheet->y();
        return true;
    }
    if (event->type() == QEvent::MouseMove && dragging) {
        auto mouse = static_cast<QMouseEvent *>(event);
        moveSheet(pressY + mouse->globalPosition().toPoint().y() - pressGlobal.y());
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && dragging) {
        dragging = false;
        const int delta = sheet->y() - pressY;
        if (delta < -35) snap(position == Collapsed ? Resting : Expanded);
        else if (delta > 35) snap(position == Expanded ? Resting : Collapsed);
        else {
            const QList<Position> states{Collapsed, Resting, Expanded};
            auto nearest = states.first();
            for (auto state : states)
                if (qAbs(sheet->y() - yFor(state)) < qAbs(sheet->y() - yFor(nearest))) nearest = state;
            snap(nearest);
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
