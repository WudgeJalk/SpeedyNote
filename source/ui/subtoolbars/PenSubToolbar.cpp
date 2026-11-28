#include "PenSubToolbar.h"
#include "../widgets/ColorListRow.h"
#include "../widgets/IconPresetButton.h"
#include "../widgets/ThicknessPresetButton.h"

#include <QLabel>
#include <QPalette>
#include <QSettings>
#include <QTimer>

namespace {
// The first three are the old presets, so a user who changes nothing sees them.
const QList<QColor> kColors = {
    QColor(0xFF, 0x00, 0x00), QColor(0x00, 0x00, 0xFF), QColor(0x00, 0x00, 0x00),
    QColor(0x2E, 0x7D, 0x32), QColor(0x7B, 0x1F, 0xA2), QColor(0x75, 0x75, 0x75)};
const QList<QColor> kColorsDark = {
    QColor(0xFF, 0x77, 0x55), QColor(0x66, 0xCC, 0xFF), QColor(0xFF, 0xFF, 0xFF),
    QColor(0x81, 0xC7, 0x84), QColor(0xCE, 0x93, 0xD8), QColor(0xBD, 0xBD, 0xBD)};
const QString kGroup = QStringLiteral("pen");
}

PenSubToolbar::PenSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    // With one pen icon, a tap on the pen in use hides this bar: the pen
    // window (pen type, stabilisation...) opens from here instead.
    m_windowButton = new IconPresetButton(this);
    m_windowButton->setIconName(QStringLiteral("settings"));
    m_windowButton->setToolTip(tr("Pen type and settings"));
    addWidget(m_windowButton);
    connect(m_windowButton, &IconPresetButton::clicked, this, &PenSubToolbar::windowRequested);

    m_multiPenButton = new IconPresetButton(this);
    m_multiPenButton->setIconName(QStringLiteral("multipen"));
    addWidget(m_multiPenButton);
    connect(m_multiPenButton, &IconPresetButton::clicked, this, &PenSubToolbar::cycleMultiPen);

    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_presetButtons[i] = new ThicknessPresetButton(this);
        m_presetButtons[i]->setToolTip(tr("Stroke preset %1 (click to select, click again to edit)").arg(i + 1));
        addWidget(m_presetButtons[i]);
        connect(m_presetButtons[i], &ThicknessPresetButton::clicked, this, [this, i]() { onPresetClicked(i); });
        connect(m_presetButtons[i], &ThicknessPresetButton::editRequested, this, [this, i]() { onPresetEditRequested(i); });
    }

    addSeparator();

    const bool darkMode = palette().color(QPalette::Window).lightness() < 128;
    m_colors = new ColorListRow(kGroup, darkMode ? kColorsDark : kColors, this);
    addWidget(m_colors);
    connect(m_colors, &ColorListRow::colorSelected, this, &PenSubToolbar::onColorSelected);
    connect(m_colors, &ColorListRow::listChanged, this, &PenSubToolbar::saveSelectionToSettings);

    loadFromSettings();
}

void PenSubToolbar::setWindowButtonVisible(bool visible)
{
    m_windowButton->setVisible(visible);
}

void PenSubToolbar::setDarkMode(bool darkMode)
{
    SubToolbar::setDarkMode(darkMode);
    m_windowButton->setDarkMode(darkMode);
    m_multiPenButton->setDarkMode(darkMode);
}

QStringList PenSubToolbar::multiPenIds(int penType)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("pen/multi%1").arg(penType));
    QStringList ids = settings.childGroups();
    ids.sort();   // Ids are zero-padded numbers: oldest first
    return ids;
}

QString PenSubToolbar::multiPenKey(int penType, const QString& id)
{
    return QStringLiteral("pen/multi%1/%2").arg(penType).arg(id);
}

QString PenSubToolbar::addMultiPen(int penType, const QString& name)
{
    const QStringList ids = multiPenIds(penType);
    const QString id = QStringLiteral("%1").arg(ids.isEmpty() ? 1 : ids.last().toInt() + 1, 4, 10, QLatin1Char('0'));
    QSettings().setValue(multiPenKey(penType, id) + QStringLiteral("/name"), name);
    return id;
}

