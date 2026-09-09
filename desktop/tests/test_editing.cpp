#include "avatar_editor.h"
#include "charger_picker.h"
#include <QtTest>
class EditingTest:public QObject {
    Q_OBJECT
  private slots:
    void imageOperations() {
        AvatarCanvas canvas;QImage source(256,256,QImage::Format_RGB32);source.fill(Qt::red);
        QPainter painter(&source);painter.fillRect(128,0,128,256,Qt::blue);painter.end();
        canvas.setImage(source);QCOMPARE(canvas.result().size(),QSize(256,256));
        QCOMPARE(canvas.result().pixelColor(64,128),QColor(Qt::red));
        canvas.mirror();QCOMPARE(canvas.result().pixelColor(64,128),QColor(Qt::blue));
        canvas.rotate();QVERIFY(canvas.result()!=source);
        canvas.reset();QCOMPARE(canvas.result().pixelColor(64,128),QColor(Qt::red));
        QCOMPARE(canvas.result().pixelColor(0,0).alpha(),0);
        canvas.setBrightness(-30);QCOMPARE(canvas.result().pixelColor(64,128).red(),225);
        canvas.reset();canvas.setZoom(200);QCOMPARE(canvas.result().size(),QSize(256,256));
        canvas.resize(280,260);canvas.show();QTest::mousePress(&canvas,Qt::LeftButton,{},QPoint(120,120));
        QMouseEvent move(QEvent::MouseMove,QPointF(210,120),QPointF(210,120),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&canvas,&move);QVERIFY(canvas.result()!=source);
    }
    void avatarAlwaysCircular_data() {
        QTest::addColumn<QSize>("sourceSize");
        QTest::newRow("square") << QSize(256,256);
        QTest::newRow("wide") << QSize(640,180);
        QTest::newRow("tall") << QSize(180,640);
        QTest::newRow("small") << QSize(24,36);
    }
    void avatarAlwaysCircular() {
        QFETCH(QSize,sourceSize);
        QImage source(sourceSize,QImage::Format_RGB32);source.fill(QColor("#38a169"));
        AvatarCanvas canvas;canvas.setImage(source);
        for (int zoom : {100,175,300}) {
            canvas.setZoom(zoom);
            for (int brightness : {-40,0,40}) {
                canvas.setBrightness(brightness);
                const QImage result=canvas.result();
                QCOMPARE(result.size(),QSize(256,256));
                QVERIFY(result.hasAlphaChannel());
                QCOMPARE(result.pixelColor(0,0).alpha(),0);
                QCOMPARE(result.pixelColor(255,0).alpha(),0);
                QCOMPARE(result.pixelColor(0,255).alpha(),0);
                QCOMPARE(result.pixelColor(255,255).alpha(),0);
                QCOMPARE(result.pixelColor(128,128).alpha(),255);
                QVERIFY(result.pixelColor(128,128).green()>0);
            }
        }
    }
    void circularDisplayCropsLegacyImages() {
        QImage wide(600,200,QImage::Format_RGB32);wide.fill(Qt::yellow);
        const auto avatar=circularAvatarImage(wide,56);
        QCOMPARE(avatar.size(),QSize(56,56));
        QCOMPARE(avatar.pixelColor(0,0).alpha(),0);
        QCOMPARE(avatar.pixelColor(55,55).alpha(),0);
        QCOMPARE(avatar.pixelColor(28,28),QColor(Qt::yellow));
    }
    void liveChargerSelection() {
        ChargerPicker picker;picker.resize(320,500);picker.show();
        QJsonArray data;
        for(auto state:{"available","charging","faulted","reserved"})data.append(QJsonObject{{"id",state},{"code","UEV-1268-02"},{"kind","fast"},{"power_kw",120},{"status",state},{"total_sessions",17}});
        picker.setChargers(data,false,false);QCOMPARE(picker.selectedId(),QString("available"));
        QVERIFY(!picker.findChild<QPushButton *>("charger-charging")->isEnabled());
        QVERIFY(!picker.findChild<QPushButton *>("charger-faulted")->isEnabled());
        auto first=data[0].toObject();first["status"]="charging";data[0]=first;
        picker.setChargers(data,false,false);QVERIFY(picker.selectedId().isEmpty());
        first["status"]="available";data[0]=first;
        picker.setChargers(data,true,false);QVERIFY(picker.selectedId().isEmpty());
        picker.setChargers(data,false,false);QVERIFY(picker.selectedId().isEmpty());
        QTest::mouseClick(picker.findChild<QPushButton *>("charger-available"),Qt::LeftButton);
        QCOMPARE(picker.selectedId(),QString("available"));
        QTest::qWait(100);
        for(auto b:picker.findChildren<QPushButton *>()) {
            QVERIFY(picker.rect().contains(b->geometry()));QVERIFY(b->height()>=140);
            for(auto text:b->findChildren<QLabel *>())QVERIFY(text->height()>=text->fontMetrics().height());
        }
        QDir().mkpath(".runtime/qt-cards-avatar/screenshots");picker.grab().save(".runtime/qt-cards-avatar/screenshots/charger-cards.png");
    }
    void chargerFilters() {
        ChargerPicker picker;
        QJsonArray data{
            QJsonObject{{"id","fast-free"},{"code","F-01"},{"kind","fast"},{"status","available"}},
            QJsonObject{{"id","slow-free"},{"code","S-01"},{"kind","slow"},{"status","available"}},
            QJsonObject{{"id","fast-busy"},{"code","F-02"},{"kind","fast"},{"status","charging"}}
        };
        picker.setChargers(data, false, false, "available", "slow");
        QVERIFY(picker.findChild<QPushButton *>("charger-slow-free"));
        QVERIFY(!picker.findChild<QPushButton *>("charger-fast-free"));
        QVERIFY(!picker.findChild<QPushButton *>("charger-fast-busy"));
        QCOMPARE(picker.selectedId(), QString("slow-free"));
    }
    void compactAvatarDialog() {
        QTemporaryDir temp;CacheStore cache(temp.filePath("cache.sqlite"));ApiClient api(QUrl("http://127.0.0.1:1"),&cache);
        QWidget owner;owner.resize(360,700);owner.show();showAvatarEditor(&owner,&api,[](const QJsonObject &){});
        auto d=owner.findChild<QDialog *>();QVERIFY(d);QTest::qWait(150);
        auto save=d->findChild<QPushButton *>("avatar-save");QVERIFY(save);
        QVERIFY(d->rect().contains(QRect(save->mapTo(d,QPoint()),save->size())));
        auto canvas=d->findChild<QWidget *>("avatar-canvas");QVERIFY(canvas);
        auto sample=d->findChild<QPushButton *>("avatar-sample-ev-top-photo.png");QVERIFY(sample);QTest::mouseClick(sample,Qt::LeftButton);
        QDir().mkpath(".runtime/qt-cards-avatar/screenshots");d->grab().save(".runtime/qt-cards-avatar/screenshots/avatar-editor.png");d->close();
    }
};
int main(int argc,char **argv){QApplication app(argc,argv);applyTheme(app);EditingTest test;return QTest::qExec(&test,argc,argv);}
#include "test_editing.moc"
