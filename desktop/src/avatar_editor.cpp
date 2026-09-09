#include "avatar_editor.h"
#include "avatar_capture_dialog.h"
#include "face_image_processor.h"
#include <QBuffer>
#include <QButtonGroup>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QRadioButton>
#include <QSharedPointer>

QImage circularAvatarImage(const QImage &image, int size) {
    if (image.isNull() || size <= 0) return {};
    QImage avatar(size,size,QImage::Format_ARGB32_Premultiplied); avatar.fill(Qt::transparent);
    const double scale=qMax(double(size)/image.width(),double(size)/image.height());
    const QSizeF scaled(image.width()*scale,image.height()*scale);
    QPainter painter(&avatar); painter.setRenderHint(QPainter::Antialiasing); painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip; clip.addEllipse(QRectF(0,0,size,size)); painter.setClipPath(clip);
    painter.drawImage(QRectF(QPointF(size/2.,size/2.)-QPointF(scaled.width()/2.,scaled.height()/2.),scaled),image);
    return avatar;
}

AvatarCanvas::AvatarCanvas(QWidget *parent):QWidget(parent){setObjectName("avatar-canvas");setMinimumHeight(240);setCursor(Qt::OpenHandCursor);}
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
    QPointF pan(qBound(-(size.width()-256)/2,offset.x()*256,(size.width()-256)/2),qBound(-(size.height()-256)/2,offset.y()*256,(size.height()-256)/2));
    QPainter p(&out);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;clip.addEllipse(QRectF(0,0,256,256));p.setClipPath(clip);
    p.drawImage(QRectF(QPointF(128,128)-QPointF(size.width()/2,size.height()/2)+pan,size),working);p.end();
    if(brightness)for(int y=0;y<out.height();++y){auto line=reinterpret_cast<QRgb *>(out.scanLine(y));for(int x=0;x<out.width();++x){auto c=line[x];line[x]=qRgba(qBound(0,qRed(c)+brightness,255),qBound(0,qGreen(c)+brightness,255),qBound(0,qBlue(c)+brightness,255),qAlpha(c));}}
    return out;
}
void AvatarCanvas::paintEvent(QPaintEvent *){QPainter p(this);const int side=qMin(width(),height())-8;QRect area((width()-side)/2,(height()-side)/2,side,side);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(area,result());p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor("#eedcff"),2,Qt::DashLine));p.drawEllipse(area.adjusted(2,2,-2,-2));}
void AvatarCanvas::mousePressEvent(QMouseEvent *e){last=e->position();}
void AvatarCanvas::mouseMoveEvent(QMouseEvent *e){if(e->buttons()&Qt::LeftButton){offset+=(e->position()-last)/qMax(1,qMin(width(),height())-8);last=e->position();update();}}

namespace {
enum class AvatarMode { Normal, Face };
enum class FaceVersion { Original, Beautified };
struct EditorState { AvatarMode mode=AvatarMode::Normal; FaceVersion version=FaceVersion::Original; QImage source,cropped,beautified; };
}

