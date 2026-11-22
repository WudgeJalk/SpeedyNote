#include "SaveErrorBox.h"

#include <QMessageBox>
#include <QPushButton>

SaveErrorBox::Choice SaveErrorBox::ask(QWidget* parent, const QString& name, const QString& error,
                                       bool offerDiscard)
{
    const QString reason = error.isEmpty() ? tr("The reason is unknown.") : error;
    QMessageBox box(QMessageBox::Warning, tr("Not saved"),
                    tr("Could not save \"%1\". %2 The last saved version on disk is kept.")
                        .arg(name, reason),
                    QMessageBox::NoButton, parent);
    QPushButton* retry = box.addButton(tr("Try again"), QMessageBox::AcceptRole);
    QPushButton* elsewhere = box.addButton(tr("Save to another place..."), QMessageBox::ActionRole);
    QPushButton* discard = offerDiscard
        ? box.addButton(tr("Close without saving"), QMessageBox::DestructiveRole) : nullptr;
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(retry);
    box.exec();

    if (box.clickedButton() == retry) return Retry;
    if (box.clickedButton() == elsewhere) return SaveCopy;
    if (discard && box.clickedButton() == discard) return Discard;
    return Cancel;
}