QString PenSubToolbar::presetGroup() const
{
    return m_multiPenId.isEmpty() ? kGroup : multiPenKey(m_penType, m_multiPenId);
}

void PenSubToolbar::cycleMultiPen()
{
    const QStringList ids = multiPenIds(m_penType);
    if (ids.isEmpty()) return;
    const QString next = ids[(ids.indexOf(m_multiPenId) + 1) % ids.size()];
    QSettings().setValue(multiPenKey(m_penType, QStringLiteral("current")), next);
    loadPresets();
    updatePresetButtons();
    emitPreset();
    showMultiPenName(QSettings().value(presetGroup() + QStringLiteral("/name")).toString());
}

void PenSubToolbar::showMultiPenName(const QString& name)
{
    QWidget* top = window();
    if (!m_multiPenLabel || m_multiPenLabel->parentWidget() != top) {
        delete m_multiPenLabel;
        m_multiPenLabel = new QLabel(top);
        m_multiPenLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_multiPenTimer = new QTimer(m_multiPenLabel);
        m_multiPenTimer->setSingleShot(true);
        connect(m_multiPenTimer, &QTimer::timeout, m_multiPenLabel, &QWidget::hide);
    }
    // The card of the floating bar (Toolbar's paintPill).
    const bool dark = m_multiPenButton->isDarkMode();
    m_multiPenLabel->setStyleSheet(QStringLiteral(
        "background: %1; color: %2; border: 1px solid %3; border-radius: 10px; padding: 5px 10px;")
        .arg(dark ? "#000000" : "#ffffff", dark ? "#ffffff" : "#000000",
             dark ? "rgba(255,255,255,30)" : "rgba(0,0,0,25)"));
    m_multiPenLabel->setText(name);
    m_multiPenLabel->adjustSize();
    // Left of the bar, level with the button; right of it when there is no room.
    QWidget* bar = parentWidget() ? parentWidget() : this;
    const QPoint barAt = bar->mapTo(top, QPoint(0, 0));
    int x = barAt.x() - m_multiPenLabel->width() - 6;
    if (x < 0) x = barAt.x() + bar->width() + 6;
    const int y = m_multiPenButton->mapTo(top, QPoint(0, 0)).y()
                  + (m_multiPenButton->height() - m_multiPenLabel->height()) / 2;
    m_multiPenLabel->move(x, y);
    m_multiPenLabel->show();
    m_multiPenLabel->raise();
    m_multiPenTimer->start(1500);
}

StrokePreset PenSubToolbar::defaultPreset(int index) const
{
    StrokePreset preset;
    preset.width = DEFAULT_THICKNESSES[index];
    preset.minWidth = qBound(0.0, m_legacyMinWidth, preset.width);
    return preset;
}

void PenSubToolbar::loadFromSettings()
{
    QSettings settings;
    // A user who tuned the old global setting keeps it as the presets' minimum.
    m_legacyMinWidth = settings.value(QStringLiteral("tools/minStrokeWidth"), DEFAULT_MIN_WIDTH).toReal();

    settings.beginGroup(kGroup);
    m_penType = quint8(qBound(0, settings.value(QStringLiteral("penType"), 0).toInt(), int(StrokeStyle::Brush)));
    m_savedColorIndex = settings.value(QStringLiteral("selectedColor"), 2).toInt();
    m_savedPreset = settings.value(QStringLiteral("selectedThickness"), 0).toInt();
    settings.endGroup();
    loadPresets();

    m_colors->reload();
    m_colors->setSelectedIndex(m_savedColorIndex, false);
    selectPreset(m_savedPreset);
    updatePresetButtons();
}