static void populateAvatarEditor(QWidget *host,QVBoxLayout *layout,ApiClient *api,std::function<void(const QJsonObject &)> saved,std::function<void()> finished,const QImage &current){
    auto state=QSharedPointer<EditorState>::create();
    layout->addWidget(label("请选择头像类型","font-size:15px;font-weight:600;"));
    auto normal=new QRadioButton("普通头像（不进行人脸检测）",host);normal->setObjectName("avatar-mode-normal");
    auto face=new QRadioButton("人脸头像（智能检测与美化）",host);face->setObjectName("avatar-mode-face");
    auto group=new QButtonGroup(host);group->addButton(normal);group->addButton(face);normal->setChecked(true);
    layout->addWidget(normal);layout->addWidget(face);
    auto hint=label("普通模式允许上传任意图片，不会进行人脸检测。","color:#bca8ca;font-size:11px;");hint->setWordWrap(true);layout->addWidget(hint);
    auto detection=new QLabel(host);detection->setObjectName("avatar-face-detection-preview");detection->setAlignment(Qt::AlignCenter);detection->setFixedHeight(105);detection->setStyleSheet("background:#130d1c;border-radius:10px;");detection->hide();layout->addWidget(detection);
    auto canvas=new AvatarCanvas;layout->addWidget(canvas);
    layout->addWidget(label("拖动调整位置，圆形虚线为头像显示范围。","color:#bca8ca;font-size:11px;"));
    auto zoom=new QSlider(Qt::Horizontal);zoom->setObjectName("avatar-zoom");zoom->setRange(100,300);zoom->setValue(100);
    auto brightness=new QSlider(Qt::Horizontal);brightness->setObjectName("avatar-brightness");brightness->setRange(-60,60);
    auto reset=[=]{canvas->reset();zoom->setValue(100);brightness->setValue(0);};
    auto versions=new QWidget(host);auto vl=new QHBoxLayout(versions);vl->setContentsMargins(0,0,0,0);
    auto original=new QPushButton("原图",versions);original->setObjectName("avatar-face-original");
    auto beauty=new QPushButton("轻度美颜",versions);beauty->setObjectName("avatar-face-beautified");vl->addWidget(original);vl->addWidget(beauty);versions->hide();
    auto showVersion=[=](FaceVersion version){state->version=version;const auto image=version==FaceVersion::Beautified?state->beautified:state->cropped;if(!image.isNull())canvas->setImage(image);original->setProperty("primary",version==FaceVersion::Original);beauty->setProperty("primary",version==FaceVersion::Beautified);original->style()->unpolish(original);original->style()->polish(original);beauty->style()->unpolish(beauty);beauty->style()->polish(beauty);reset();};
    QObject::connect(original,&QPushButton::clicked,host,[=]{showVersion(FaceVersion::Original);});QObject::connect(beauty,&QPushButton::clicked,host,[=]{showVersion(FaceVersion::Beautified);});
    auto applyImage=[=](const QImage &image){
        if(image.isNull())return;state->source=image;
        if(state->mode==AvatarMode::Normal){state->cropped={};state->beautified={};detection->setToolTip({});canvas->setToolTip({});detection->hide();versions->hide();canvas->setImage(image);reset();return;}
        const auto result=FaceImageProcessor::process(image);
        if(!result.ok){QMessageBox box(QMessageBox::Warning,"人脸检测",result.error,QMessageBox::Retry,host);auto useNormal=box.addButton("使用普通头像模式",QMessageBox::AcceptRole);box.exec();if(box.clickedButton()==useNormal)normal->click();return;}
        state->cropped=result.croppedImage;state->beautified=result.beautifiedImage;
        detection->setPixmap(QPixmap::fromImage(result.detectionPreview).scaled(detection->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
        const QString faceTip=QString("识别到 %1 张人脸\n绿色框：面积最大的主脸\n黄色框：其他人脸\n检测框仅用于预览，不会上传").arg(result.faceCount);
        detection->setToolTip(faceTip);canvas->setToolTip(faceTip);detection->show();versions->show();showVersion(FaceVersion::Original);
    };
    QObject::connect(normal,&QRadioButton::toggled,host,[=](bool on){if(!on)return;state->mode=AvatarMode::Normal;hint->setText("普通模式允许上传任意图片，不会进行人脸检测。");if(!state->source.isNull())applyImage(state->source);});
    QObject::connect(face,&QRadioButton::toggled,host,[=](bool on){if(!on)return;state->mode=AvatarMode::Face;hint->setText("人脸模式将检测最大人脸、自动裁剪，并提供原图与轻度美颜预览。");if(!FaceImageProcessor::isAvailable()){QMessageBox::warning(host,"人脸头像","人脸检测组件加载失败，可继续使用普通头像模式。");normal->setChecked(true);return;}if(!state->source.isNull())applyImage(state->source);});
    layout->addWidget(label("示例图片 · 点击试用","font-size:12px;"));auto samples=new QHBoxLayout;
    for(const auto &name:{"ev-photo.png","ev-top-photo.png","charging-plasma.png"}){auto b=new QPushButton;b->setObjectName("avatar-sample-"+QString(name));b->setIcon(QIcon(":/assets/"+QString(name)));b->setIconSize(QSize(62,44));b->setStyleSheet("QPushButton{padding:6px;}");b->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);samples->addWidget(b);QObject::connect(b,&QPushButton::clicked,host,[=]{applyImage(QImage(":/assets/"+QString(name)));});}
    layout->addLayout(samples);applyImage(current.isNull()?QImage(":/assets/ev-photo.png"):current);
    auto sources=new QHBoxLayout;
    auto cameraButton=button("打开相机拍照",host,[]{},true);
    cameraButton->setObjectName("avatar-open-camera");
    cameraButton->setFocusPolicy(Qt::NoFocus);
    QObject::connect(cameraButton,&QPushButton::clicked,host,[=]{
        AvatarCaptureDialog capture(host);
        const int result=capture.exec();
        cameraButton->clearFocus();
        host->setFocus(Qt::OtherFocusReason);
        if(result==QDialog::Accepted)applyImage(capture.capturedImage());
    });
    sources->addWidget(cameraButton,1);
    sources->addWidget(button("选择本地图片",host,[=]{auto path=QFileDialog::getOpenFileName(host,"选择头像",{},"图片 (*.png *.jpg *.jpeg)");if(path.isEmpty())return;QImageReader reader(path);reader.setAutoTransform(true);const auto size=reader.size();if(!size.isValid()||qint64(size.width())*size.height()>40000000||QFileInfo(path).size()>15*1024*1024){QMessageBox::warning(host,"图片过大","请选择 15 MB、4000 万像素以内的 PNG 或 JPEG 图片。");return;}reader.setScaledSize(size.scaled(1600,1600,Qt::KeepAspectRatio));auto image=reader.read();if(image.isNull()){QMessageBox::warning(host,"读取失败","无法读取该图片，请更换文件。");return;}applyImage(image);}),1);layout->addLayout(sources);layout->addWidget(versions);
    auto transforms=row({button("旋转 90°",host,[=]{canvas->rotate();}),button("水平翻转",host,[=]{canvas->mirror();}),button("重置",host,reset)});for(auto b:transforms->findChildren<QPushButton *>()){b->setStyleSheet("QPushButton{padding:7px 4px;}");b->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);}layout->addWidget(transforms);
    layout->addWidget(label("缩放裁剪"));layout->addWidget(zoom);layout->addWidget(label("亮度"));layout->addWidget(brightness);
    QObject::connect(zoom,&QSlider::valueChanged,host,[=](int n){canvas->setZoom(n);});QObject::connect(brightness,&QSlider::valueChanged,host,[=](int n){canvas->setBrightness(n);});
    auto error=label("","color:#ef819d;");layout->addWidget(error);auto save=new QPushButton("保存头像");save->setObjectName("avatar-save");save->setProperty("primary",true);layout->addWidget(save);
    QObject::connect(save,&QPushButton::clicked,host,[=]{auto image=canvas->result();if(image.isNull()){error->setText("请先拍照或选择图片。");return;}QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");save->setEnabled(false);api->request("PUT","/me/avatar",{{"png_base64",QString::fromLatin1(bytes.toBase64())}},host,[=](const Reply &r){save->setEnabled(true);if(!r.ok){error->setText(r.error);return;}saved(r.data.object());finished();});});
}

void showAvatarEditor(QWidget *owner,ApiClient *api,std::function<void(const QJsonObject &)> saved,const QImage &current){auto d=new QDialog(owner);d->setAttribute(Qt::WA_DeleteOnClose);d->resize(440,860);d->setWindowTitle("编辑头像");auto l=new QVBoxLayout(d);l->addWidget(dialogHeader(d,label("编辑头像","font-size:22px;")));populateAvatarEditor(d,l,api,std::move(saved),[d]{d->accept();},current);d->show();}
QWidget *createAvatarEditorPage(QWidget *owner,ApiClient *api,std::function<void(const QJsonObject &)> saved,const QImage &current){auto page=new QWidget(owner);page->setObjectName("avatar-editor-page");auto layout=new QVBoxLayout(page);layout->setContentsMargins(0,0,0,0);populateAvatarEditor(page,layout,api,std::move(saved),[]{},current);return page;}
