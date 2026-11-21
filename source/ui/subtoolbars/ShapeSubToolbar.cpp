#include "ShapeSubToolbar.h"
#include "../widgets/ColorListRow.h"
#include "../widgets/IconPresetButton.h"
#include "../widgets/ThicknessPresetButton.h"

#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QSettings>

namespace {
// The pen's defaults, so a new user finds the same colours on both tools.
const QList<QColor> kColors = {
    QColor(0xFF, 0x00, 0x00), QColor(0x00, 0x00, 0xFF), QColor(0x00, 0x00, 0x00),
    QColor(0x2E, 0x7D, 0x32), QColor(0x7B, 0x1F, 0xA2), QColor(0x75, 0x75, 0x75)};
const QList<QColor> kColorsDark = {
    QColor(0xFF, 0x77, 0x55), QColor(0x66, 0xCC, 0xFF), QColor(0xFF, 0xFF, 0xFF),
    QColor(0x81, 0xC7, 0x84), QColor(0xCE, 0x93, 0xD8), QColor(0xBD, 0xBD, 0xBD)};
const QString kGroup = QStringLiteral("shapes");

StrokePreset defaultPreset()
{
    StrokePreset preset;
    preset.width = 2.0;
    preset.minWidth = -1.0;   // Shapes have one width: no minimum
    return preset;
}
} // namespace

QIcon ShapeSubToolbar::kindIcon(int kind, bool dark)
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(dark ? QColor(235, 235, 235) : QColor(40, 40, 40), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath path;
    switch (kind) {
    case Free:
        path.moveTo(10, 44);
        path.cubicTo(18, 6, 30, 58, 38, 28);
        path.cubicTo(42, 14, 52, 18, 54, 30);
        break;
    case Elbow:
        path.moveTo(12, 10);
        path.lineTo(12, 40);
        path.quadTo(12, 52, 24, 52);
        path.lineTo(54, 52);
        break;
    case Curved:
        path.moveTo(10, 52);
        path.cubicTo(44, 52, 20, 12, 54, 12);
        break;
    case Rect:      path.addRect(10, 16, 44, 32); break;
    case Ellipse:   path.addEllipse(8, 14, 48, 36); break;
    case Triangle:
        path.moveTo(32, 10); path.lineTo(55, 52); path.lineTo(9, 52); path.closeSubpath();
        break;
    case Diamond:
        path.moveTo(32, 7); path.lineTo(57, 32); path.lineTo(32, 57); path.lineTo(7, 32); path.closeSubpath();
        break;
    default:        path.addRoundedRect(8, 16, 48, 32, 11, 11); break;
    }
    p.drawPath(path);
    return QIcon(pixmap);
}

ShapeSubToolbar::ShapeSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    const QString tips[KindCount] = {
        tr("Free shape: each line is cleaned when the pen lifts"),
        tr("Connector with right angles (attaches to a shape side)"),
        tr("Curved connector (attaches to a shape side)"),
        tr("Rectangle"), tr("Circle or ellipse"), tr("Triangle"), tr("Diamond"),
        tr("Rounded rectangle")};
    for (int i = 0; i < KindCount; ++i) {
        m_kindButtons[i] = new IconPresetButton(this);
        m_kindButtons[i]->setToolTip(tips[i]);
        addWidget(m_kindButtons[i]);
        connect(m_kindButtons[i], &IconPresetButton::clicked, this, [this, i]() {
            selectKind(i);
            QSettings().setValue(kGroup + QStringLiteral("/kind"), i);
            emit shapeKindChanged(i);
        });
    }

    addSeparator();

    m_presetButton = new ThicknessPresetButton(this);
    m_presetButton->setToolTip(tr("Line of the shapes (tap to change: width, dashes, arrow heads)"));
    m_presetButton->setSelected(true);
    addWidget(m_presetButton);
    connect(m_presetButton, &ThicknessPresetButton::clicked, this, &ShapeSubToolbar::editPreset);

    addSeparator();

    const bool darkMode = palette().color(QPalette::Window).lightness() < 128;
    m_colors = new ColorListRow(kGroup, darkMode ? kColorsDark : kColors, this);
    addWidget(m_colors);
    connect(m_colors, &ColorListRow::colorSelected, this, [this](const QColor& color) {
        updateButtons();
        QSettings().setValue(kGroup + QStringLiteral("/selectedColor"), m_colors->selectedIndex());
        emit shapeColorChanged(color);
    });

    setDarkMode(darkMode);
    refreshFromSettings();
}

