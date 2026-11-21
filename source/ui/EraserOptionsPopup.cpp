#include "EraserOptionsPopup.h"
#include "../core/DrawingSettings.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {

QLabel* hintLabel(const QString& text, QWidget* parent)
{
    QLabel* hint = new QLabel(text, parent);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    return hint;
}

} // namespace

EraserOptionsPopup::EraserOptionsPopup(QWidget* parent)
    : QFrame(parent, Qt::Popup)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setFrameShape(QFrame::StyledPanel);
    setMaximumWidth(320);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(6);

    const DrawingSettings& drawing = DrawingSettings::get();
    const auto addSwitch = [this, layout](const QString& text, const char* key, bool value) {
        auto* box = new QCheckBox(text, this);
        box->setChecked(value);
        connect(box, &QCheckBox::toggled, this, [key](bool on) {
            QSettings("SpeedyNote", "App").setValue(QLatin1String(key), on);
            DrawingSettings::reload();
        });
        layout->addWidget(box);
    };

    addSwitch(tr("Erase pen lines"), "drawing/eraserPen", drawing.eraserPen);
    addSwitch(tr("Erase marker lines"), "drawing/eraserMarker", drawing.eraserMarker);
    layout->addWidget(hintLabel(tr("The eraser leaves alone the lines of a tool that is off."), this));
    layout->addSpacing(4);
    addSwitch(tr("Go back to the last tool after erasing"), "drawing/eraserAutoDeselect",
              drawing.eraserAutoDeselect);
    layout->addSpacing(4);

    auto* clear = new QPushButton(tr("Clear the page"), this);
    connect(clear, &QPushButton::clicked, this, [this]() {
        emit clearPageRequested();
        close();
    });
    layout->addWidget(clear);
    layout->addWidget(hintLabel(tr("Erases the lines of this layer that the eraser may erase. "
                                   "Undo brings them back."), this));
}
