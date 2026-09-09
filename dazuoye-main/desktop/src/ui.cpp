#include "ui.h"
#include <QDesktopServices>
#include <QPainterPath>
#include <QSvgRenderer>
#include <cmath>
namespace {
const QJsonObject &webDesign() {
    static QJsonObject data = [] {
        QFile f(":/assets/web-ui.json");
        if (!f.open(QIODevice::ReadOnly))
            return QJsonObject{};
        return QJsonDocument::fromJson(f.readAll()).object();
    }();
    return data;
}
} // namespace
QString text(const QJsonObject &o, const QString &key, const QString &fallback) {
    auto v = o.value(key);
    return v.isNull() || v.isUndefined() ? fallback : v.toVariant().toString();
}
double number(const QJsonObject &o, const QString &key) {
    return o.value(key).toVariant().toDouble();
}
QString money(double v) {
    return QLocale(QLocale::Chinese).toString(v, 'f', 2);
}
QString uid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QString statusText(const QString &s) {
    static QMap<QString, QString> m = {
        {"available", "可用"},   {"reserved", "已预约"},  {"charging", "充电中"},
        {"pending_payment", "待支付"}, {"pending", "待审核"}, {"paid", "模拟到账"}, {"rejected", "已驳回"},
        {"completed", "已完成"}, {"cancelled", "已取消"}, {"faulted", "故障"},
        {"active", "正常"},      {"frozen", "已冻结"},    {"fast", "快充"},
        {"slow", "慢充"},        {"recharge", "充值"},    {"settlement", "结算"},
        {"charge", "充电扣款"},  {"adjustment", "调账"},  {"refund", "退款"},
        {"withdrawal", "模拟提现"}, {"target_reached", "已达到充电上限"},
        {"balance_exhausted", "余额耗尽"}, {"user_stopped", "用户停止"},
        {"reservation_expired", "预约超时"}, {"user_cancelled", "用户取消"}};
    return m.value(s, s);
}

QIcon appIcon(const QString &name, const QColor &color) {
    QPixmap pix(48, 48);
    pix.setDevicePixelRatio(2);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const auto iconPath = webDesign().value("icons").toObject().value(name).toString();
    if (!iconPath.isEmpty()) {
        QByteArray svg =
            ("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path d='" + iconPath +
             "' fill='none' stroke='" + color.name() +
             "' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'/></svg>")
                .toUtf8();
        QSvgRenderer renderer(svg);
        renderer.render(&p, QRectF(0, 0, 24, 24));
        return QIcon(pix);
    }
    if (name == "brand") {
        p.setPen(Qt::NoPen);
        QLinearGradient g(0, 0, 24, 24);
        g.setColorAt(0, QColor("#f2bcff"));
        g.setColorAt(1, QColor("#8c27d8"));
        p.setBrush(g);
        p.drawRoundedRect(QRectF(1, 1, 22, 22), 7, 7);
        p.setBrush(QColor("#170c25"));
        p.drawPolygon(QPolygonF{{3, 11}, {21, 11}, {16, 20}, {0, 20}});
    } else if (name == "grid") {
        for (int x : {4, 14})
            for (int y : {4, 14})
                p.drawRoundedRect(QRectF(x, y, 6, 6), 1, 1);
    } else if (name == "car") {
        p.drawRoundedRect(QRectF(3, 10, 18, 8), 2, 2);
        p.drawLine(5, 10, 7, 5);
        p.drawLine(7, 5, 17, 5);
        p.drawLine(17, 5, 19, 10);
        p.drawLine(6, 18, 6, 21);
        p.drawLine(18, 18, 18, 21);
        p.drawLine(6, 13, 8, 13);
        p.drawLine(16, 13, 18, 13);
    } else if (name == "bolt") {
        QPolygonF shape;
        shape << QPointF(14, 2) << QPointF(5, 13) << QPointF(11, 13) << QPointF(9, 22)
              << QPointF(19, 10) << QPointF(13, 10);
        p.drawPolygon(shape);
    } else if (name == "pin" || name == "plug") {
        QPainterPath path;
        path.moveTo(12, 22);
        path.cubicTo(8, 16, 4, 13, 4, 9);
        path.cubicTo(4, -1, 20, -1, 20, 9);
        path.cubicTo(20, 13, 16, 17, 12, 22);
        p.drawPath(path);
        p.drawEllipse(QPointF(12, 9), 3, 3);
    } else if (name == "user") {
        p.drawEllipse(QPointF(12, 7), 4, 4);
        p.drawArc(QRectF(4, 12, 16, 17), 0, 180 * 16);
    } else if (name == "bell") {
        QPainterPath a;
        a.moveTo(4, 17);
        a.lineTo(6, 14);
        a.lineTo(6, 9);
        a.cubicTo(6, 1, 18, 1, 18, 9);
        a.lineTo(18, 14);
        a.lineTo(20, 17);
        a.closeSubpath();
        p.drawPath(a);
        p.drawArc(QRectF(9, 17, 6, 5), 180 * 16, 180 * 16);
    } else if (name == "chart") {
        p.drawLine(4, 3, 4, 21);
        p.drawLine(4, 21, 22, 21);
        p.drawLine(8, 17, 8, 12);
        p.drawLine(13, 17, 13, 5);
        p.drawLine(18, 17, 18, 9);
    } else if (name == "history") {
        p.drawEllipse(QRectF(3, 3, 18, 18));
        p.drawLine(12, 7, 12, 12);
        p.drawLine(12, 12, 16, 14);
    } else if (name == "settings") {
        for (int y : {6, 12, 18}) {
            p.drawLine(3, y, 21, y);
            p.setBrush(QColor("#1d112b"));
            p.drawEllipse(QPointF(y == 12 ? 16 : 8, y), 2.5, 2.5);
        }
    } else if (name == "arrow") {
        p.drawLine(5, 12, 20, 12);
        p.drawLine(15, 7, 20, 12);
        p.drawLine(15, 17, 20, 12);
    } else {
        p.drawEllipse(QRectF(4, 4, 16, 16));
        p.drawEllipse(QRectF(8, 8, 8, 8));
    }
    return QIcon(pix);
}
NavButton::NavButton(const QString &title, const QString &icon, bool stacked)
    : QPushButton(title), iconName(icon), vertical(stacked) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet(
        QString("padding:0;border:0;background:transparent;min-height:%1px;max-height:%1px;")
            .arg(stacked ? 63 : 42));
    setMinimumWidth(stacked ? 60 : 106);
    setFixedHeight(stacked ? 63 : 42);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}
void NavButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor ink = isChecked() ? QColor("#df9bfb") : QColor("#a99ab9");
    if (!vertical && (isChecked() || underMouse())) {
        QLinearGradient g(0, 0, width(), height());
        g.setColorAt(0, QColor("#533170"));
        g.setColorAt(1, QColor("#2b1940"));
        p.setBrush(g);
        p.setPen(QColor("#77449c"));
        p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 16, 16);
        ink = QColor("#f4e5ff");
    }
    if (vertical) {
        appIcon(iconName, ink).paint(&p, QRect((width() - 24) / 2, 7, 24, 24));
        p.setPen(ink);
        p.drawText(QRect(0, 36, width(), 23), Qt::AlignCenter, text());
    } else {
        appIcon(iconName, ink).paint(&p, QRect(11, (height() - 18) / 2, 18, 18));
        p.setPen(ink);
        p.drawText(rect().adjusted(36, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
}
QWidget *valueBlock(const QString &value, const QString &unit, const QString &caption) {
    auto w = new QWidget;
    auto l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(5);
    auto n = new QLabel;
    l->addWidget(n);
    n->setText("<span style='font-size:25px;font-weight:600;color:#f6edff'>" +
               value.toHtmlEscaped() + "</span> <span style='font-size:10px;color:#b5a0c5'>" +
               unit.toHtmlEscaped() + "</span>");
    l->addWidget(label(caption, "font-size:10px;color:#a491b7;"));
    return w;
}
QLabel *label(const QString &s, const QString &style, QWidget *p) {
    auto w = new QLabel(s, p);
    w->setTextFormat(Qt::PlainText);
    w->setWordWrap(true);
    w->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    w->setStyleSheet(style);
    return w;
}
QPushButton *button(const QString &s, QObject *ctx, std::function<void()> action, bool primary) {
    auto b = new QPushButton(s);
    b->setCursor(Qt::PointingHandCursor);
    b->setProperty("primary", primary);
    QObject::connect(b, &QPushButton::clicked, ctx, std::move(action));
    return b;
}
QFrame *card(const QString &title, QVBoxLayout **out) {
    auto f = new QFrame;
    f->setObjectName("card");
    auto l = new QVBoxLayout(f);
    l->setContentsMargins(16, 16, 16, 16);
    l->setSpacing(12);
    if (!title.isEmpty()) {
        auto heading = new QHBoxLayout;
        heading->setSpacing(10);
        auto mark = label("");
        QString icon = "bolt";
        for (auto entry : QList<QPair<QString, QString>>{{"车辆", "car"},
                                                         {"站", "plug"},
                                                         {"订单", "history"},
                                                         {"审计", "history"},
                                                         {"操作", "history"},
                                                         {"时间", "history"},
                                                         {"统计", "chart"},
                                                         {"趋势", "chart"},
                                                         {"概览", "chart"},
                                                         {"目标", "target"},
                                                         {"分布", "target"},
                                                         {"活跃", "grid"},
                                                         {"用户", "users"},
                                                         {"账户", "users"}})
            if (title.contains(entry.first))
                icon = entry.second;
        mark->setPixmap(appIcon(icon).pixmap(18, 18));
        mark->setFixedSize(30, 30);
        mark->setAlignment(Qt::AlignCenter);
        mark->setStyleSheet("background:#30213e;border-radius:15px;");
        heading->addWidget(mark);
        heading->addWidget(label(title, "font-size:17px;font-weight:600;color:#f3e4fd;"), 1);
        l->addLayout(heading);
    }
    if (out)
        *out = l;
    return f;
}
QWidget *row(const QList<QWidget *> &widgets, const QList<int> &stretch) {
    auto w = new QWidget;
    auto l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(10);
    for (int i = 0; i < widgets.size(); i++)
        l->addWidget(widgets[i], stretch.value(i, 1));
    return w;
}
void clearLayout(QLayout *l) {
    while (auto item = l->takeAt(0)) {
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        if (item->layout())
            clearLayout(item->layout());
        delete item;
    }
}
void applyTheme(QApplication &app) {
    Q_INIT_RESOURCE(resources);
    const int fontId = QFontDatabase::addApplicationFont(":/assets/noto-sans-sc.ttf");
    QFontDatabase::addApplicationFont(":/assets/noto-sans-sc-semibold.ttf");
    app.setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#100a1a"));
    palette.setColor(QPalette::WindowText, QColor("#eee4f8"));
    palette.setColor(QPalette::Base, QColor("#1c1228"));
    palette.setColor(QPalette::AlternateBase, QColor("#261833"));
    palette.setColor(QPalette::Text, QColor("#eee4f8"));
    palette.setColor(QPalette::Button, QColor("#30203e"));
    palette.setColor(QPalette::ButtonText, QColor("#eee4f8"));
    palette.setColor(QPalette::Highlight, QColor("#674180"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#a18fae"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#a18fae"));
    app.setPalette(palette);
    installDialogPolicy(app);
    app.setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).value(0, "Noto Sans SC"), 10));
    app.setStyleSheet(R"(
QMainWindow,QDialog { background:#100a1a; color:#f6edff; }
QWidget { color:#eee4f8; font-size:13px; }
QWidget#canvas {background:qradialgradient(cx:.4,cy:0,radius:1,fx:.4,fy:0,stop:0 #583456,stop:.65 #100a1a);}
QWidget#root {background:#100b1b;border:3px solid #4b3d57;border-radius:28px;}
QFrame#navigation {background:#170f24;border:1px solid #3c294e;border-radius:19px;}
QWidget#root[mobile="true"] {background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #08060f,stop:1 #140b23);border:1px solid #4b3859;border-radius:32px;}
QFrame#station-row {background:#2c1e39;border:1px solid #43304f;border-radius:12px;}
QPushButton[quiet="true"] {background:transparent;border:0;padding:6px;color:#b9a2c9;}
QPushButton[round="true"] {border-radius:18px;padding:7px;background:#1d122b;}

QFrame#card { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #20152d,stop:1 #130d20); border:1px solid #3a2948; border-radius:14px; }
QLabel { background:transparent; border:0; }
QPushButton {background:#251632;border:1px solid #463053;border-radius:12px;padding:10px 16px; min-height:18px;}
QPushButton:hover {background:#49305d;border-color:#c68bd8;}
QPushButton:checked {background:#432456;border-color:#d798df;}
QPushButton[primary="true"] {background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #8425e8,stop:.55 #ae50e1,stop:1 #e798d7);color:white;border:1px solid #d48aec;}
QPushButton:disabled {color:#786b85;background:#201728;}
QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox,QTextEdit {background:#1c1228;border:1px solid #50365f;border-radius:9px;padding:9px;selection-background-color:#8a4fbc;}
QComboBox QAbstractItemView {background:#281933;selection-background-color:#674180;}
QScrollArea {border:0;background:transparent;} QScrollArea>QWidget>QWidget {background:transparent;}
QAbstractItemView {background:#1c1228;color:#eee4f8;alternate-background-color:#261833;border:0;selection-background-color:#563168;selection-color:white;}
QTableWidget {gridline-color:#382441;}
QTableCornerButton::section {background:#30203e;border:0;}
QHeaderView::section {background:#30203e;color:#bd9fcb;border:0;padding:10px;}
QScrollBar:vertical {background:transparent;width:8px;} QScrollBar::handle:vertical {background:#604274;border-radius:4px;min-height:25px;} QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;}
QScrollBar:horizontal {background:#1c1228;height:10px;} QScrollBar::handle:horizontal {background:#806091;border-radius:4px;min-width:30px;} QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal {width:0;} QScrollBar::add-page,QScrollBar::sub-page {background:#1c1228;}
QTabBar::tab {background:#241631;padding:12px;border-radius:8px;} QTabBar::tab:selected {background:#6a3988;}
QToolTip {background:#3b2550;color:white;border:1px solid #ae74cc;}
)");
}
QWidget *dialogHeader(QDialog *dialog, QLabel *title) {
    auto header = new QWidget(dialog);
    header->setObjectName("dialog-header");
    auto layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 4);
    title->setWordWrap(true);
    layout->addWidget(title, 1);
    auto close = new QPushButton("× 关闭", header);
    close->setObjectName("dialog-close");
    close->setAccessibleName("关闭弹窗");
    close->setToolTip("关闭弹窗（Esc）");
    close->setCursor(Qt::PointingHandCursor);
    close->setAutoDefault(false);
    close->setDefault(false);
    close->setMinimumSize(84, 40);
    layout->addWidget(close, 0, Qt::AlignTop);
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::reject);
    return header;
}
void showDetail(QWidget *p, const QString &title, const QJsonObject &data) {
    auto d = new QDialog(p);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(title);
    auto l = new QVBoxLayout(d);
    l->addWidget(dialogHeader(d, label(title, "font-size:22px;")));
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto content = new QWidget;
    auto form = new QFormLayout(content);
    form->setContentsMargins(14, 14, 14, 14);
    form->setVerticalSpacing(15);
    form->setLabelAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto fields = webDesign().value("fields").toObject();
    for(auto pair:QList<QPair<QString,QString>>{
        {"requested_at","申请时间"},{"decided_at","审核时间"},{"destination","演示收款账户"},
        {"review_note","审核说明"},{"reviewer_id","审核人编号"},{"request_key","申请编号"},
        {"start_hour","开始小时（北京时间）"},{"end_hour","结束小时（北京时间）"},{"start","时段开始"},{"end","时段结束"},{"electricity_price","电费 元/kWh"},
        {"service_price","服务费 元/kWh"},{"unit_price","合计单价 元/kWh"},{"energy_kwh","电量 kWh"},
        {"tariff_snapshot","预约时价格"},{"billing_detail","分时明细"},{"held_balance","冻结金额"},
        {"available_balance","可用余额"},{"vehicle_name","车型"},{"vehicle_plate","车牌"},{"paid_at","付款时间"},
        {"battery_kwh","车辆电池容量 kWh"},{"initial_soc","开始充电时电量（%）"},
        {"vehicle_soc","结束充电时电量（%）"},{"target_energy_kwh","计划充电量 kWh"},
        {"balance_before","订单前余额"},{"balance_after","订单后余额"},
        {"address_verified","地址是否已核验"},{"online_rate","设备在线率（%）"},
        {"online_count","在线设备数量"},{"tariff","分时价格方案"}})
        fields.insert(pair.first,pair.second);
    fields.insert("entry_type", "收支类型"); fields.insert("balance_after", "交易后余额");
    fields.insert("created_at", "创建时间"); fields.insert("order_id", "订单编号");
    fields.insert("amount", "金额"); fields.insert("id", "记录编号");
    fields.insert("code", "电桩编号"); fields.insert("status", "状态");
    fields.insert("power_kw", "功率 kW"); fields.insert("total_sessions", "累计充电次数");
    fields.insert("total_minutes", "累计运行分钟");
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (it.key() == "avatar_path")
            continue;
        if (it.value().isArray()) {
            auto rows = it.value().toArray();
            auto table = new QTableWidget;
            QStringList keys;
            for (auto v : rows) for (auto key : v.toObject().keys())
                if (!keys.contains(key)) keys << key;
            table->setColumnCount(keys.size()); table->setRowCount(rows.size());
            QStringList headings; for (auto key : keys) headings << fields.value(key).toString(key);
            table->setHorizontalHeaderLabels(headings);
            for (int i = 0; i < rows.size(); ++i)
                for (int j = 0; j < keys.size(); ++j)
                    table->setItem(i, j, new QTableWidgetItem(statusText(text(rows[i].toObject(), keys[j]))));
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
            const bool compactPricingTable = it.key() == "billing_detail" || it.key() == "tariff_snapshot";
            if (compactPricingTable) {
                table->setFixedHeight(165);
                table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
                table->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            } else {
                table->setMinimumHeight(280);
            }
            form->addRow(fields.value(it.key()).toString(it.key()), table);
            continue;
        }
        QString value;
        if (it.value().isObject() || it.value().isArray())
            value = QString::fromUtf8(
                it.value().isObject()
                    ? QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Indented)
                    : QJsonDocument(it.value().toArray()).toJson(QJsonDocument::Indented));
        else
            value = statusText(it.value().toVariant().toString());
        auto field =
            label(fields.value(it.key()).toString(it.key()), "font-size:12px;color:#a58ab8;");
        auto content = label(value, "font-size:13px;");
        content->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(field, content);
    }
    scroll->setWidget(content);
    l->addWidget(scroll);
    l->addWidget(button("关闭", d, [d] { d->accept(); }, true));
    d->resize(620, 560);
    d->show();
}
void command(QWidget *p, ApiClient *api, const QString &method, const QString &path,
             QJsonObject body, std::function<void(const Reply &)> done) {
    api->request(method, path, body, p, [p, done](const Reply &r) {
        if (!r.ok) {
            QMessageBox::warning(p, "操作未完成", r.error);
            return;
        }
        done(r);
    });
}
void editForm(QWidget *p, ApiClient *api, const QString &title, const QString &method,
              const QString &path, const QList<QPair<QString, QString>> &fields, QJsonObject values,
              std::function<void()> done) {
    auto d = new QDialog(p);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(title);
    auto l = new QVBoxLayout(d);
    l->addWidget(dialogHeader(d, label(title, "font-size:22px;")));
    auto f = new QFormLayout;
    QMap<QString, QLineEdit *> editors;
    for (const auto &field : fields) {
        auto e = new QLineEdit(text(values, field.first, ""));
        e->setObjectName(field.first);
        if (field.first == "password" || field.first == "code_otp")
            e->setEchoMode(QLineEdit::Password);
        f->addRow(field.second, e);
        editors[field.first] = e;
    }
    l->addLayout(f);
    auto err = label("", "color:#f5a0c3;");
    l->addWidget(err);
    auto save = new QPushButton("确认保存");
    save->setProperty("primary", true);
    l->addWidget(save);
    QObject::connect(save, &QPushButton::clicked, d, [=] {
        QJsonObject body = values;
        for (auto it = editors.begin(); it != editors.end(); ++it)
            body[it.key()] = it.value()->text().trimmed();
        save->setEnabled(false);
        api->request(method, path, body, d, [=](const Reply &r) {
            save->setEnabled(true);
            if (!r.ok) {
                err->setText(r.error);
                return;
            }
            d->accept();
            done();
        });
    });
    d->setMinimumWidth(420);
    if (title == "部分退款")
        d->resize(460, 380);
    else if (title == "新增记录")
        d->resize(460, 360);
    d->show();
}
QWidget *picture(const QString &name, int height) {
    auto w = new QLabel;
    auto pix = QPixmap(":/assets/" + name);
    w->setPixmap(pix.scaled(460, height, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    w->setAlignment(Qt::AlignCenter);
    return w;
}
ArtWidget::ArtWidget(Kind k, QWidget *p) : QWidget(p), kind(k) {
    setMinimumHeight(190);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (k == Ring) {
        animation.setInterval(50);
        connect(&animation, &QTimer::timeout, this, [this] {
            phase += .02;
            if (isVisible())
                update();
        });
        animation.start();
    }
}
void ArtWidget::setValue(double v, const QString &s) {
    value = v;
    setProperty("displayValue", v);
    setAccessibleName(s);
    setAccessibleDescription(v < 0 ? "暂无数据" : QString::number(v));
    caption = s;
    update();
}
void ArtWidget::setValues(const QList<double> &v, const QStringList &labels) {
    categories = labels;
    values = v;
    update();
}
void ArtWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double w = width(), h = height();
    p.setPen(QColor("#dac6ec"));

    if (kind == UserHero) {
        auto write = [&](QRectF rect, QString s, int size, QColor color = Qt::white) {
            QFont f = font();
            f.setPixelSize(size);
            p.setFont(f);
            p.setPen(color);
            p.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, s);
        };
        QLinearGradient glass(12, 0, 55, 0);
        glass.setColorAt(0, QColor("#8d88a6"));
        glass.setColorAt(.45, QColor("#ded8ee"));
        glass.setColorAt(1, QColor("#635773"));
        p.setPen(Qt::NoPen);
        p.setBrush(glass);
        p.drawRoundedRect(QRectF(5, 29, 44, 80), 10, 10);
        p.setBrush(QColor("#75d4b1"));
        p.drawRoundedRect(QRectF(10, 60, 34, 43), 5, 5);
        p.setBrush(QColor("#cfc4e8"));
        p.drawEllipse(QRectF(5, 25, 44, 11));
        p.drawRoundedRect(QRectF(21, 19, 13, 6), 3, 3);
        appIcon("bolt", QColor("#ffe09c")).paint(&p, QRect(9, 42, 36, 55));
        write(QRectF(66, 30, 160, 24), "电池电量", 13, QColor("#b3a0c5"));
        write(QRectF(64, 55, 155, 60), value < 0 ? "—" : QString::number(value, 'f', 0) + "%", 40);
        for (int i = 0; i < 2; i++) {
            QRectF r(5, 132 + i * 80, w * .40, 68);
            p.setPen(QPen(QColor("#41304e"), 1.2));
            p.setBrush(QColor("#110b1b"));
            p.drawRoundedRect(r, 10, 10);
            appIcon(i ? "bolt" : "car").paint(&p, QRect(16, int(r.y() + 14), 18, 18));
            write(r.adjusted(38, 6, -2, -40), i ? "充电上限" : "电池容量", 12, QColor("#b9a3ca"));
            write(r.adjusted(20, 34, 40, -5), values.value(i,-1) < 0 ? "—" : QString::number(values.value(i), 'f', 0) + (i ? " %" : " kWh"), 23);
        }
        QPixmap car(":/assets/ev-photo.png");
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const double side = h * 1.41;
        p.drawPixmap(QRectF(w * .36, -39, side, side), car, car.rect());
        write(QRectF(w - 82, h - 21, 80, 18), "演示车辆", 9, QColor("#766184"));
        return;
    }
    if (kind == Car || kind == TopCar) {
        QPixmap image(":/assets/" +
                      QString(kind == Car ? "admin-xray-car.png" : "ev-top-photo.png"));
        auto size = image.size().scaled(QSize(width() - 4, height() - 4), Qt::KeepAspectRatio);
        QRectF target((w - size.width()) / 2, (h - size.height()) / 2, size.width(), size.height());
        p.drawPixmap(target, image, image.rect());
        if (kind == Car) {
            QPainterPath capsule;
            capsule.addRoundedRect(target.adjusted(4, 6, -4, -6), target.height() / 2,
                                   target.height() / 2);
            p.save();
            p.setClipPath(capsule);
            QLinearGradient tint(target.center().x(), 0, target.right(), 0);
            tint.setColorAt(0, QColor("#b0073039"));
            tint.setColorAt(1, QColor("#c002a0c7"));
            p.fillRect(
                QRectF(target.center().x() + 14, target.top(), target.width() / 2, target.height()),
                tint);
            p.setPen(QPen(QColor("#72f5ed"), 1));
            p.drawLine(QPointF(target.center().x() + 14, target.top()),
                       QPointF(target.center().x() + 14, target.bottom()));
            p.translate(target.right() - target.width() * .2, target.center().y());
            p.rotate(-90);
            QFont f = font();
            f.setPixelSize(33);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(QColor("#d3fbff"));
            p.drawText(QRectF(-65, -23, 130, 46), Qt::AlignCenter, value < 0 ? "—" : QString::number(value, 'f', 0) + "%");
            p.restore();
        }
        return;
    }
    if (kind == Gauge) {
        double radius = qMin((w - 80) / 2, h - 66);
        QPointF center(w / 2, h - 29);
        QRectF arc(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
        for (int i = 0; i <= 50; i++) {
            double a = 3.14159265358979323846 - i * 3.14159265358979323846 / 50;
            auto point = [&](double r) {
                return center + QPointF(std::cos(a) * r, -std::sin(a) * r);
            };
            p.setPen(QPen(QColor(i % 5 ? "#7e658c" : "#bfa5ce"), i % 5 ? 1 : 1.5));
            p.drawLine(point(radius + 10), point(radius + (i % 5 ? 13 : 17)));
            if (i % 10 == 0) {
                p.setPen(QColor("#bba3cc"));
                QFont f = font();
                f.setPixelSize(11);
                p.setFont(f);
                QPointF t = point(radius + 28);
                p.drawText(QRectF(t.x() - 16, t.y() - 9, 32, 18), Qt::AlignCenter,
                           QString::number(i * 2));
            }
        }
        p.setPen(QPen(QColor("#291a38"), 23, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(arc, 0, 180 * 16);
        QLinearGradient gradient(arc.left(), 0, arc.right(), 0);
        gradient.setColorAt(0, QColor("#8942ec"));
        gradient.setColorAt(1, QColor("#ec9cdf"));
        p.setPen(QPen(QBrush(gradient), 23, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(arc, 180 * 16, -int(qBound(0., value, 100.) * 1.8 * 16));
        QRectF inner = arc.adjusted(35, 35, -35, -35);
        p.setPen(QPen(QColor("#281b37"), 15, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(inner, 0, 180 * 16);
        const double secondary = qBound(0., values.value(0), 100.);
        const int dots = qCeil(secondary / 4.);
        for (int i = 0; i < dots; i++) {
            const double a = 3.14159265358979323846 * (1. - (i + .5) / 25.);
            QPointF t = center + QPointF(std::cos(a) * (radius - 35), -std::sin(a) * (radius - 35));
            p.setPen(Qt::NoPen);
            QLinearGradient dot(t - QPointF(8, 0), t + QPointF(8, 0));
            dot.setColorAt(0, QColor("#8e47e7")); dot.setColorAt(1, QColor("#e9a3e5"));
            p.setBrush(dot); p.drawEllipse(t, 7, 7);
        }
        return;
    }
    if (kind == Ring) {
        double size = qMin(w, h) - 18;
        QRectF area((w - size) / 2, (h - size) / 2, size, size);
        QRadialGradient glow(area.center(), size * .52);
        glow.setColorAt(0, QColor("#030114"));
        glow.setColorAt(.66, QColor("#30065c"));
        glow.setColorAt(1, QColor("#00471173"));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(area);
        QPixmap image(":/assets/charging-plasma.png");
        p.save();
        p.translate(w / 2, h / 2);
        p.rotate(std::sin(phase) * 4);
        p.drawPixmap(QRectF(-size / 2, -size / 2, size, size), image, image.rect());
        p.restore();
        QFont f = font();
        f.setPixelSize(47);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, value < 0 ? "—" : QString::number(value, 'f', 0) + "%");
        return;
    }
    if (kind == Flow) {
        const double peak = values.isEmpty() ? 0 : *std::max_element(values.begin(),values.end());
        if (peak <= 0) { p.drawText(rect(),Qt::AlignCenter,"暂无已结算营收"); return; }
        const int count = values.size();
        const double step = w / count, bw = qMin(100., step * .5), base = h - 32;
        auto top = [&](int i) { return base - values[i]/peak*(h-78); };
        for (int i=0;i<count;++i) {
            const double x=step*(i+.5)-bw/2;
            if (i+1<count) {
                const double next=x+step;
                QPainterPath ribbon; ribbon.moveTo(x+bw,top(i));
                ribbon.cubicTo(x+bw+step*.3,top(i),next-step*.3,top(i+1),next,top(i+1));
                ribbon.lineTo(next,base); ribbon.lineTo(x+bw,base); ribbon.closeSubpath();
                p.fillPath(ribbon,QColor("#553a255f"));
            }
            QLinearGradient g(0,top(i),0,base); g.setColorAt(0,QColor("#e89edb"));g.setColorAt(.5,QColor("#a563d5"));g.setColorAt(1,QColor("#63348d"));
            p.setPen(Qt::NoPen);p.setBrush(g);
            if(values[i]>0)p.drawRoundedRect(QRectF(x,top(i),bw,base-top(i)),7,7);
            p.setPen(QColor("#cfb9dd"));
            p.drawText(QRectF(x-25,top(i)-27,bw+50,22),Qt::AlignCenter,"¥"+money(values[i]));
            p.drawText(QRectF(x-30,base+9,bw+60,20),Qt::AlignCenter,categories.value(i));
        }
        return;
    }
    if (kind == Spark) {
        if (values.isEmpty()) {
            p.drawText(rect(), Qt::AlignCenter, "暂无记录");
            return;
        }
        double max = *std::max_element(values.begin(), values.end());
        max = qMax(1., max);
        QPainterPath path;
        for (int i = 0; i < values.size(); i++) {
            QPointF pt(15 + i * (w - 30) / qMax(1, values.size() - 1),
                       h - 25 - values[i] / max * (h - 50));
            if (i == 0)
                path.moveTo(pt);
            else
                path.lineTo(pt);
        }
        auto fill = path;
        fill.lineTo(w - 15, h - 25);
        fill.lineTo(15, h - 25);
        QLinearGradient g(0, 0, 0, h);
        g.setColorAt(0, QColor("#ba6add"));
        g.setColorAt(1, QColor("#241532"));
        p.fillPath(fill, g);
        p.setPen(QPen(QColor("#e99cdd"), 2.5));
        p.drawPath(path);
    }
}
DesktopWindow::DesktopWindow(ApiClient *a, CacheStore *c, const QString &title) : api(a), cache(c) {
    setWindowTitle(title);
    root = new QWidget;
    root->setObjectName("root");
    auto canvas = new QWidget;
    canvas->setObjectName("canvas");
    auto frame = new QVBoxLayout(canvas);
    frame->setContentsMargins(22, 28, 22, 22);
    frame->addWidget(root);
    setCentralWidget(canvas);
    outer = new QVBoxLayout(root);
    outer->setContentsMargins(20, 20, 20, 12);
    outer->setSpacing(14);
    nav = new QHBoxLayout;
    outer->addLayout(nav);
    auto scroll = new QScrollArea;
    scroll->setObjectName("page-scroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    content = new QWidget;
    body = new QVBoxLayout(content);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(12);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    connection = label("正在连接统一业务服务…", "color:#ac91bb;font-size:11px;");
    outer->addWidget(connection);
    connect(api, &ApiClient::connectionChanged, this, [this](bool online) {
        connection->setText(online ? "● 服务已连接" : "● 离线只读 · 禁止提交业务操作");
    });
    connect(api, &ApiClient::streamChanged, this, [this](bool online) {
        if (online)
            connection->setText("● 服务已连接 · 实时事件同步中");
    });
}
void DesktopWindow::message(const Reply &r) {
    if (r.cached)
        connection->setText("● 离线只读缓存 · " + r.cachedAt);
    else if (!r.ok)
        connection->setText(r.error);
    else
        connection->setText("● 服务已连接 · 数据由服务器确认");
}
void DesktopWindow::openUrl(const QString &path) {
    QDesktopServices::openUrl(api->baseUrl().resolved(QUrl(path)));
}
void DesktopWindow::screenshot(const QString &dir, const QStringList &pages, int delay) {
    QDir().mkpath(dir);
    auto index = std::make_shared<int>(0);
    auto tick = new QTimer(this);
    tick->setInterval(delay);
    connect(tick, &QTimer::timeout, this, [=] {
        if (*index > 0)
            grab().save(dir + "/" + pages[*index - 1] + ".png");
        if (*index >= pages.size()) {
            tick->stop();
            qApp->quit();
            return;
        }
        navigate(pages[*index]);
        ++*index;
    });
    tick->start();
}
