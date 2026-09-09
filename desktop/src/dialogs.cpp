#include "ui.h"

namespace {
QRect dialogBounds(QDialog *dialog) {
    QWidget *owner = dialog->parentWidget();
    while (owner && owner->parentWidget()) owner = owner->parentWidget();
    const auto screen = owner ? owner->screen() : dialog->screen();
    auto bounds = screen->availableGeometry();
    if (owner) bounds = bounds.intersected(QRect(owner->mapToGlobal(QPoint()), owner->size()));
    return bounds.adjusted(8, 8, -8, -8);
}

void compactFilePicker(QFileDialog *dialog) {
    dialog->setViewMode(QFileDialog::List);
    dialog->setLabelText(QFileDialog::LookIn, "位置");
    dialog->setLabelText(QFileDialog::FileName, "文件名");
    dialog->setLabelText(QFileDialog::FileType, "类型");
    dialog->setLabelText(QFileDialog::Accept, dialog->acceptMode() == QFileDialog::AcceptSave ? "保存" : "打开");
    dialog->setLabelText(QFileDialog::Reject, "取消");
    // Keep the standard filesystem model, selection validation and overwrite confirmation.
    // Only remove optional chrome from its compact presentation.
    for (const auto &name : {"sidebar", "backButton", "forwardButton", "listModeButton", "detailModeButton"})
        if (auto widget = dialog->findChild<QWidget *>(name)) widget->hide();
    for (auto combo : dialog->findChildren<QComboBox *>()) {
        combo->setMinimumWidth(0);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    if (auto box = dialog->findChild<QDialogButtonBox *>()) {
        box->setOrientation(Qt::Horizontal);
        if (auto grid = qobject_cast<QGridLayout *>(dialog->layout())) {
            grid->removeWidget(box);
            grid->addWidget(box, grid->rowCount(), 0, 1, grid->columnCount());
        }
    }
}

void prepareContent(QDialog *dialog, int width) {
    if (dialog->property("dialogPrepared").toBool()) return;
    dialog->setProperty("dialogPrepared", true);
    if (auto file = qobject_cast<QFileDialog *>(dialog)) {
        if (width < 600) compactFilePicker(file);
        return;
    }
    auto header = dialog->findChild<QWidget *>("dialog-header");
    auto original = dialog->layout();
    if (!header || !original) return; // Let standard message/input dialogs lay out their own text.
    for (auto form : dialog->findChildren<QFormLayout *>())
        if (width < 600) form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    for (auto label : dialog->findChildren<QLabel *>()) {
        const int naturalWidth = label->sizeHint().width();
        label->setWordWrap(true);
        if (width < 600) {
            label->setMaximumWidth(qMax(120, width - 40));
            if (!label->parentWidget() || label->parentWidget()->objectName() != "dialog-header")
                label->setMinimumWidth(qMin(naturalWidth, qMax(120, width - 72)));
        }
    }
    original->removeWidget(header);
    QWidget *footer = nullptr;
    if (original->count()) {
        auto last = original->itemAt(original->count() - 1)->widget();
        if (qobject_cast<QPushButton *>(last) || qobject_cast<QDialogButtonBox *>(last)) {
            footer = last;
            original->removeWidget(footer);
        }
    }
    auto content = new QWidget;
    content->setLayout(original); // Transfer ownership of the existing form without changing callbacks.
    auto layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addWidget(header);
    auto scroll = new QScrollArea;
    scroll->setObjectName("dialog-body-scroll");
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    if (footer) layout->addWidget(footer);
}

void fitDialog(QDialog *dialog) {
    const auto bounds = dialogBounds(dialog);
    if (bounds.isEmpty()) return;
    const QSize target(qMax(240, qRound(bounds.width() * DialogAppearance::WidthRatio)),
                       qMax(220, qRound(bounds.height() * DialogAppearance::HeightRatio)));
    prepareContent(dialog, target.width());
    if (dialog->layout()) dialog->layout()->setSizeConstraint(QLayout::SetNoConstraint);
    dialog->setFixedSize(target.boundedTo(bounds.size()));
    if (dialog->layout()) dialog->layout()->activate();
    dialog->move(dialog->pos() + bounds.center() - dialog->frameGeometry().center());
}

class DialogPolicy : public QObject {
  public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show) {
            if (auto dialog = qobject_cast<QDialog *>(object)) {
                dialog->setStyleSheet(dialog->styleSheet() + QString(
                    "QDialog{background:%1;border:%2px solid %3;border-radius:%4px;}")
                    .arg(DialogAppearance::BackgroundColor)
                    .arg(DialogAppearance::BorderWidth)
                    .arg(DialogAppearance::BorderColor)
                    .arg(DialogAppearance::CornerRadius));
                // Run after the platform's default placement, including modal exec() dialogs.
                QTimer::singleShot(0, dialog, [dialog] { fitDialog(dialog); });
            }
        }
        return false;
    }
};
} // namespace

void installDialogPolicy(QApplication &app) {
    app.setAttribute(Qt::AA_DontUseNativeDialogs);
    app.installEventFilter(new DialogPolicy(&app));
}
