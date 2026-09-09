#include "face_image_processor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QStandardPaths>

#include <algorithm>

#ifdef ELECTRA_HAS_OPENCV
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#endif

namespace {
constexpr int OutputSide = 512;
constexpr double CropScale = 2.0;
constexpr int BilateralDiameter = 7;
constexpr double BilateralSigmaColor = 35.0;
constexpr double BilateralSigmaSpace = 35.0;
constexpr double BeautyAlpha = 1.04;
constexpr double BeautyBeta = 4.0;

#ifdef ELECTRA_HAS_OPENCV
cv::Mat qImageToMat(const QImage &source) {
    const QImage rgba = source.convertToFormat(QImage::Format_RGBA8888);
    cv::Mat view(rgba.height(), rgba.width(), CV_8UC4,
                 const_cast<uchar *>(rgba.constBits()), rgba.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(view, bgr, cv::COLOR_RGBA2BGR);
    return bgr.clone();
}

QImage matToQImage(const cv::Mat &source) {
    if (source.empty()) return {};
    cv::Mat rgba;
    if (source.channels() == 4)
        cv::cvtColor(source, rgba, cv::COLOR_BGRA2RGBA);
    else if (source.channels() == 3)
        cv::cvtColor(source, rgba, cv::COLOR_BGR2RGBA);
    else
        cv::cvtColor(source, rgba, cv::COLOR_GRAY2RGBA);
    return QImage(rgba.data, rgba.cols, rgba.rows, int(rgba.step),
                  QImage::Format_RGBA8888).copy();
}

QString cascadePath() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                              + "/electra-avatar";
    QDir().mkpath(directory);
    const QString path = directory + "/haarcascade_frontalface_default.xml";
    QFile resource(":/opencv/haarcascade_frontalface_default.xml");
    if ((!QFile::exists(path) || QFileInfo(path).size() != resource.size()) && resource.open(QIODevice::ReadOnly)) {
        QFile::remove(path);
        QFile output(path);
        if (output.open(QIODevice::WriteOnly)) output.write(resource.readAll());
    }
    return path;
}

QRect squareCrop(const QRect &face, const QSize &imageSize) {
    int side = qRound(qMax(face.width(), face.height()) * CropScale);
    side = qMin(side, qMin(imageSize.width(), imageSize.height()));
    // Move the face slightly upward so the crop also retains the shoulders.
    const QPoint center(face.center().x(), face.center().y() + qRound(face.height() * 0.18));
    int x = qBound(0, center.x() - side / 2, imageSize.width() - side);
    int y = qBound(0, center.y() - side / 2, imageSize.height() - side);
    return QRect(x, y, side, side);
}
#endif
}

bool FaceImageProcessor::isAvailable() {
#ifdef ELECTRA_HAS_OPENCV
    cv::CascadeClassifier classifier;
    return classifier.load(cascadePath().toStdString());
#else
    return false;
#endif
}

FaceImageProcessor::Result FaceImageProcessor::process(const QImage &image) {
    Result result;
    if (image.isNull() || image.width() < 80 || image.height() < 80) {
        result.error = "图片尺寸太小，请选择更清晰的图片。";
        return result;
    }
#ifndef ELECTRA_HAS_OPENCV
    result.error = "人脸检测组件未安装，可继续使用普通头像模式。";
    return result;
#else
    try {
        cv::CascadeClassifier classifier;
        if (!classifier.load(cascadePath().toStdString())) {
            result.error = "人脸检测组件加载失败，可继续使用普通头像模式。";
            return result;
        }
        const cv::Mat color = qImageToMat(image);
        cv::Mat gray;
        cv::cvtColor(color, gray, cv::COLOR_BGR2GRAY);
        cv::equalizeHist(gray, gray);
        std::vector<cv::Rect> faces;
        classifier.detectMultiScale(gray, faces, 1.1, 5, 0,
                                    cv::Size(qMax(40, image.width() / 12),
                                             qMax(40, image.height() / 12)));
        if (faces.empty()) {
            result.error = "未检测到人脸，请重新拍摄、重新选择图片，或使用普通头像模式。";
            return result;
        }
        const auto main = *std::max_element(faces.begin(), faces.end(), [](const cv::Rect &a, const cv::Rect &b) {
            return a.area() < b.area();
        });
        result.faceCount = int(faces.size());
        result.mainFace = QRect(main.x, main.y, main.width, main.height);

        result.detectionPreview = image.copy();
        QPainter marker(&result.detectionPreview);
        marker.setPen(QPen(QColor("#7ee7c1"), qMax(2, image.width() / 250)));
        marker.drawRect(result.mainFace);
        marker.end();

        const QRect crop = squareCrop(result.mainFace, image.size());
        result.croppedImage = image.copy(crop).scaled(OutputSide, OutputSide,
                                                       Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        cv::Mat cropped = qImageToMat(result.croppedImage);
        cv::Mat smoothed;
        cv::bilateralFilter(cropped, smoothed, BilateralDiameter,
                            BilateralSigmaColor, BilateralSigmaSpace);
        smoothed.convertTo(smoothed, -1, BeautyAlpha, BeautyBeta);
        result.beautifiedImage = matToQImage(smoothed);
        result.ok = !result.croppedImage.isNull() && !result.beautifiedImage.isNull();
        if (!result.ok) result.error = "图像处理失败，可切换到普通头像模式继续上传。";
    } catch (const cv::Exception &) {
        result.error = "人脸图像处理出现异常，可切换到普通头像模式继续上传。";
    } catch (...) {
        result.error = "图像处理出现异常，可切换到普通头像模式继续上传。";
    }
    return result;
#endif
}
