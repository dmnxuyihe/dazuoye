#include "charger_picker.h"

ChargerPicker::ChargerPicker(QWidget *parent) : QWidget(parent), grid(new QGridLayout(this)) {
    setObjectName("charger-picker");
    grid->setContentsMargins(0,0,0,0);grid->setSpacing(10);grid->setAlignment(Qt::AlignTop);
}
void ChargerPicker::setChargers(const QJsonArray &items, bool readOnly, bool fastOnly,
                                const QString &statusFilter, const QString &kindFilter) {
    for(auto b:buttons)delete b;
    buttons.clear();
    QString first, slow;
    for(auto value:items) {
        auto o=value.toObject();const auto state=text(o,"status");
        if (!statusFilter.isEmpty() && state != statusFilter) continue;
        if (!kindFilter.isEmpty() && text(o,"kind") != kindFilter) continue;
        const bool available=state=="available" && !readOnly && (!fastOnly || text(o,"kind")=="fast");
        const auto id=text(o,"id");
        if(available) {if(first.isEmpty())first=id;if(slow.isEmpty()&&text(o,"kind")=="slow")slow=id;}
        const QString color=state=="available"?"#65dcb0":state=="charging"?"#ffb45b":state=="reserved"?"#9cafff":"#ef819d";
        auto b=new QPushButton(this);b->setObjectName("charger-"+id);b->setProperty("chargerId",id);
        b->setProperty("stateColor",color);b->setProperty("available",available);
        b->setCheckable(true);b->setEnabled(available);b->setMinimumHeight(142);b->setMinimumWidth(0);
        b->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
        auto l=new QVBoxLayout(b);l->setContentsMargins(9,10,9,10);l->setSpacing(5);
        auto title=label("ϟ  "+statusText(state),"font-size:14px;font-weight:600;color:"+color+";");
        auto code=label(text(o,"code"),"font-size:11px;font-weight:600;");code->setWordWrap(true);
        QString kind=text(o,"kind")=="fast"?"快充":"慢充";
        auto info=label(kind+" · "+QString::number(number(o,"power_kw"))+" kW","font-size:11px;");
        auto count=label("累计充电 "+(o.contains("total_sessions")?QString::number(o["total_sessions"].toInt()):"—")+" 次","font-size:10px;color:#c6b5d1;");
        for(auto w:{title,code,info,count}){w->setAttribute(Qt::WA_TransparentForMouseEvents);l->addWidget(w);}
        b->setAccessibleName(text(o,"code")+" "+statusText(state));
        connect(b,&QPushButton::clicked,this,[this,id]{selected=id;updateSelection();if(selectionChanged)selectionChanged();});
        buttons.append(b);
    }
    bool valid=false;for(auto b:buttons)if(b->property("chargerId").toString()==selected&&b->property("available").toBool())valid=true;
    if(!valid)selected=initialized?QString():(!fastOnly&&!slow.isEmpty()?slow:first);
    initialized=true;
    arrange();updateSelection();if(selectionChanged)selectionChanged();
}
void ChargerPicker::updateSelection() {
    for(auto b:buttons) {
        const bool checked=b->property("chargerId").toString()==selected;
        b->setChecked(checked);
        b->setStyleSheet(QString("QPushButton{background:%1;border:%2px solid %3;border-radius:12px;text-align:left;min-height:140px;padding:0;} QPushButton:hover{background:#392443;}")
          .arg(checked?"#362043":"#1c1428").arg(checked?3:1).arg(b->property("stateColor").toString()));
    }
}
void ChargerPicker::arrange() {
    const int columns=width()>=540?3:2;
    for(int i=0;i<buttons.size();++i){grid->removeWidget(buttons[i]);grid->addWidget(buttons[i],i/columns,i%columns);}
    for(int i=0;i<3;++i)grid->setColumnStretch(i,i<columns?1:0);
}
void ChargerPicker::resizeEvent(QResizeEvent *e) {QWidget::resizeEvent(e);arrange();}
