#pragma once
#include "ui.h"

class ChargerPicker : public QWidget {
  public:
    explicit ChargerPicker(QWidget *parent = nullptr);
    void setChargers(const QJsonArray &items, bool readOnly, bool fastOnly,
                     const QString &statusFilter = {}, const QString &kindFilter = {});
    QString selectedId() const { return selected; }
    std::function<void()> selectionChanged;
  protected:
    void resizeEvent(QResizeEvent *event) override;
  private:
    void arrange();
    void updateSelection();
    QGridLayout *grid;
    QList<QPushButton *> buttons;
    QString selected;
    bool initialized=false;
};