void PenSubToolbar::loadPresets()
{
    QSettings settings;
    const QStringList ids = settings.value(kGroup + QStringLiteral("/multiPen"), false).toBool()
                                ? multiPenIds(m_penType) : QStringList();
    const QString current = settings.value(multiPenKey(m_penType, QStringLiteral("current"))).toString();
    m_multiPenId = ids.isEmpty() ? QString() : ids.contains(current) ? current : ids.first();
    m_multiPenButton->setVisible(!ids.isEmpty());
    m_multiPenButton->setToolTip(tr("Next pen (now: %1)")
        .arg(settings.value(presetGroup() + QStringLiteral("/name")).toString()));

    // The usual presets first: a pen that has no sizes of its own yet starts from them.
    for (const QString& group : {kGroup, presetGroup()}) {
        settings.beginGroup(group);
        for (int i = 0; i < NUM_PRESETS; ++i) {
            const QString n = QString::number(i + 1);
            const StrokePreset d = group == kGroup ? defaultPreset(i) : m_presets[i];
            StrokePreset& p = m_presets[i];
            p.width = settings.value(QStringLiteral("thickness") + n, d.width).toReal();
            // Clamp so a minimum can't exceed a later-reduced width.
            p.minWidth = qBound(0.0, settings.value(QStringLiteral("minWidth") + n, d.minWidth).toReal(), p.width);
            p.lineStyle = quint8(qBound(0, settings.value(QStringLiteral("lineStyle") + n, int(d.lineStyle)).toInt(), 2));
            p.arrowStart = quint8(qBound(0, settings.value(QStringLiteral("arrowStart") + n, int(d.arrowStart)).toInt(), 2));
            p.arrowEnd = quint8(qBound(0, settings.value(QStringLiteral("arrowEnd") + n, int(d.arrowEnd)).toInt(), 2));
            p.rounded = settings.value(QStringLiteral("rounded") + n, d.rounded).toBool();
        }
        settings.endGroup();
    }
}

void PenSubToolbar::saveToSettings()
{
    QSettings settings;
    settings.beginGroup(presetGroup());
    for (int i = 0; i < NUM_PRESETS; ++i) {
        const QString n = QString::number(i + 1);
        const StrokePreset& p = m_presets[i];
        settings.setValue(QStringLiteral("thickness") + n, p.width);
        settings.setValue(QStringLiteral("minWidth") + n, p.minWidth);
        settings.setValue(QStringLiteral("lineStyle") + n, int(p.lineStyle));
        settings.setValue(QStringLiteral("arrowStart") + n, int(p.arrowStart));
        settings.setValue(QStringLiteral("arrowEnd") + n, int(p.arrowEnd));
        settings.setValue(QStringLiteral("rounded") + n, p.rounded);
    }
    settings.endGroup();
    saveSelectionToSettings();
}

void PenSubToolbar::saveSelectionToSettings()
{
    // Cheap enough for every click, and skipped when nothing changed, so
    // spam-clicking a preset does not write QSettings.
    const int color = m_colors->selectedIndex();
    if (color == m_savedColorIndex && m_selectedPreset == m_savedPreset) return;
    QSettings settings;
    settings.beginGroup(kGroup);
    settings.setValue(QStringLiteral("selectedColor"), color);
    settings.setValue(QStringLiteral("selectedThickness"), m_selectedPreset);
    settings.endGroup();
    m_savedColorIndex = color;
    m_savedPreset = m_selectedPreset;
}

void PenSubToolbar::refreshFromSettings()
{
    loadFromSettings();
    emitPreset();   // The pen in use can have other sizes now (multi-pen)
}

void PenSubToolbar::emitCurrentValues()
{
    emit penColorChanged(currentColor());
    emitPreset();
}

void PenSubToolbar::emitPreset()
{
    emit penThicknessChanged(currentThickness());
    emit penMinStrokeWidthChanged(currentMinStrokeWidth());
    emit penStyleChanged(currentStyle());
}

QColor PenSubToolbar::currentColor() const
{
    return m_colors->currentColor();
}

qreal PenSubToolbar::currentThickness() const
{
    return m_presets[m_selectedPreset].width;
}

