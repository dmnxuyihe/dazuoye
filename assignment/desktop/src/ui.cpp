#include "ui.h"
#include <QDesktopServices>
#include <QPainterPath>
#include <cmath>
QString text(const QJsonObject &o, const QString &key, const QString &fallback) {
    // JSON 字段不存在或为 null 时返回统一占位文本，避免页面出现空白。
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
    // 服务端保留英文枚举，显示层在这里集中映射为中文；未知值原样显示便于排错。
    static QMap<QString, QString> m = {
        {"available", "可用"},   {"reserved", "已预约"},  {"charging", "充电中"},
        {"completed", "已完成"}, {"cancelled", "已取消"}, {"faulted", "故障"},
        {"active", "正常"},      {"frozen", "已冻结"},    {"fast", "快充"},
        {"slow", "慢充"},        {"recharge", "充值"},    {"settlement", "结算"},
        {"charge", "充电扣款"},  {"adjustment", "调账"},  {"refund", "退款"}};
    return m.value(s, s);
}
QLabel *label(const QString &s, const QString &style, QWidget *p) {
    // 强制纯文本可避免服务端字符串被 QLabel 当成富文本解析。
    auto w = new QLabel(s, p);
    w->setTextFormat(Qt::PlainText);
    w->setWordWrap(true);
    w->setStyleSheet(style);
    return w;
}
QPushButton *button(const QString &s, QObject *ctx, std::function<void()> action, bool primary) {
    // clicked 信号连接到调用者提供的动作；ctx 销毁时 Qt 会自动解除连接。
    auto b = new QPushButton(s);
    b->setCursor(Qt::PointingHandCursor);
    b->setProperty("primary", primary);
    QObject::connect(b, &QPushButton::clicked, ctx, std::move(action));
    return b;
}
QFrame *card(const QString &title, QVBoxLayout **out) {
    // 创建统一卡片容器，并可通过 out 把内部布局返回给调用方继续填充内容。
    auto f = new QFrame;
    f->setObjectName("card");
    auto l = new QVBoxLayout(f);
    l->setContentsMargins(22, 20, 22, 20);
    l->setSpacing(14);
    if (!title.isEmpty())
        l->addWidget(label(title, "font-size:16px;font-weight:600;color:#f3e4fd;"));
    if (out)
        *out = l;
    return f;
}
QWidget *row(const QList<QWidget *> &widgets, const QList<int> &stretch) {
    // 按给定伸缩系数横向排列控件；未指定时每项默认占一份空间。
    auto w = new QWidget;
    auto l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(18);
    for (int i = 0; i < widgets.size(); i++)
        l->addWidget(widgets[i], stretch.value(i, 1));
    return w;
}
void clearLayout(QLayout *l) {
    // 递归清空动态页面。deleteLater 让控件在当前事件处理结束后安全销毁。
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
    // 注册 qrc 字体/图片资源，并在应用级样式表中统一所有原生控件外观。
    Q_INIT_RESOURCE(resources);
    QFontDatabase::addApplicationFont(":/assets/noto-sans-sc.ttf");
    app.setStyle("Fusion");
    app.setFont(QFont("Noto Sans CJK SC", 10));
    app.setStyleSheet(R"(
QMainWindow,QDialog { background:#100a1a; color:#f6edff; }
QWidget { color:#eee4f8; font-size:13px; }
QWidget#root { background:qradialgradient(cx:.65,cy:0,radius:1,fx:.65,fy:0,stop:0 #38213f,stop:.5 #100a1a); }
QFrame#card { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #291936,stop:1 #1a1126); border:1px solid #3b294a; border-radius:18px; }
QLabel { background:transparent; border:0; }
QPushButton {background:#251632;border:1px solid #463053;border-radius:12px;padding:10px 16px; min-height:18px;}
QPushButton:hover {background:#49305d;border-color:#c68bd8;}
QPushButton:checked {background:#432456;border-color:#d798df;}
QPushButton[primary="true"] {background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #8425e8,stop:.55 #ae50e1,stop:1 #e798d7);color:white;border:1px solid #d48aec;}
QPushButton:disabled {color:#786b85;background:#201728;}
QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox,QTextEdit {background:#1c1228;border:1px solid #50365f;border-radius:9px;padding:9px;selection-background-color:#8a4fbc;}
QComboBox QAbstractItemView {background:#281933;selection-background-color:#674180;}
QScrollArea {border:0;background:transparent;} QScrollArea>QWidget>QWidget {background:transparent;}
QTableWidget {background:#1c1228;alternate-background-color:#261833;border:0;gridline-color:#382441;selection-background-color:#563168;}
QHeaderView::section {background:#30203e;color:#bd9fcb;border:0;padding:10px;}
QScrollBar:vertical {background:transparent;width:8px;} QScrollBar::handle:vertical {background:#604274;border-radius:4px;min-height:25px;} QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;}
QTabBar::tab {background:#241631;padding:12px;border-radius:8px;} QTabBar::tab:selected {background:#6a3988;}
QToolTip {background:#3b2550;color:white;border:1px solid #ae74cc;}
)");
}
void showDetail(QWidget *p, const QString &title, const QJsonObject &data) {
    // 非模态 JSON 详情窗用于审计/调试；关闭后由 WA_DeleteOnClose 自动释放。
    auto d = new QDialog(p);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(title);
    auto l = new QVBoxLayout(d);
    l->addWidget(label(title, "font-size:22px;"));
    auto t = new QTextEdit;
    t->setReadOnly(true);
    t->setPlainText(QJsonDocument(data).toJson(QJsonDocument::Indented));
    l->addWidget(t);
    d->resize(620, 560);
    d->show();
}
void command(QWidget *p, ApiClient *api, const QString &method, const QString &path,
             QJsonObject body, std::function<void(const Reply &)> done) {
    // 写操作的统一包装：失败弹窗，成功才继续执行页面传入的后续逻辑。
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
    // 根据字段元数据动态生成编辑框，使多个后台管理表单复用同一套提交逻辑。
    auto d = new QDialog(p);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(title);
    auto l = new QVBoxLayout(d);
    l->addWidget(label(title, "font-size:22px;"));
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
    // 保存按钮 clicked 信号收集全部输入；以 d 为上下文可防止关窗后回调悬空。
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
    d->show();
}
QWidget *picture(const QString &name, int height) {
    // 从编译进 EXE 的 qrc 读取图片，并保持宽高比做平滑缩放。
    auto w = new QLabel;
    auto pix = QPixmap(":/assets/" + name);
    w->setPixmap(pix.scaled(460, height, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    w->setAlignment(Qt::AlignCenter);
    return w;
}
ArtWidget::ArtWidget(Kind k, QWidget *p) : QWidget(p), kind(k) {
    // 地图只需在构造时解析一次道路数据；其他重绘直接复用内存中的几何数据。
    setMinimumHeight(k == Map ? 240 : 190);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (k == Map) {
        QFile f(":/assets/shenzhen-roads.geojson");
        if (f.open(QIODevice::ReadOnly))
            roads = QJsonDocument::fromJson(f.readAll()).object().value("features").toArray();
    }
    if (k == Ring) {
        animation.setInterval(50);
        // timeout 每 50ms 推进相位；仅可见时 update，减少后台页面无意义绘制。
        connect(&animation, &QTimer::timeout, this, [this] {
            phase += .02;
            if (isVisible())
                update();
        });
        animation.start();
    }
}
void ArtWidget::setValue(double v, const QString &s) {
    // 修改绘图模型后调用 update()，Qt 会合并请求并稍后触发 paintEvent。
    value = v;
    caption = s;
    update();
}
void ArtWidget::setStations(const QJsonArray &r, const QString &s) {
    stations = r;
    selected = s;
    update();
}
void ArtWidget::setValues(const QList<double> &v) {
    values = v;
    update();
}
void ArtWidget::paintEvent(QPaintEvent *) {
    // 所有图形均使用 QPainter 原生绘制，不依赖 WebEngine 或浏览器画布。
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double w = width(), h = height();
    p.setPen(QColor("#dac6ec"));
    // 车辆模式绘制本地位图；按控件尺寸等比居中，避免拉伸变形。
    if (kind == Car || kind == TopCar) {
        QPixmap image(":/assets/" +
                      QString(kind == Car ? "admin-xray-car.png" : "ev-top-photo.png"));
        auto size = image.size().scaled(QSize(width() - 10, height() - 10), Qt::KeepAspectRatio);
        p.drawPixmap(
            QRectF((w - size.width()) / 2, (h - size.height()) / 2, size.width(), size.height()),
            image, image.rect());
        return;
    }
    // Ring 是带轻微旋转动画的充电光环；Gauge 是按百分比绘制的半圆仪表。
    if (kind == Ring || kind == Gauge) {
        double size = qMin(w, h) - 35;
        QRectF r((w - size) / 2, (h - size) / 2, size, size);
        if (kind == Ring) {
            QPixmap image(":/assets/charging-plasma.png");
            p.save();
            p.translate(w / 2, h / 2);
            p.rotate(std::sin(phase) * 5);
            p.drawPixmap(QRectF(-size / 2, -size / 2, size, size), image, image.rect());
            p.restore();
        } else {
            p.setPen(QPen(QColor("#493051"), 16, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(r, 0, 180 * 16);
            QConicalGradient g(r.center(), 0);
            g.setColorAt(0, QColor("#eb9cdd"));
            g.setColorAt(1, QColor("#7936e1"));
            p.setPen(QPen(QBrush(g), 16, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(r, 180 * 16, -int(qBound(0., value, 100.) * 180 / 100 * 16));
        }
        p.setPen(Qt::white);
        p.setFont(QFont(font().family(), kind == Ring ? 38 : 30, QFont::DemiBold));
        p.drawText(rect(), Qt::AlignCenter, QString::number(value, 'f', 0) + "%");
        p.setFont(QFont(font().family(), 10));
        p.setPen(QColor("#b8a1c8"));
        p.drawText(QRectF(0, h / 2 + 35, w, 30), Qt::AlignCenter, caption);
        return;
    }
    // 把深圳经纬度范围线性投影到控件坐标，先画道路，再叠加可点击站点。
    if (kind == Map) {
        p.fillRect(rect(), QColor("#130f19"));
        hits.clear();
        auto project = [&](double lon, double lat) {
            return QPointF((lon - 113.78) / .84 * w, h - (lat - 22.43) / .43 * h);
        };
        p.setPen(QPen(QColor("#483344"), 1.2));
        for (const auto &v : roads) {
            auto g = v.toObject().value("geometry").toObject();
            auto coords = g.value("coordinates").toArray();
            QList<QJsonArray> lines;
            if (g.value("type") == "LineString")
                lines << coords;
            else if (g.value("type") == "MultiLineString")
                for (const auto &c : coords)
                    lines << c.toArray();
            for (const auto &line : lines) {
                QPainterPath path;
                bool first = true;
                for (const auto &c : line) {
                    auto a = c.toArray();
                    if (a.size() < 2)
                        continue;
                    auto pt = project(a[0].toDouble(), a[1].toDouble());
                    if (first)
                        path.moveTo(pt);
                    else
                        path.lineTo(pt);
                    first = false;
                }
                p.drawPath(path);
            }
        }
        for (const auto &v : stations) {
            auto o = v.toObject();
            auto pt = project(number(o, "longitude"), number(o, "latitude"));
            pt.setX(qBound(20., pt.x(), w - 20));
            pt.setY(qBound(25., pt.y(), h - 40));
            auto id = text(o, "id");
            hits << qMakePair(pt, id);
            bool on = id == selected;
            p.setPen(QPen(QColor(on ? "#f7b9ee" : "#a67cd0"), 2));
            p.setBrush(QColor(on ? "#aa4be0" : "#4b2f6b"));
            p.drawEllipse(pt, on ? 12 : 8, on ? 12 : 8);
            if (on) {
                p.setPen(QColor("#f6e9ff"));
                p.drawText(QRectF(qBound(0., pt.x() - 90, w - 180), pt.y() + 15, 180, 40),
                           Qt::AlignHCenter, text(o, "name"));
            }
        }
        p.setPen(QColor("#a789b6"));
        p.setFont(QFont(font().family(), 8));
        p.drawText(QRectF(10, h - 22, w - 20, 20), Qt::AlignRight,
                   "深圳道路 · © OpenStreetMap contributors");
        return;
    }
    // Flow 用堆叠柱和半透明贝塞尔带表达连续月份的能量构成变化。
    if (kind == Flow) {
        const QList<QColor> palette = {QColor("#d65ec9"), QColor("#edb154"), QColor("#8055ac"),
                                       QColor("#493064")};
        const double xs[] = {15, w * .43, w * .85}, bw = w * .14, base = h - 30;
        const double fractions[] = {.27, .31, .27, .15};
        auto level = [&](int col, int layer) {
            double below = 0;
            for (int k = 0; k < layer; k++)
                below += fractions[k];
            return base - (values.value(col, 60) / 100) * (h - 55) * below;
        };
        for (int col = 0; col < 3; col++) {
            for (int k = 0; k < 4; k++) {
                double bottom = level(col, k), top = level(col, k + 1);
                QLinearGradient gradient(0, top, 0, bottom);
                gradient.setColorAt(0, palette[k].lighter(120));
                gradient.setColorAt(1, palette[k]);
                p.setPen(Qt::NoPen);
                p.setBrush(gradient);
                p.drawRoundedRect(QRectF(xs[col], top, bw, qMax(2., bottom - top - 4)), 5, 5);
                if (col < 2) {
                    double nt = level(col + 1, k + 1), nb = level(col + 1, k);
                    QPainterPath ribbon;
                    ribbon.moveTo(xs[col] + bw + 3, top);
                    ribbon.cubicTo(xs[col] + bw + 30, top, xs[col + 1] - 30, nt, xs[col + 1] - 3,
                                   nt);
                    ribbon.lineTo(xs[col + 1] - 3, nb - 4);
                    ribbon.cubicTo(xs[col + 1] - 30, nb - 4, xs[col] + bw + 30, bottom - 4,
                                   xs[col] + bw + 3, bottom - 4);
                    ribbon.closeSubpath();
                    QColor color = palette[k];
                    color.setAlpha(65);
                    p.fillPath(ribbon, color);
                }
            }
            p.setPen(QColor("#c7adcf"));
            p.drawText(QRectF(xs[col] - 10, level(col, 4) - 25, bw + 20, 20), Qt::AlignCenter,
                       QString("%1%").arg(values.value(col)));
            p.drawText(QRectF(xs[col], h - 25, bw, 20), Qt::AlignCenter,
                       QString("%1月").arg(10 + col));
        }
        return;
    }
    // Spark 将数值归一化到可用高度，绘制折线及渐变面积背景。
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
void ArtWidget::mousePressEvent(QMouseEvent *e) {
    // 地图点击采用 22 像素容差选择最近站点，命中后发出 stationSelected 信号。
    if (kind != Map)
        return;
    double best = 22;
    QString id;
    for (const auto &hit : hits) {
        double distance = QLineF(e->position(), hit.first).length();
        if (distance < best) {
            best = distance;
            id = hit.second;
        }
    }
    if (!id.isEmpty())
        emit stationSelected(id);
}
DesktopWindow::DesktopWindow(ApiClient *a, CacheStore *c, const QString &title) : api(a), cache(c) {
    // 基类搭建所有页面共有的“导航栏 + 可滚动内容 + 连接状态”骨架。
    setWindowTitle(title);
    root = new QWidget;
    root->setObjectName("root");
    setCentralWidget(root);
    outer = new QVBoxLayout(root);
    outer->setContentsMargins(24, 22, 24, 18);
    outer->setSpacing(18);
    nav = new QHBoxLayout;
    outer->addLayout(nav);
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    content = new QWidget;
    body = new QVBoxLayout(content);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(18);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    connection = label("正在连接统一业务服务…", "color:#ac91bb;font-size:11px;");
    outer->addWidget(connection);
    // REST 连通信号控制离线只读提示。
    connect(api, &ApiClient::connectionChanged, this, [this](bool online) {
        connection->setText(online ? "● 服务已连接" : "● 离线只读 · 禁止提交业务操作");
    });
    // WebSocket 连通信号进一步提示实时事件已经开始同步。
    connect(api, &ApiClient::streamChanged, this, [this](bool online) {
        if (online)
            connection->setText("● 服务已连接 · 实时事件同步中");
    });
}
void DesktopWindow::message(const Reply &r) {
    // 请求结束后把在线、错误或缓存时间统一反映到底部状态栏。
    if (r.cached)
        connection->setText("● 离线只读缓存 · " + r.cachedAt);
    else if (!r.ok)
        connection->setText(r.error);
    else
        connection->setText("● 服务已连接 · 数据由服务器确认");
}
void DesktopWindow::openUrl(const QString &path) {
    // 使用系统默认浏览器打开同一 API 服务下的 Web 页面。
    QDesktopServices::openUrl(api->baseUrl().resolved(QUrl(path)));
}
void DesktopWindow::screenshot(const QString &dir, const QStringList &pages, int delay) {
    // 自动验收模式按定时器逐页导航：先保存上一页，再切换下一页，最终退出应用。
    QDir().mkpath(dir);
    auto index = std::make_shared<int>(0);
    auto tick = new QTimer(this);
    tick->setInterval(delay);
    // tick 的 timeout 信号驱动状态机；定时器以窗口为父对象，无需手动 delete。
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
