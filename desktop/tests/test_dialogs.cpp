#include "ui.h"
#include <QtTest>

class DialogTest : public QObject {
    Q_OBJECT
    QWidget owner;
    QRect bounds() const {
        return QRect(owner.mapToGlobal(QPoint()),owner.size()).intersected(owner.screen()->availableGeometry());
    }
    void capture(QDialog *dialog, const QString &name) {
        const auto dir=qEnvironmentVariable("ELECTRA_DIALOG_SCREENSHOTS", ".runtime/qt-dialog-fit-20260909/screenshots");
        QDir().mkpath(dir);
        dialog->grab().save(dir+"/"+name+".png");
    }
  private slots:
    void init() { owner.resize(460,700); owner.move(0,0); owner.show(); }
    void cleanup() {
        for(auto dialog:owner.findChildren<QDialog *>())dialog->close();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        owner.hide();
    }
    void recordDetails() {
        showDetail(&owner,"提现申请与审核记录",{{"提现记录",QJsonArray{QJsonObject{
            {"id","a8d0f674-9634-46c4-983c-987654321012"},{"amount",8},
            {"destination","演示钱包"},{"status","pending"},{"requested_at","2026-09-09T03:30:00Z"}}}}});
        auto d=owner.findChild<QDialog *>();QVERIFY(d);
        QTest::qWait(100);capture(d,"records");
        QTRY_VERIFY_WITH_TIMEOUT(bounds().contains(d->frameGeometry()),2000);
        auto close=d->findChild<QPushButton *>("dialog-close");QVERIFY(close);
        QVERIFY(d->rect().contains(QRect(close->mapTo(d,QPoint()),close->size())));
        QTest::mouseClick(close,Qt::LeftButton);QVERIFY(!d->isVisible());
    }
    void filePicker() {
        QFileDialog d(&owner,"选择头像",QDir::currentPath(),"图片 (*.png *.jpg *.jpeg)");
        d.setOption(QFileDialog::DontUseNativeDialog);d.show();
        QTest::qWait(100);capture(&d,"file-picker");
        QTRY_VERIFY_WITH_TIMEOUT(bounds().contains(d.frameGeometry()),2000);
        auto box=d.findChild<QDialogButtonBox *>();QVERIFY(box);
        for(auto b:box->buttons())if(b->isVisible())
            QVERIFY(d.rect().contains(QRect(b->mapTo(&d,QPoint()),b->size())));
        QVERIFY(d.palette().color(QPalette::Base).lightnessF()<.3);
    }
    void longForm() {
        owner.resize(360,640);
        QTemporaryDir temp;CacheStore cache(temp.filePath("test.sqlite"));ApiClient api(QUrl("http://127.0.0.1:1"),&cache);
        QList<QPair<QString,QString>> fields;
        for(int i=0;i<5;++i)fields.append({QString::number(i),"车辆资料字段"+QString::number(i)});
        editForm(&owner,&api,"车辆资料", "PUT","/me/vehicle",fields,{},[]{});
        auto d=owner.findChild<QDialog *>();QVERIFY(d);
        QTest::qWait(100);capture(d,"form");
        QTRY_VERIFY_WITH_TIMEOUT(owner.screen()->availableGeometry().contains(d->frameGeometry()),2000);
        QVERIFY(!d->findChild<QScrollArea *>("dialog-body-scroll"));
        QVERIFY(d->sizeHint().height()>0);
        for(auto b:d->findChildren<QPushButton *>())if(b->text()=="确认保存")
            QVERIFY(d->rect().contains(QRect(b->mapTo(d,QPoint()),b->size())));
    }
    void standardPrompts() {
        QMessageBox message(QMessageBox::Question,"确认操作","是否确认处理这条提现申请？请核对金额和收款账户。",
                            QMessageBox::Yes|QMessageBox::No,&owner);
        message.show();QTest::qWait(100);capture(&message,"confirmation");
        const auto centerDelta=(bounds().center()-message.frameGeometry().center()).manhattanLength();
        QVERIFY2(centerDelta<=8,qPrintable(QString("center delta=%1 owner=%2,%3 dialog=%4,%5")
            .arg(centerDelta).arg(bounds().center().x()).arg(bounds().center().y())
            .arg(message.frameGeometry().center().x()).arg(message.frameGeometry().center().y())));
        for(auto b:message.buttons())
            QVERIFY(message.rect().contains(QRect(b->mapTo(&message,QPoint()),b->size())));
        message.close();
        QInputDialog input(&owner);input.setLabelText("请输入提现金额");input.setInputMode(QInputDialog::DoubleInput);
        input.show();QTest::qWait(100);capture(&input,"amount-input");
        QVERIFY(bounds().contains(input.frameGeometry()));
    }
    void fileActions() {
        QTemporaryDir temp;
        const auto path=temp.filePath("avatar.png");
        QImage avatar(8,8,QImage::Format_RGB32);avatar.fill(Qt::magenta);QVERIFY(avatar.save(path));
        QFileDialog open(&owner,"选择头像",path,"图片 (*.png)");
        open.setFileMode(QFileDialog::ExistingFile);open.show();QTest::qWait(100);
        auto box=open.findChild<QDialogButtonBox *>();QVERIFY(box);
        QTRY_VERIFY(box->button(QDialogButtonBox::Open)->isEnabled());
        capture(&open,"open-selection");
        QTest::mouseClick(box->button(QDialogButtonBox::Open),Qt::LeftButton);
        QTRY_COMPARE(open.result(),int(QDialog::Accepted));QCOMPARE(open.selectedFiles(),QStringList{path});
        QFileDialog save(&owner,"导出记录",temp.filePath("records.csv"),"CSV (*.csv)");
        save.setAcceptMode(QFileDialog::AcceptSave);save.show();QTest::qWait(100);
        capture(&save,"export-file");
        QVERIFY(bounds().contains(save.frameGeometry()));
        box=save.findChild<QDialogButtonBox *>();QVERIFY(box);
        auto button=box->button(QDialogButtonBox::Save);QVERIFY(button);
        QVERIFY(save.rect().contains(QRect(button->mapTo(&save,QPoint()),button->size())));
        QTest::mouseClick(button,Qt::LeftButton);
        QCOMPARE(save.result(),int(QDialog::Accepted));
        QCOMPARE(save.selectedFiles(),QStringList{temp.filePath("records.csv")});
    }
    void palette() {
        QVERIFY(qApp->palette().color(QPalette::Base).lightnessF()<.3);
        QVERIFY(qApp->palette().color(QPalette::Text).lightnessF()>.65);
    }
};
int main(int argc,char **argv) {
    QApplication app(argc,argv);applyTheme(app);
    DialogTest test;return QTest::qExec(&test,argc,argv);
}
#include "test_dialogs.moc"