qreal PenSubToolbar::currentMinStrokeWidth() const
{
    return m_presets[m_selectedPreset].minWidth;
}

StrokeStyle::Style PenSubToolbar::currentStyle() const
{
    const StrokePreset& p = m_presets[m_selectedPreset];
    StrokeStyle::Style style;
    style.penType = m_penType;
    style.lineStyle = p.lineStyle;
    style.arrowStart = p.arrowStart;
    style.arrowEnd = p.arrowEnd;
    style.rounded = p.rounded;
    return style;
}

void PenSubToolbar::setPenType(quint8 penType)
{
    penType = qMin(penType, quint8(StrokeStyle::Brush));
    if (penType == m_penType) return;
    m_penType = penType;
    QSettings().setValue(kGroup + QStringLiteral("/penType"), int(penType));
    loadPresets();   // With multi-pen, each pen type has its own pens
    updatePresetButtons();
    emitPreset();
}

void PenSubToolbar::restoreTabState(int tabIndex)
{
    const TabState state = m_tabStates.value(tabIndex);
    if (!state.initialized) return;  // No saved state for this tab: keep the global values
    m_colors->setSelectedIndex(state.colorIndex, false);
    selectPreset(state.presetIndex);
    updatePresetButtons();
}

void PenSubToolbar::saveTabState(int tabIndex)
{
    TabState& state = m_tabStates[tabIndex];
    state.colorIndex = m_colors->selectedIndex();
    state.presetIndex = m_selectedPreset;
    state.initialized = true;
}

void PenSubToolbar::clearTabState(int tabIndex)
{
    m_tabStates.remove(tabIndex);
}

QList<QColor> PenSubToolbar::colors() const
{
    return m_colors->colors();
}

QColor PenSubToolbar::presetColor(int index) const
{
    return m_colors->colors().value(index);
}

int PenSubToolbar::selectedColorPreset() const
{
    return m_colors->selectedIndex();
}

void PenSubToolbar::pickColorPreset(int index)
{
    m_colors->setSelectedIndex(index, true);
}

void PenSubToolbar::cycleColor()
{
    m_colors->cycle();
}

void PenSubToolbar::cycleThickness()
{
    onPresetClicked((m_selectedPreset + 1) % NUM_PRESETS);
}

void PenSubToolbar::onColorSelected()
{
    updatePresetButtons();
    emit penColorChanged(currentColor());
    saveSelectionToSettings();
}

void PenSubToolbar::onPresetClicked(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    // Always apply: the viewport's width could have been changed some other way.
    selectPreset(index);
    emitPreset();
    saveSelectionToSettings();
}

void PenSubToolbar::onPresetEditRequested(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    StrokePresetDialog dialog(m_presets[index], defaultPreset(index), 0.5, 50.0, this);
    dialog.setWindowTitle(tr("Pen stroke settings"));
    if (dialog.exec() != QDialog::Accepted) return;
    StrokePreset preset = dialog.preset();
    preset.minWidth = qBound(0.0, preset.minWidth, preset.width);
    applyPreset(index, preset);
}

void PenSubToolbar::applyPreset(int index, const StrokePreset& preset)
{
    if (preset == m_presets[index]) return;
    m_presets[index] = preset;
    updatePresetButtons();
    saveToSettings();
    if (index == m_selectedPreset) emitPreset();
}

void PenSubToolbar::selectPreset(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    m_selectedPreset = index;
    for (int i = 0; i < NUM_PRESETS; ++i) m_presetButtons[i]->setSelected(i == index);
}

void PenSubToolbar::updatePresetButtons()
{
    // The presets show the selected colour.
    const QColor color = currentColor();
    for (int i = 0; i < NUM_PRESETS; ++i) {
        const StrokePreset& p = m_presets[i];
        m_presetButtons[i]->setThickness(p.width);
        m_presetButtons[i]->setLineStyle(p.lineStyle);
        m_presetButtons[i]->setArrows(p.arrowStart, p.arrowEnd);
        m_presetButtons[i]->setLineColor(color);
    }
}
