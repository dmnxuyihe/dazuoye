#pragma once

#include <QtWidgets>

class DraggableBottomSheet : public QWidget {
    Q_OBJECT
  public:
    explicit DraggableBottomSheet(QWidget *parent = nullptr);
    void setBackground(QWidget *widget);
    QVBoxLayout *contentLayout() const { return content; }
    void expand();

  signals:
    // Emitted only for a user drag that crosses into Expanded. Programmatic
    // expansion (for example after clicking a map marker) does not emit it.
    void userExpanded();

  protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    enum Position { Collapsed, Half, Expanded };
    int yFor(Position position) const;
    void moveSheet(int y);
    void snap(Position position, bool userInitiated = false);
    QWidget *background = nullptr;
    QFrame *sheet;
    QWidget *grabArea;
    QVBoxLayout *content;
    QPropertyAnimation *animation;
    Position position = Half;
    bool dragging = false;
    QPoint pressGlobal;
    int pressY = 0;
};
