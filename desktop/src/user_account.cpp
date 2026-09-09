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
void UserWindow::profile() {
    body->addWidget(pageHeading("个人中心", this, [this] { navigate("home"); }));
    QVBoxLayout *l;
    auto account = card("", &l);
    account->setObjectName("profile-account");
    account->setStyleSheet("QFrame#profile-account{background:transparent;border:0;border-radius:0;}");
    auto header = new QHBoxLayout;
    header->setSpacing(16);
    auto avatar = button(api->authenticated() ? text(me, "nickname", "E").left(1) : "E", this, [this] {
        if (!api->authenticated()) { login(); return; }
        navigate("avatar");
    });
    avatar->setObjectName("profile-avatar");
    avatar->setStyleSheet("QPushButton#profile-avatar{background:transparent;border:0;padding:0;}");
    avatar->setFixedSize(56, 56);
    avatar->setMask(QRegion(avatar->rect(), QRegion::Ellipse));
    auto avatarData = QByteArray::fromBase64(text(me,"avatar_data","").toUtf8());
    QImage roundImage;
    if (!avatarData.isEmpty()) {
        QImage image;
        image.loadFromData(avatarData, "PNG");
        roundImage = circularAvatarImage(image, 56);
    }
    if (roundImage.isNull()) {
        roundImage = QImage(56,56,QImage::Format_ARGB32_Premultiplied);
        roundImage.fill(Qt::transparent);
        QPainter painter(&roundImage);painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);painter.setBrush(QColor("#9455b4"));painter.drawEllipse(QRectF(0,0,56,56));
        QFont font=avatar->font();font.setPixelSize(23);font.setWeight(QFont::DemiBold);
        painter.setFont(font);painter.setPen(Qt::white);painter.drawText(roundImage.rect(),Qt::AlignCenter,avatar->text());
    }
    avatar->setText("");
    avatar->setIcon(QIcon(QPixmap::fromImage(roundImage)));
    avatar->setIconSize(QSize(56,56));
    header->addWidget(avatar);
    header->addWidget(label(api->authenticated() ? text(me, "nickname", "充电用户") : "欢迎来到 ELECTRA",
                            "font-size:18px;font-weight:600;"), 1);
    auto edit = button("编辑", this, [this] {
        if (!api->authenticated()) { login(); return; }
        editForm(this, api, "个人资料", "PATCH", "/me", {{"nickname", "昵称"}},
                 {{"nickname", me.value("nickname")}}, [this] { refresh(); });
    });
    edit->setIcon(appIcon("settings"));
    edit->setIconSize(QSize(18,18));
    edit->setFixedWidth(82);
    header->addWidget(edit, 0, Qt::AlignRight | Qt::AlignVCenter);
    l->addLayout(header);
    if (!api->authenticated()) {
        l->addSpacing(12);
        l->addWidget(picture("ev-photo.png", 160));
        l->addWidget(button("手机号验证码登录", this, [this] { login(); }, true));
        body->addWidget(account);
        body->addWidget(
            emptyPanel("开启你的充电旅程", "登录后预约充电、查看账单与管理钱包。", "car"));
        body->addWidget(button("刷新网络连接", this, [this] { refresh(); }));
        return;
    }
    body->addWidget(account);

    auto balances = new QWidget;
    auto balanceRow = new QHBoxLayout(balances);
    balanceRow->setContentsMargins(0,4,0,4);
    balanceRow->setSpacing(0);
    auto metric = [](const QString &amount, const QString &caption) {
        auto w = new QWidget;
        auto box = new QVBoxLayout(w);
        box->setContentsMargins(0,0,0,0);
        box->setSpacing(5);
        auto value = label(amount, "font-size:22px;font-weight:600;");
        auto name = label(caption, "font-size:11px;color:#b3a0c2;");
        value->setAlignment(Qt::AlignCenter); name->setAlignment(Qt::AlignCenter);
        box->addWidget(value); box->addWidget(name);
        return w;
    };
    balanceRow->addWidget(metric("¥"+money(number(me,"balance")), "总金额"), 1);
    balanceRow->addWidget(metric("¥"+money(me.contains("available_balance") ? number(me,"available_balance") : number(me,"balance")), "可用余额"), 1);
    balanceRow->addWidget(metric("¥"+money(number(me,"held_balance")), "待提现"), 1);
    body->addWidget(balances);

    QVBoxLayout *shortcutLayout;
    auto shortcuts = card("", &shortcutLayout);
    auto shortcutRow = new QHBoxLayout;
    shortcutRow->setContentsMargins(0,0,0,0);
    shortcutRow->setSpacing(10);
    auto shortcut = [this,shortcutRow](const QString &name, const QString &icon, std::function<void()> action) {
        auto b = new NavButton(name, icon, true);
        b->setCheckable(false);
        b->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        connect(b,&QPushButton::clicked,this,std::move(action));
        shortcutRow->addWidget(b,1);
    };
    shortcut("钱包", "chart", [this] { navigate("wallet"); });
    shortcut("我的车辆", "car", [this] {
        editForm(this,api,"绑定车辆（账户同步）","PUT","/me/vehicle",
            {{"vehicle_name","车型"},{"vehicle_plate","车牌"},{"battery_kwh","电池容量 kWh"},{"vehicle_soc","当前电量 %"},{"charge_limit","默认充电上限 %"}},
            {{"vehicle_name",me.value("vehicle_name")},{"vehicle_plate",me.value("vehicle_plate")},{"battery_kwh",me.contains("battery_kwh")?me.value("battery_kwh"):QJsonValue(60)},
             {"vehicle_soc",batterySoc()},{"charge_limit",me.contains("charge_limit")?me.value("charge_limit"):QJsonValue(80)}},
            [this]{active={};refresh();});
    });
    shortcut("提现", "arrow", [this] { navigate("withdrawals"); });
    shortcutLayout->addLayout(shortcutRow);
    body->addWidget(shortcuts);

    QVBoxLayout *historyLayout;
    auto historyCard = card("", &historyLayout);
    auto historyButton = button("订单历史     ›", this, [this] { navigate("history"); });
    historyButton->setIcon(appIcon("history"));
    historyButton->setIconSize(QSize(20,20));
    historyButton->setStyleSheet("QPushButton{text-align:left;padding:17px 8px;background:transparent;border:0;}QPushButton:hover{background:#352041;}");
    historyLayout->addWidget(historyButton);
    body->addWidget(historyCard);
    auto logout = button("退出登录", this, [this] {
        api->logout();
        if (refreshContext) delete refreshContext;
        events.stop();
        me = {};
        active = {};
        orders = {};
        ledger = {};
        withdrawals = {};
        navigate("profile");
    });
    logout->setProperty("quiet", true);
    logout->setStyleSheet("QPushButton{background:#1a1322;border:1px solid #2f2339;color:#9f8daa;}QPushButton:hover{background:#24192e;border-color:#493455;color:#c4b1cf;}");
    body->addWidget(logout);
}
void UserWindow::avatarPage() {
    body->addWidget(pageHeading("编辑头像",this,[this]{navigate("profile");}));
    if (!api->authenticated()) {
        body->addWidget(button("请先登录",this,[this]{login();},true));
        return;
    }
    QImage currentAvatar;
    currentAvatar.loadFromData(QByteArray::fromBase64(text(me,"avatar_data").toLatin1()),"PNG");
    body->addWidget(createAvatarEditorPage(this,api,[this](const QJsonObject &account){
        me=account;
        navigate("profile");
    },currentAvatar),1);
}
void UserWindow::wallet() {
    body->addWidget(pageHeading("我的钱包", this, [this] { navigate("profile"); }));
    if (!api->authenticated()) {
        body->addWidget(button("请先登录", this, [this] { login(); }, true));
        return;
    }
    auto balances = new QWidget;
    auto balanceRow = new QHBoxLayout(balances);
    balanceRow->setContentsMargins(0,6,0,8); balanceRow->setSpacing(0);
    auto metric = [](const QString &amount, const QString &caption) {
        auto w = new QWidget; auto box = new QVBoxLayout(w);
        box->setContentsMargins(0,0,0,0); box->setSpacing(5);
        auto value=label(amount,"font-size:22px;font-weight:600;");
        auto name=label(caption,"font-size:11px;color:#b3a0c2;");
        value->setAlignment(Qt::AlignCenter); name->setAlignment(Qt::AlignCenter);
        box->addWidget(value); box->addWidget(name); return w;
    };
    balanceRow->addWidget(metric("¥"+money(number(me,"balance")),"总金额"),1);
    balanceRow->addWidget(metric("¥"+money(me.contains("available_balance")?number(me,"available_balance"):number(me,"balance")),"可用余额"),1);
    balanceRow->addWidget(metric("¥"+money(number(me,"held_balance")),"待提现"),1);
    body->addWidget(balances);

    auto actions = new QHBoxLayout;
    actions->setSpacing(12);
    actions->addWidget(button("充值",this,[this]{
        editForm(this,api,"钱包充值","POST","/wallet/recharges",{{"amount","充值金额（元）"}},{{"idempotency_key",uid()}},[this]{refresh();});
    },true),1);
    actions->addWidget(button("提现",this,[this]{
        editForm(this,api,"申请提现（审核后模拟到账）","POST","/wallet/withdrawals",
            {{"amount","提现金额（元）"},{"destination","演示收款账户"}},{{"idempotency_key",uid()},{"destination","演示钱包"}},[this]{refresh();});
    }),1);
    body->addLayout(actions);

    QVBoxLayout *recordsLayout;
    auto records = card("",&recordsLayout);
    recordsLayout->setSpacing(10);
    auto tabs = new Segments({"资金记录","消费记录"},walletFilter);
    connect(tabs,&Segments::changed,this,[this](int n){walletFilter=n;navigate("wallet");});
    recordsLayout->addWidget(tabs);
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumHeight(360);
    auto content = new QWidget;
    auto rows = new QVBoxLayout(content);
    rows->setContentsMargins(0,0,0,0); rows->setSpacing(8);
    int shown=0;
    for (auto value : ledger) {
        auto entry=value.toObject();
        const auto type=text(entry,"entry_type");
        if ((walletFilter==1)!=(type=="charge")) continue;
        ++shown;
        auto item=new QFrame;
        item->setObjectName("walletRecord");
        item->setStyleSheet("QFrame#walletRecord{background:#21152c;border:1px solid #3e2949;border-radius:12px;}");
        auto line=new QHBoxLayout(item); line->setContentsMargins(13,10,13,10);
        auto info=new QVBoxLayout; info->setSpacing(3);
        info->addWidget(label(statusText(type),"font-size:12px;font-weight:600;"));
        info->addWidget(label(localDateTime(text(entry,"created_at")),"font-size:10px;color:#957ba8;"));
        line->addLayout(info,1);
        const double amount=number(entry,"amount");
        auto amountLabel=label(QString(amount>=0?"+¥%1":"-¥%1").arg(money(std::abs(amount))),
            QString("font-size:15px;font-weight:600;color:%1;").arg(amount>=0?"#7ad8c8":"#d983e1"));
        amountLabel->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
        line->addWidget(amountLabel);
        rows->addWidget(item);
    }
    if (!shown) rows->addWidget(emptyPanel(walletFilter?"暂无消费记录":"暂无资金记录","充值、提现或结算后会在此记录。","chart"));
    rows->addStretch();
    scroll->setWidget(content);
    recordsLayout->addWidget(scroll,1);
    body->addWidget(records,1);
}
void UserWindow::withdrawalsPage() {
    body->addWidget(pageHeading("提现审核", this, [this] { navigate("profile"); }));
    if (!api->authenticated()) {
        body->addWidget(button("请先登录", this, [this] { login(); }, true));
        return;
    }

    double pendingAmount = 0;
    int pendingCount = 0, approvedCount = 0, rejectedCount = 0;
    for (const auto &value : withdrawals) {
        const auto record = value.toObject();
        const auto status = text(record,"status");
        if (status == "pending") { pendingAmount += number(record,"amount"); ++pendingCount; }
        else if (status == "paid" || status == "approved") ++approvedCount;
        else if (status == "rejected") ++rejectedCount;
    }

    QVBoxLayout *summaryLayout;
    auto summary = card("",&summaryLayout);
    summary->setStyleSheet(
        "QFrame#card{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #422158,stop:.5 "
        "#2b183d,stop:1 #1a112a);border:1px solid #684073;border-radius:20px;}");
    auto pendingLabel=label("¥ "+money(pendingAmount),"font-size:34px;font-weight:600;");
    pendingLabel->setAlignment(Qt::AlignCenter);
    auto caption=label("提现待审核金额","font-size:12px;color:#c1a7d0;");
    caption->setAlignment(Qt::AlignCenter);
    summaryLayout->addWidget(pendingLabel);
    summaryLayout->addWidget(caption);
    body->addWidget(summary);

    auto counts = new QWidget;
    auto countRow = new QHBoxLayout(counts);
    countRow->setContentsMargins(0,0,0,0); countRow->setSpacing(0);
    auto countBlock=[](int count,const QString &name) {
        auto w=new QWidget; auto l=new QVBoxLayout(w); l->setContentsMargins(0,0,0,0); l->setSpacing(4);
        auto value=label(QString::number(count),"font-size:19px;font-weight:600;");
        auto caption=label(name,"font-size:10px;color:#b3a0c2;");
        value->setAlignment(Qt::AlignCenter); caption->setAlignment(Qt::AlignCenter);
        l->addWidget(value); l->addWidget(caption); return w;
    };
    countRow->addWidget(countBlock(pendingCount,"待审核"),1);
    countRow->addWidget(countBlock(approvedCount,"已通过"),1);
    countRow->addWidget(countBlock(rejectedCount,"已驳回"),1);
    body->addWidget(counts);
    body->addWidget(button("申请提现",this,[this]{
        editForm(this,api,"申请提现（审核后模拟到账）","POST","/wallet/withdrawals",
            {{"amount","提现金额（元）"},{"destination","演示收款账户"}},
            {{"idempotency_key",uid()},{"destination","演示钱包"}},[this]{refresh();});
    },true));

    QVBoxLayout *recordsLayout;
    auto records=card("审核记录",&recordsLayout);
    auto scroll=new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumHeight(300);
    auto contentWidget=new QWidget;
    auto rows=new QVBoxLayout(contentWidget);
    rows->setContentsMargins(0,0,0,0); rows->setSpacing(8);
    for (const auto &value : withdrawals) {
        const auto record=value.toObject();
        const auto status=text(record,"status");
        const auto statusName=status=="pending"?"待审核":
            ((status=="paid"||status=="approved")?"已通过":
             (status=="rejected"?"已驳回":statusText(status)));
        auto item=new QFrame;
        item->setObjectName("withdrawalRecord");
        item->setStyleSheet("QFrame#withdrawalRecord{background:#21152c;border:1px solid #3e2949;border-radius:12px;}");
        auto line=new QHBoxLayout(item); line->setContentsMargins(13,10,13,10);
        auto info=new QVBoxLayout; info->setSpacing(3);
        info->addWidget(label("提现申请 · "+statusName,"font-size:12px;font-weight:600;"));
        info->addWidget(label(localDateTime(text(record,"requested_at")),"font-size:10px;color:#957ba8;"));
        const auto note=text(record,"review_note");
        if (!note.isEmpty()) info->addWidget(label(note,"font-size:10px;color:#b3a0c2;"));
        line->addLayout(info,1);
        auto amount=label("¥"+money(number(record,"amount")),"font-size:15px;font-weight:600;color:#d983e1;");
        amount->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
        line->addWidget(amount);
        rows->addWidget(item);
    }
    if (withdrawals.isEmpty()) rows->addWidget(emptyPanel("暂无提现记录","申请提现后，审核状态会显示在这里。","chart"));
    rows->addStretch();
    scroll->setWidget(contentWidget);
    recordsLayout->addWidget(scroll,1);
    body->addWidget(records,1);
}
void UserWindow::history() {
    body->addWidget(pageHeading("我的充电订单", this, [this] { navigate("profile"); }));
    if (!api->authenticated()) {
        body->addWidget(button("请先登录", this, [this] { login(); }, true));
        return;
    }
    auto filters = new Segments({"全部订单", "进行中", "已完成", "待支付"}, historyFilter);
    connect(filters, &Segments::changed, this, [this](int n) {
        historyFilter = n;
        navigate("history");
    });
    body->addWidget(filters);
    if (orders.isEmpty())
        body->addWidget(
            emptyPanel("还没有充电旅程", "完成首次充电后，这里会显示电量、费用和订单状态", "car"));
    for (const auto &v : orders) {
        auto o = v.toObject();
        if (historyFilter == 1 && text(o, "status") != "reserved" &&
            text(o, "status") != "charging")
            continue;
        if (historyFilter == 3 && text(o,"status") != "pending_payment") continue;
        if (historyFilter == 2 && text(o, "status") != "completed")
            continue;
        QVBoxLayout *l;
        auto box = card(text(o, "station_name"), &l);
        l->addWidget(label(statusText(text(o, "status")) + "   ·   " + text(o, "charger_code"),
                           "color:#d195df;"));
        l->addWidget(
            label(money(number(o, "energy_kwh")) + " kWh     ¥" + money(number(o, "amount")),
                  "font-size:20px;"));
        l->addWidget(label(localDateTime(text(o, "reserved_at")), muted));
        l->addWidget(button("查看订单", this, [=] {
            api->get("/orders/" + text(o, "id"), this, [this](const Reply &r) {
                if (r.ok) {
                    active = r.data.object();
                    navigate("charging");
                } else
                    message(r);
            });
        }));
        body->addWidget(box);
    }
    body->addWidget(label("最近200条订单", muted));
}
void UserWindow::login() {
    auto d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle("手机号登录");
    auto l = new QVBoxLayout(d);
    l->addWidget(dialogHeader(d, label("欢迎回来", "font-size:25px;")));
    auto phone = new QLineEdit;
    phone->setObjectName("phone");
    phone->setPlaceholderText("11位手机号");
    auto code = new QLineEdit;
    code->setObjectName("otp");
    code->setPlaceholderText("6位验证码");
    l->addWidget(phone);
    l->addWidget(code);
    auto err = label("验证码由服务器发送；开发环境可能返回测试验证码。", muted);
    l->addWidget(err);
    auto send = button("获取验证码", d, [=] {
        api->request("POST", "/auth/otp/request", {{"phone", phone->text()}}, d,
                     [=](const Reply &r) {
                         if (!r.ok) {
                             err->setText(r.error);
                             return;
                         }
                         auto dev = text(r.data.object(), "development_code", "");
                         err->setText(dev.isEmpty() ? "验证码已发送" : "开发验证码：" + dev);
                     });
    });
    l->addWidget(send);
    l->addWidget(button(
        "登录", d,
        [=] {
            api->request("POST", "/auth/otp/verify",
                         {{"phone", phone->text()}, {"code", code->text()}}, d,
                         [=](const Reply &r) {
                             if (!r.ok) {
                                 err->setText(r.error);
                                 return;
                             }
                             api->session(text(r.data.object(), "access_token"));
                             d->accept();
                             refresh();
                         });
        },
        true));
    d->resize(400, 330);
    d->show();
}

