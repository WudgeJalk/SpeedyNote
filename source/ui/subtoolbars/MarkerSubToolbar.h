#ifndef MARKERSUBTOOLBAR_H
#define MARKERSUBTOOLBAR_H

#include "SubToolbar.h"
#include <QColor>
#include <QHash>

class ColorListRow;
class ThicknessPresetButton;

/**
 * @brief Subtoolbar for the Marker tool.
 *
 * Layout: 3 width presets | separator | the colours (any number).
 *
 * Key difference from PenSubToolbar:
 * - The colour list is SHARED with HighlighterSubToolbar ("marker/colors")
 * - Width-only presets, marker-specific (8.0, 16.0, 32.0 defaults)
 *
 * Features:
 * - Click unselected preset → select and apply
 * - Click selected preset → open editor dialog; selected colour → its menu
 * - Per-tab state: the selected colour and preset
 * - Global persistence via QSettings
 */
class MarkerSubToolbar : public SubToolbar {
    Q_OBJECT

public:
    explicit MarkerSubToolbar(QWidget* parent = nullptr);

    // SubToolbar interface
    void refreshFromSettings() override;
    void restoreTabState(int tabIndex) override;
    void saveTabState(int tabIndex) override;
    void clearTabState(int tabIndex) override;

    /// Reload the shared colour list (the Highlighter may have changed it).
    void syncSharedState() override;

    /**
     * @brief Emit the currently selected preset values.
     *
     * Call this when connecting to a new viewport to sync its
     * color/thickness with the subtoolbar's current selection.
     */
    void emitCurrentValues();

    /**
     * @brief Get the currently selected marker color (with MARKER_OPACITY applied).
     */
    QColor currentColor() const;

    /**
     * @brief Get the currently selected marker thickness.
     */
    qreal currentThickness() const;

    /// Advance to the next colour (wraps), applying and persisting it.
    void cycleColor();

    /// Advance to the next thickness preset (wraps), applying and persisting it.
    void cycleThickness();

signals:
    void markerColorChanged(const QColor& color);
    void markerThicknessChanged(qreal thickness);

private:
    void loadFromSettings();
    void saveThicknessesToSettings();
    /// Write only the two selection indexes, and only when they changed.
    void saveSelectionToSettings();
    void onColorSelected();
    void onThicknessPresetClicked(int index);
    void onThicknessEditRequested(int index);
    void selectThicknessPreset(int index);
    void updateThicknessPreviewColors();

    // Widgets
    ThicknessPresetButton* m_thicknessButtons[3] = {nullptr, nullptr, nullptr};
    ColorListRow* m_colors = nullptr;

    // Current state
    int m_selectedThicknessIndex = 0;  // Default: thin
    int m_savedColorIndex = -1;
    int m_savedThicknessIndex = -1;

    // Per-tab state: the colour list and presets are global, only the picks are per tab.
    struct TabState {
        int colorIndex = 0;
        int thicknessIndex = 0;
        bool initialized = false;
    };
    QHash<int, TabState> m_tabStates;

    // Default values
    static constexpr int NUM_PRESETS = 3;
    static constexpr qreal DEFAULT_THICKNESSES[NUM_PRESETS] = {8.0, 16.0, 32.0};

    // Marker opacity (50% = 128/255)
    // This is applied when emitting markerColorChanged to maintain consistency
    static constexpr int MARKER_OPACITY = 128;
};

#endif // MARKERSUBTOOLBAR_H
