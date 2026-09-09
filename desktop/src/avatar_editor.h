#pragma once
#include "ui.h"

class AvatarCanvas : public QWidget {
  public:
    explicit AvatarCanvas(QWidget *parent=nullptr);
    void setImage(const QImage &image);
    void rotate();
    void mirror();
    void reset();
    void setZoom(int percent);
    void setBrightness(int value);
    QImage result() const;
  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
  private:
    QImage original, working;
    double zoom=1.;int brightness=0;
    QPointF offset,last;
};
void showAvatarEditor(QWidget *owner, ApiClient *api, std::function<void(const QJsonObject &)> saved,
                      const QImage &current = {});