void ShapeSubToolbar::refreshFromSettings()
{
    QSettings settings;
    settings.beginGroup(kGroup);
    const StrokePreset d = defaultPreset();
    m_preset.width = qBound(0.5, settings.value(QStringLiteral("thickness"), d.width).toReal(), 50.0);
    m_preset.minWidth = d.minWidth;
    m_preset.lineStyle = quint8(qBound(0, settings.value(QStringLiteral("lineStyle"), 0).toInt(), 2));
    m_preset.arrowStart = quint8(qBound(0, settings.value(QStringLiteral("arrowStart"), 0).toInt(), 2));
    m_preset.arrowEnd = quint8(qBound(0, settings.value(QStringLiteral("arrowEnd"), 0).toInt(), 2));
    m_preset.rounded = settings.value(QStringLiteral("rounded"), true).toBool();
    const int kind = settings.value(QStringLiteral("kind"), int(Free)).toInt();
    const int color = settings.value(QStringLiteral("selectedColor"), 2).toInt();
    settings.endGroup();

    m_colors->reload();
    m_colors->setSelectedIndex(color, false);
    selectKind(qBound(0, kind, KindCount - 1));
    updateButtons();
}

void ShapeSubToolbar::setDarkMode(bool darkMode)
{
    SubToolbar::setDarkMode(darkMode);
    for (int i = 0; i < KindCount; ++i) {
        m_kindButtons[i]->setDarkMode(darkMode);
        m_kindButtons[i]->setIcon(kindIcon(i, darkMode));
    }
}

QColor ShapeSubToolbar::currentColor() const
{
    return m_colors->currentColor();
}

StrokeStyle::Style ShapeSubToolbar::currentStyle() const
{
    StrokeStyle::Style style;
    style.lineStyle = m_preset.lineStyle;
    style.arrowStart = m_preset.arrowStart;
    style.arrowEnd = m_preset.arrowEnd;
    style.rounded = m_preset.rounded;
    return style;
}

void ShapeSubToolbar::emitCurrentValues()
{
    emit shapeKindChanged(m_kind);
    emit shapeColorChanged(currentColor());
    emit shapeStrokeChanged(currentThickness(), currentStyle());
}

void ShapeSubToolbar::cycleColor()
{
    m_colors->cycle();
}

void ShapeSubToolbar::selectKind(int kind)
{
    m_kind = kind;
    for (int i = 0; i < KindCount; ++i) m_kindButtons[i]->setSelected(i == kind);
}

void ShapeSubToolbar::editPreset()
{
    StrokePresetDialog dialog(m_preset, defaultPreset(), 0.5, 50.0, this);
    dialog.setWindowTitle(tr("Shape line settings"));
    if (dialog.exec() != QDialog::Accepted) return;
    StrokePreset preset = dialog.preset();
    preset.minWidth = defaultPreset().minWidth;
    if (preset == m_preset) return;
    m_preset = preset;
    QSettings settings;
    settings.beginGroup(kGroup);
    settings.setValue(QStringLiteral("thickness"), m_preset.width);
    settings.setValue(QStringLiteral("lineStyle"), int(m_preset.lineStyle));
    settings.setValue(QStringLiteral("arrowStart"), int(m_preset.arrowStart));
    settings.setValue(QStringLiteral("arrowEnd"), int(m_preset.arrowEnd));
    settings.setValue(QStringLiteral("rounded"), m_preset.rounded);
    settings.endGroup();
    updateButtons();
    emit shapeStrokeChanged(currentThickness(), currentStyle());
}

void ShapeSubToolbar::updateButtons()
{
    m_presetButton->setThickness(m_preset.width);
    m_presetButton->setLineStyle(m_preset.lineStyle);
    m_presetButton->setArrows(m_preset.arrowStart, m_preset.arrowEnd);
    m_presetButton->setLineColor(currentColor());
}
