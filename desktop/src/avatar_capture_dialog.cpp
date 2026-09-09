#include "avatar_capture_dialog.h"

#include <QCamera>
#include <QComboBox>
#include <QImageCapture>
#include <QLabel>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

namespace {
constexpr int PreviewSide = 360;
}

AvatarCaptureDialog::AvatarCaptureDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("avatar-capture-dialog");
    setWindowTitle("拍照上传头像");
    setModal(true);
    setMinimumSize(430, 525);
    setStyleSheet(
        "QDialog#avatar-capture-dialog{background:#100b1b;}"
        "QStackedWidget#avatar-camera-view{background:#130d1c;border:0;}"
        "QLabel#avatar-camera-video{background:#130d1c;border:0;}"
        "QPushButton:focus{outline:none;}");
    setupUi();
    setState(State::Idle);
}

AvatarCaptureDialog::~AvatarCaptureDialog() { stopCamera(); }

void AvatarCaptureDialog::setupUi() {
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    cameraSelector = new QComboBox(this);
    cameraSelector->setObjectName("avatar-camera-selector");
    root->addWidget(cameraSelector);

    viewStack = new QStackedWidget(this);
    viewStack->setObjectName("avatar-camera-view");
    viewStack->setFixedSize(PreviewSide, PreviewSide);
    loadingLabel = new QLabel("正在打开摄像头…", viewStack);
    loadingLabel->setAlignment(Qt::AlignCenter);
    loadingLabel->setStyleSheet("background:#130d1c;color:#cfbddb;");
    videoPreview = new QLabel(viewStack);
    videoPreview->setObjectName("avatar-camera-video");
    videoPreview->setAlignment(Qt::AlignCenter);
    videoPreview->setFocusPolicy(Qt::NoFocus);
    videoPreview->setAutoFillBackground(true);
    QPalette videoPalette = videoPreview->palette();
    videoPalette.setColor(QPalette::Window, QColor("#130d1c"));
    videoPreview->setPalette(videoPalette);
    videoPreview->setMinimumSize(PreviewSide, PreviewSide);
    frozenLabel = new QLabel(viewStack);
    frozenLabel->setAlignment(Qt::AlignCenter);
    frozenLabel->setStyleSheet("background:#130d1c;");
    viewStack->addWidget(loadingLabel);
    viewStack->addWidget(videoPreview);
    viewStack->addWidget(frozenLabel);
    root->addWidget(viewStack, 0, Qt::AlignHCenter);

    hintLabel = new QLabel(this);
    hintLabel->setAlignment(Qt::AlignCenter);
    hintLabel->setStyleSheet("color:#a991ba;font-size:12px;");
    root->addWidget(hintLabel);

    auto actions = new QHBoxLayout;
    shutterButton = new QPushButton("拍摄", this);
    shutterButton->setObjectName("avatar-camera-shutter");
    shutterButton->setProperty("primary", true);
    retakeButton = new QPushButton("重拍", this);
    confirmButton = new QPushButton("使用照片", this);
    confirmButton->setObjectName("avatar-camera-confirm");
    confirmButton->setProperty("primary", true);
    auto cancelButton = new QPushButton("取消", this);
    for (auto button : {shutterButton, retakeButton, confirmButton, cancelButton}) {
        button->setMinimumHeight(38);
        actions->addWidget(button);
    }
    root->addLayout(actions);

    connect(shutterButton, &QPushButton::clicked, this, &AvatarCaptureDialog::capture);
    connect(retakeButton, &QPushButton::clicked, this, &AvatarCaptureDialog::retake);
    connect(confirmButton, &QPushButton::clicked, this, &AvatarCaptureDialog::confirm);
    connect(cancelButton, &QPushButton::clicked, this, &AvatarCaptureDialog::reject);
    connect(cameraSelector, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) { if (isVisible()) startCamera(index); });
}

void AvatarCaptureDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (state != State::Idle) return;
    const auto devices = QMediaDevices::videoInputs();
    {
        const QSignalBlocker blocker(cameraSelector);
        cameraSelector->clear();
        for (const auto &device : devices) cameraSelector->addItem(device.description());
    }
    if (devices.isEmpty()) {
        QMessageBox::warning(this, "拍照", "未检测到可用摄像头，请检查系统相机权限。");
        QTimer::singleShot(0, this, &AvatarCaptureDialog::reject);
        return;
    }
    startCamera(0);
}

