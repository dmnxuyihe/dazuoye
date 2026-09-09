#pragma once

#include <QImage>
#include <QRect>
#include <QString>

class FaceImageProcessor {
  public:
    struct Result {
        bool ok = false;
        QString error;
        QImage detectionPreview;
        QImage croppedImage;
        QImage beautifiedImage;
        QRect mainFace;
        int faceCount = 0;
    };

    static bool isAvailable();
    static Result process(const QImage &image);
};
