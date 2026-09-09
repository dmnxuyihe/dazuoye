#pragma once

#include <QDialog>
#include <QImage>

class QCamera;
class QComboBox;
class QImageCapture;
class QLabel;
class QMediaCaptureSession;
class QPushButton;
class QStackedWidget;
class QVideoSink;

class AvatarCaptureDialog : public QDialog {
    Q_OBJECT

  public:
    explicit AvatarCaptureDialog(QWidget *parent = nullptr);
    ~AvatarCaptureDialog() override;

    QImage capturedImage() const { return frozenImage; }

  protected:
    void showEvent(QShowEvent *event) override;
    void reject() override;

  private:
    enum class State { Idle, Previewing, Frozen, Accepted };

    void setupUi();
    void startCamera(int deviceIndex);
    void stopCamera();
    void setState(State state);
    void capture();
    void retake();
    void confirm();

    State state = State::Idle;
    QImage frozenImage;
    QStackedWidget *viewStack = nullptr;
    QLabel *loadingLabel = nullptr;
    QLabel *videoPreview = nullptr;
    QVideoSink *videoSink = nullptr;
    QLabel *frozenLabel = nullptr;
    QLabel *hintLabel = nullptr;
    QComboBox *cameraSelector = nullptr;
    QPushButton *shutterButton = nullptr;
    QPushButton *retakeButton = nullptr;
    QPushButton *confirmButton = nullptr;
    QCamera *camera = nullptr;
    QMediaCaptureSession *captureSession = nullptr;
    QImageCapture *imageCapture = nullptr;
};