void AvatarCaptureDialog::startCamera(int deviceIndex) {
    const auto devices = QMediaDevices::videoInputs();
    if (deviceIndex < 0 || deviceIndex >= devices.size()) return;
    viewStack->setCurrentWidget(loadingLabel);
    stopCamera();
    captureSession = new QMediaCaptureSession(this);
    imageCapture = new QImageCapture(this);
    videoSink = new QVideoSink(this);
    camera = new QCamera(devices.at(deviceIndex), this);
    captureSession->setCamera(camera);
    captureSession->setVideoSink(videoSink);
    captureSession->setImageCapture(imageCapture);
    connect(videoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (state != State::Previewing || !frame.isValid()) return;
        const QImage image = frame.toImage();
        if (image.isNull()) return;
        videoPreview->setPixmap(QPixmap::fromImage(image).scaled(
            PreviewSide, PreviewSide, Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation));
    });
    connect(camera, &QCamera::errorOccurred, this,
            [this](QCamera::Error, const QString &message) {
                hintLabel->setText(message.isEmpty() ? "摄像头打开失败，请检查系统权限。" : message);
            });
    connect(camera, &QCamera::activeChanged, this, [this](bool active) {
        if (!active || state != State::Previewing) return;
        viewStack->setCurrentWidget(videoPreview);
        hintLabel->setText("对准后点击“拍摄”");
        shutterButton->setEnabled(imageCapture && imageCapture->isReadyForCapture());
    });
    connect(imageCapture, &QImageCapture::readyForCaptureChanged, shutterButton,
            [this](bool ready) { shutterButton->setEnabled(ready && state == State::Previewing); });
    connect(imageCapture, &QImageCapture::imageCaptured, this,
            [this](int, const QImage &image) {
                if (image.isNull()) {
                    hintLabel->setText("拍摄失败，请重试。");
                    return;
                }
                frozenImage = image;
                const auto preview = QPixmap::fromImage(image).scaled(
                    PreviewSide, PreviewSide, Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation);
                frozenLabel->setPixmap(preview);
                setState(State::Frozen);
                // Hide the native video surface before stopping it. Windows may
                // otherwise clear that surface to white for one frame.
                camera->stop();
            });
    connect(imageCapture, &QImageCapture::errorOccurred, this,
            [this](int, QImageCapture::Error, const QString &message) {
                hintLabel->setText(message.isEmpty() ? "拍摄失败，请重试。" : message);
                shutterButton->setEnabled(true);
            });
    setState(State::Previewing);
    camera->start();
}

void AvatarCaptureDialog::stopCamera() {
    if (camera) camera->stop();
    if (captureSession) {
        captureSession->setCamera(nullptr);
        captureSession->setImageCapture(nullptr);
        captureSession->setVideoSink(nullptr);
    }
    delete captureSession;
    delete videoSink;
    delete imageCapture;
    delete camera;
    camera = nullptr;
    imageCapture = nullptr;
    videoSink = nullptr;
    captureSession = nullptr;
    if (videoPreview) videoPreview->clear();
}

void AvatarCaptureDialog::setState(State nextState) {
    state = nextState;
    const bool previewing = state == State::Previewing;
    const bool frozen = state == State::Frozen;
    shutterButton->setVisible(previewing);
    shutterButton->setEnabled(previewing && imageCapture && imageCapture->isReadyForCapture());
    retakeButton->setVisible(frozen);
    confirmButton->setVisible(frozen);
    cameraSelector->setEnabled(!frozen);
    if (previewing || state == State::Idle) {
        viewStack->setCurrentWidget(loadingLabel);
        hintLabel->setText("正在打开摄像头…");
    } else if (frozen) {
        viewStack->setCurrentWidget(frozenLabel);
        hintLabel->setText("可重拍，或使用该照片进入头像裁剪。");
    }
}

void AvatarCaptureDialog::capture() {
    if (state != State::Previewing || !imageCapture || !imageCapture->isReadyForCapture()) return;
    shutterButton->setEnabled(false);
    hintLabel->setText("正在拍摄…");
    imageCapture->capture();
}

void AvatarCaptureDialog::retake() {
    frozenImage = {};
    frozenLabel->clear();
    setState(State::Previewing);
    if (camera) camera->start();
}

void AvatarCaptureDialog::confirm() {
    if (state != State::Frozen || frozenImage.isNull()) return;
    state = State::Accepted;
    hide();
    stopCamera();
    accept();
}

void AvatarCaptureDialog::reject() {
    hide();
    stopCamera();
    frozenImage = {};
    state = State::Idle;
    QDialog::reject();
}
