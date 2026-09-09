#pragma once

#include <QtWidgets>

class DraggableBottomSheet : public QWidget {
    Q_OBJECT
  public:
    explicit DraggableBottomSheet(QWidget *parent = nullptr);
    void setBackground(QWidget *widget);
    QVBoxLayout *contentLayout() const { return content; }
    void expand();

  protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    enum Position { Collapsed, Resting, Expanded };
    int yFor(Position position) const;
    void moveSheet(int y);
    void snap(Position position);
    QWidget *background = nullptr;
    QFrame *sheet;
    QWidget *grabArea;
    QVBoxLayout *content;
    QPropertyAnimation *animation;
    Position position = Resting;
    bool dragging = false;
    QPoint pressGlobal;
    int pressY = 0;
};
