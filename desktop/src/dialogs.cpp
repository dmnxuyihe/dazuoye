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

void prepareContent(QDialog *dialog) {
    if (dialog->property("dialogPrepared").toBool()) return;
    dialog->setProperty("dialogPrepared", true);
    if (auto file = qobject_cast<QFileDialog *>(dialog)) {
        compactFilePicker(file);
        return;
    }
    for (auto form : dialog->findChildren<QFormLayout *>())
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    for (auto label : dialog->findChildren<QLabel *>()) {
        label->setWordWrap(true);
        label->setMaximumWidth(qMax(260, dialogBounds(dialog).width() - 64));
    }
}

void centerDialogContent(QDialog *dialog) {
    if (auto layout = dialog->layout()) {
        layout->setContentsMargins(24, 22, 24, 22);
        layout->setSpacing(qMax(12, layout->spacing()));
    }

    const bool filePicker = qobject_cast<QFileDialog *>(dialog);
    for (auto label : dialog->findChildren<QLabel *>()) {
        // Keep the file browser itself conventional, while centering its explanatory text.
        if (filePicker && label->inherits("QFileDialogLabel"))
            continue;
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
    }
    for (auto lineEdit : dialog->findChildren<QLineEdit *>())
        lineEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    for (auto spinBox : dialog->findChildren<QAbstractSpinBox *>())
        if (auto editor = spinBox->findChild<QLineEdit *>())
            editor->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    for (auto form : dialog->findChildren<QFormLayout *>()) {
        form->setLabelAlignment(Qt::AlignCenter);
        form->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(qMax(12, form->verticalSpacing()));
    }
    for (auto box : dialog->findChildren<QDialogButtonBox *>())
        box->setCenterButtons(true);
    for (auto table : dialog->findChildren<QTableWidget *>())
        for (int row = 0; row < table->rowCount(); ++row)
            for (int column = 0; column < table->columnCount(); ++column)
                if (auto item = table->item(row, column)) item->setTextAlignment(Qt::AlignCenter);
}

void centerDialogFrame(QDialog *dialog) {
    const auto bounds = dialogBounds(dialog);
    if (bounds.isEmpty()) return;
    dialog->move(dialog->pos() + bounds.center() - dialog->frameGeometry().center());
    const QRect frame = dialog->frameGeometry();
    QPoint correction;
    if (frame.left() < bounds.left()) correction.rx() += bounds.left() - frame.left();
    if (frame.right() > bounds.right()) correction.rx() -= frame.right() - bounds.right();
    if (frame.top() < bounds.top()) correction.ry() += bounds.top() - frame.top();
    if (frame.bottom() > bounds.bottom()) correction.ry() -= frame.bottom() - bounds.bottom();
    if (!correction.isNull()) dialog->move(dialog->pos() + correction);
}

void fitDialog(QDialog *dialog) {
    const auto bounds = dialogBounds(dialog);
    if (bounds.isEmpty()) return;
    prepareContent(dialog);
    centerDialogContent(dialog);
    dialog->setMinimumSize(0, 0);
    dialog->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    if (dialog->layout()) {
        dialog->layout()->setSizeConstraint(QLayout::SetMinAndMaxSize);
        dialog->layout()->activate();
    }
    // Manager/detail dialogs intentionally reserve room for tables and scroll areas.
    // Still apply the common content preparation and platform-frame bounds.
    QSize target = dialog->property("preserveDialogSize").toBool()
                       ? dialog->size() : dialog->sizeHint().expandedTo(QSize(320, 180));
    if (qobject_cast<QFileDialog *>(dialog)) target = target.expandedTo(QSize(560, 380));
    const QSize frameExtra = dialog->frameGeometry().size() - dialog->size();
    target = target.boundedTo(bounds.size() - frameExtra);
    dialog->resize(target);
    centerDialogFrame(dialog);
}

class DialogPolicy : public QObject {
  public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Resize) {
            if (auto dialog = qobject_cast<QDialog *>(object); dialog && dialog->isVisible() &&
                !dialog->property("dialogCenterQueued").toBool()) {
                dialog->setProperty("dialogCenterQueued", true);
                QTimer::singleShot(0, dialog, [dialog] {
                    dialog->setProperty("dialogCenterQueued", false);
                    centerDialogFrame(dialog);
                });
            }
        }
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
                // Some platform dialogs update their frame after the first layout pass.
                QTimer::singleShot(50, dialog, [dialog] { fitDialog(dialog); });
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
