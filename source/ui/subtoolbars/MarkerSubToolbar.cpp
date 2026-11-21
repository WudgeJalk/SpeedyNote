#include "MarkerSubToolbar.h"
#include "../widgets/ColorListRow.h"
#include "../widgets/ThicknessPresetButton.h"

#include <QSettings>

namespace {
// Default marker colors, shared with the Highlighter ("marker/colors").
const QList<QColor> kColors = {
    QColor(0xFF, 0xAA, 0xAA),  // Light red/pink
    QColor(0xFF, 0xFF, 0x00),  // Yellow
    QColor(0xAA, 0xAA, 0xFF)   // Light blue
};
const QString kGroup = QStringLiteral("marker");
}

MarkerSubToolbar::MarkerSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_thicknessButtons[i] = new ThicknessPresetButton(this);
        m_thicknessButtons[i]->setThickness(DEFAULT_THICKNESSES[i]);
        m_thicknessButtons[i]->setToolTip(tr("Thickness preset %1 (click to select, click again to edit)").arg(i + 1));
        addWidget(m_thicknessButtons[i]);
        connect(m_thicknessButtons[i], &ThicknessPresetButton::clicked, this, [this, i]() {
            onThicknessPresetClicked(i);
        });
        connect(m_thicknessButtons[i], &ThicknessPresetButton::editRequested, this, [this, i]() {
            onThicknessEditRequested(i);
        });
    }

    addSeparator();

    m_colors = new ColorListRow(kGroup, kColors, this);
    addWidget(m_colors);
    connect(m_colors, &ColorListRow::colorSelected, this, &MarkerSubToolbar::onColorSelected);
    connect(m_colors, &ColorListRow::listChanged, this, &MarkerSubToolbar::saveSelectionToSettings);

    loadFromSettings();
}

void MarkerSubToolbar::loadFromSettings()
{
    QSettings settings;
    settings.beginGroup(kGroup);
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = QStringLiteral("thickness") + QString::number(i + 1);
        m_thicknessButtons[i]->setThickness(settings.value(key, DEFAULT_THICKNESSES[i]).toReal());
    }
    m_savedColorIndex = settings.value(QStringLiteral("selectedColor"), 0).toInt();
    m_savedThicknessIndex = settings.value(QStringLiteral("selectedThickness"), 0).toInt();
    settings.endGroup();

    m_colors->reload();
    m_colors->setSelectedIndex(m_savedColorIndex, false);
    selectThicknessPreset(m_savedThicknessIndex);
    updateThicknessPreviewColors();
}

void MarkerSubToolbar::saveThicknessesToSettings()
{
    // Marker-specific width presets only; the selection is saved on each click.
    QSettings settings;
    settings.beginGroup(kGroup);
    for (int i = 0; i < NUM_PRESETS; ++i) {
        QString key = QStringLiteral("thickness") + QString::number(i + 1);
        settings.setValue(key, m_thicknessButtons[i]->thickness());
    }
    settings.endGroup();
}

void MarkerSubToolbar::saveSelectionToSettings()
{
    // Skipped when nothing changed, so spam-clicking does not write QSettings.
    const int color = m_colors->selectedIndex();
    if (color == m_savedColorIndex && m_selectedThicknessIndex == m_savedThicknessIndex) return;
    QSettings settings;
    settings.beginGroup(kGroup);
    settings.setValue(QStringLiteral("selectedColor"), color);
    settings.setValue(QStringLiteral("selectedThickness"), m_selectedThicknessIndex);
    settings.endGroup();
    m_savedColorIndex = color;
    m_savedThicknessIndex = m_selectedThicknessIndex;
}

void MarkerSubToolbar::refreshFromSettings()
{
    loadFromSettings();
}

void MarkerSubToolbar::syncSharedState()
{
    // Only the shared list; the per-tab selection stays.
    m_colors->reload();
    updateThicknessPreviewColors();
}

void MarkerSubToolbar::emitCurrentValues()
{
    emit markerColorChanged(currentColor());
    emit markerThicknessChanged(currentThickness());
}

QColor MarkerSubToolbar::currentColor() const
{
    QColor color = m_colors->currentColor();
    color.setAlpha(MARKER_OPACITY);
    return color;
}

qreal MarkerSubToolbar::currentThickness() const
{
    if (m_selectedThicknessIndex >= 0 && m_selectedThicknessIndex < NUM_PRESETS) {
        return m_thicknessButtons[m_selectedThicknessIndex]->thickness();
    }
    return DEFAULT_THICKNESSES[0];
}

void MarkerSubToolbar::restoreTabState(int tabIndex)
{
    const TabState state = m_tabStates.value(tabIndex);
    if (!state.initialized) return;  // No saved state for this tab: keep the global values
    m_colors->setSelectedIndex(state.colorIndex, false);
    selectThicknessPreset(state.thicknessIndex);
    updateThicknessPreviewColors();
}

void MarkerSubToolbar::saveTabState(int tabIndex)
{
    TabState& state = m_tabStates[tabIndex];
    state.colorIndex = m_colors->selectedIndex();
    state.thicknessIndex = m_selectedThicknessIndex;
    state.initialized = true;
}

void MarkerSubToolbar::clearTabState(int tabIndex)
{
    m_tabStates.remove(tabIndex);
}

void MarkerSubToolbar::cycleColor()
{
    m_colors->cycle();
}

void MarkerSubToolbar::cycleThickness()
{
    int start = qMax(0, m_selectedThicknessIndex);
    onThicknessPresetClicked((start + 1) % NUM_PRESETS);
}

void MarkerSubToolbar::onColorSelected()
{
    updateThicknessPreviewColors();
    emit markerColorChanged(currentColor());
    saveSelectionToSettings();
}

void MarkerSubToolbar::onThicknessPresetClicked(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    // Always apply the thickness when clicked
    selectThicknessPreset(index);
    emit markerThicknessChanged(m_thicknessButtons[index]->thickness());
    saveSelectionToSettings();
}

void MarkerSubToolbar::onThicknessEditRequested(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;

    ThicknessEditDialog dialog(m_thicknessButtons[index]->thickness(), 0.5, 50.0,
                               /*currentMinWidth=*/-1.0, this);
    dialog.setWindowTitle(tr("Edit Marker Thickness"));

    if (dialog.exec() == QDialog::Accepted) {
        qreal newThickness = dialog.thickness();
        m_thicknessButtons[index]->setThickness(newThickness);
        saveThicknessesToSettings();
        if (m_selectedThicknessIndex == index) {
            emit markerThicknessChanged(newThickness);
        }
    }
}

void MarkerSubToolbar::selectThicknessPreset(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_thicknessButtons[i]->setSelected(i == index);
    }
    m_selectedThicknessIndex = index;
}

void MarkerSubToolbar::updateThicknessPreviewColors()
{
    // The previews show the actual marker look, opacity included.
    const QColor previewColor = currentColor();
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_thicknessButtons[i]->setLineColor(previewColor);
    }
}
