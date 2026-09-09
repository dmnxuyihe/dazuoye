#include "avatar_editor.h"
#include <QImageReader>

QImage circularAvatarImage(const QImage &image, int size) {
    if (image.isNull() || size <= 0) return {};

    QImage avatar(size, size, QImage::Format_ARGB32_Premultiplied);
    avatar.fill(Qt::transparent);
    const double scale = qMax(double(size) / image.width(), double(size) / image.height());
    const QSizeF scaled(image.width() * scale, image.height() * scale);

    QPainter painter(&avatar);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addEllipse(QRectF(0, 0, size, size));
    painter.setClipPath(clip);
    painter.drawImage(QRectF(QPointF(size / 2.0, size / 2.0) -
                                   QPointF(scaled.width() / 2.0, scaled.height() / 2.0),
                               scaled),
                      image);
    return avatar;
}

AvatarCanvas::AvatarCanvas(QWidget *parent):QWidget(parent) {
    setObjectName("avatar-canvas");setMinimumHeight(240);setCursor(Qt::OpenHandCursor);
}
void AvatarCanvas::setImage(const QImage &image){original=image;reset();}
void AvatarCanvas::reset(){working=original;zoom=1.;brightness=0;offset={};update();}
void AvatarCanvas::rotate(){working=working.transformed(QTransform().rotate(90));offset={};update();}
void AvatarCanvas::mirror(){working=working.mirrored(true,false);offset.setX(-offset.x());update();}
void AvatarCanvas::setZoom(int percent){zoom=percent/100.;update();}
void AvatarCanvas::setBrightness(int value){brightness=value;update();}
QImage AvatarCanvas::result() const {
    if(working.isNull())return {};
    QImage out(256,256,QImage::Format_ARGB32_Premultiplied);out.fill(Qt::transparent);
    const double scale=qMax(256./working.width(),256./working.height())*zoom;
    QSizeF size(working.width()*scale,working.height()*scale);
    // Clamp panning to keep every output pixel covered by the source image.
    QPointF pan(qBound(-(size.width()-256)/2,offset.x()*256,(size.width()-256)/2),
                qBound(-(size.height()-256)/2,offset.y()*256,(size.height()-256)/2));
    QPainter p(&out);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;clip.addEllipse(QRectF(0,0,256,256));p.setClipPath(clip);
    p.drawImage(QRectF(QPointF(128,128)-QPointF(size.width()/2,size.height()/2)+pan,size),working);p.end();
    if(brightness)for(int y=0;y<out.height();++y){auto line=reinterpret_cast<QRgb *>(out.scanLine(y));for(int x=0;x<out.width();++x){auto c=line[x];line[x]=qRgba(qBound(0,qRed(c)+brightness,255),qBound(0,qGreen(c)+brightness,255),qBound(0,qBlue(c)+brightness,255),qAlpha(c));}}
    return out;
}
void AvatarCanvas::paintEvent(QPaintEvent *) {
    QPainter p(this);const int side=qMin(width(),height())-8;QRect area((width()-side)/2,(height()-side)/2,side,side);
    p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(area,result());
    p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor("#eedcff"),2,Qt::DashLine));p.drawEllipse(area.adjusted(2,2,-2,-2));
}
void AvatarCanvas::mousePressEvent(QMouseEvent *e){last=e->position();}
void AvatarCanvas::mouseMoveEvent(QMouseEvent *e){if(e->buttons()&Qt::LeftButton){offset+=(e->position()-last)/qMax(1,qMin(width(),height())-8);last=e->position();update();}}
static void populateAvatarEditor(QWidget *d, QVBoxLayout *l, ApiClient *api,
                                 std::function<void(const QJsonObject &)> saved,
                                 std::function<void()> finished, const QImage &current) {
    l->addWidget(label("拖动调整位置，圆形虚线为头像显示范围。","color:#bca8ca;font-size:11px;"));
    auto canvas=new AvatarCanvas;l->addWidget(canvas);
    auto zoom=new QSlider(Qt::Horizontal);zoom->setObjectName("avatar-zoom");zoom->setRange(100,300);zoom->setValue(100);
    auto brightness=new QSlider(Qt::Horizontal);brightness->setObjectName("avatar-brightness");brightness->setRange(-60,60);
    auto reset=[=]{canvas->reset();zoom->setValue(100);brightness->setValue(0);};
    auto choose=[=](const QImage &image){canvas->setImage(image);reset();};
    l->addWidget(label("示例图片 · 点击试用","font-size:12px;"));
    auto samples=new QHBoxLayout;
    for(const auto &name:{"ev-photo.png","ev-top-photo.png","charging-plasma.png"}) {
        auto b=new QPushButton;b->setObjectName("avatar-sample-"+QString(name));b->setIcon(QIcon(":/assets/"+QString(name)));b->setIconSize(QSize(62,44));b->setStyleSheet("QPushButton{padding:6px;}");b->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
        b->setToolTip("使用示例图片");samples->addWidget(b);
        QObject::connect(b,&QPushButton::clicked,d,[=]{choose(QImage(":/assets/"+QString(name)));});
    }
    l->addLayout(samples);choose(current.isNull()?QImage(":/assets/ev-photo.png"):current);
    l->addWidget(button("选择本地图片",d,[=]{
        auto path=QFileDialog::getOpenFileName(d,"选择头像",{},"图片 (*.png *.jpg *.jpeg)");if(path.isEmpty())return;
        QImageReader reader(path);reader.setAutoTransform(true);const auto size=reader.size();
        if(!size.isValid()||qint64(size.width())*size.height()>40000000||QFileInfo(path).size()>15*1024*1024){QMessageBox::warning(d,"图片过大","请选择 15 MB、4000 万像素以内的 PNG 或 JPEG 图片。");return;}
        reader.setScaledSize(size.scaled(1600,1600,Qt::KeepAspectRatio));auto image=reader.read();
        if(image.isNull()){QMessageBox::warning(d,"读取失败","无法读取该图片，请更换文件。");return;}choose(image);
    }));
    auto transforms=row({button("旋转 90°",d,[=]{canvas->rotate();}),button("水平翻转",d,[=]{canvas->mirror();}),button("重置",d,reset)});
    for(auto b:transforms->findChildren<QPushButton *>()){b->setStyleSheet("QPushButton{padding:7px 4px;}");b->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);}
    l->addWidget(transforms);
    l->addWidget(label("缩放裁剪"));l->addWidget(zoom);l->addWidget(label("亮度"));l->addWidget(brightness);
    QObject::connect(zoom,&QSlider::valueChanged,d,[=](int n){canvas->setZoom(n);});
    QObject::connect(brightness,&QSlider::valueChanged,d,[=](int n){canvas->setBrightness(n);});
    auto error=label("","color:#ef819d;");l->addWidget(error);
    auto save=new QPushButton("保存头像");save->setObjectName("avatar-save");save->setProperty("primary",true);l->addWidget(save);
    QObject::connect(save,&QPushButton::clicked,d,[=]{
        QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);canvas->result().save(&buffer,"PNG");save->setEnabled(false);
        api->request("PUT","/me/avatar",{{"png_base64",QString::fromLatin1(bytes.toBase64())}},d,[=](const Reply &r){
            save->setEnabled(true);if(!r.ok){error->setText(r.error);return;}saved(r.data.object());finished();
        });
    });
}
void showAvatarEditor(QWidget *owner, ApiClient *api, std::function<void(const QJsonObject &)> saved, const QImage &current) {
    auto d=new QDialog(owner);d->setAttribute(Qt::WA_DeleteOnClose);d->resize(440,760);d->setWindowTitle("编辑头像");
    auto l=new QVBoxLayout(d);l->addWidget(dialogHeader(d,label("编辑头像","font-size:22px;")));
    populateAvatarEditor(d,l,api,std::move(saved),[d]{d->accept();},current);
    d->show();
}
QWidget *createAvatarEditorPage(QWidget *owner, ApiClient *api,
                                std::function<void(const QJsonObject &)> saved,
                                const QImage &current) {
    auto page=new QWidget(owner);page->setObjectName("avatar-editor-page");
    auto layout=new QVBoxLayout(page);layout->setContentsMargins(0,0,0,0);
    populateAvatarEditor(page,layout,api,std::move(saved),[]{},current);
    return page;
}
