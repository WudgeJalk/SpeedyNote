#ifndef PENSUBTOOLBAR_H
#define PENSUBTOOLBAR_H

#include "SubToolbar.h"
#include "../StrokePresetDialog.h"
#include "../../strokes/StrokeStyle.h"
#include <QColor>
#include <QHash>
#include <QList>
#include <QStringList>

class ColorListRow;
class QLabel;
class QTimer;
class IconPresetButton;
class ThicknessPresetButton;

/**
 * @brief Subtoolbar for the Pen tool.
 *
 * Layout: [pen window button] 3 stroke presets | separator | the pen's colours
 * (any number). The pen window button is there with one pen icon only.
 *
 * A stroke preset is a width AND a line style (solid, dashed, dotted, arrow
 * heads, rounded corners). Colour, width, pen type and line style are
 * independent: changing one never changes another.
 *
 * - Click unselected preset or colour → select and apply
 * - Click selected preset → stroke preset window; selected colour → its menu
 * - Per-tab state: the selected colour and preset
 * - Global persistence via QSettings ("pen/...")
 */
class PenSubToolbar : public SubToolbar {
    Q_OBJECT

    friend class ToolbarButtonTests;

public:
    explicit PenSubToolbar(QWidget* parent = nullptr);

    // SubToolbar interface
    void refreshFromSettings() override;
    void restoreTabState(int tabIndex) override;
    void saveTabState(int tabIndex) override;
    void clearTabState(int tabIndex) override;

    /**
     * @brief Emit the current colour, width, minimum width and style.
     *
     * Call this when connecting to a new viewport to sync it with the
     * subtoolbar's current selection.
     */
    void emitCurrentValues();

    QColor currentColor() const;
    qreal currentThickness() const;
    /// The selected preset's minimum stroke width (page units).
    qreal currentMinStrokeWidth() const;
    /// Pen type (from the pen window) plus the selected preset's line style.
    StrokeStyle::Style currentStyle() const;

    /// Fountain, Ball or Brush. Saved; emits penStyleChanged.
    void setPenType(quint8 penType);

    /// Advance to the next colour (wraps), applying and persisting it.
    void cycleColor();

    /// Advance to the next stroke preset (wraps), applying and persisting it.
    void cycleThickness();

    // For the quick bar: the same colours, picked from somewhere else.
    QList<QColor> colors() const;
    QColor presetColor(int index) const;
    int selectedColorPreset() const;
    void pickColorPreset(int index);

    // Multi-pen: several named pens of one pen type (fountain, ball, brush),
    // each with its own three stroke presets - one pen per paper format, for
    // example. "pen/multiPen" turns it on. A pen is the settings group
    // "pen/multi<type>/<id>": its "name" and the same preset keys as "pen/".
    // A pen type without pens works as before.
    /// The pens of @p penType, oldest first.
    static QStringList multiPenIds(int penType);
    static QString multiPenKey(int penType, const QString& id);
    /// @return The id of the new pen.
    static QString addMultiPen(int penType, const QString& name);
    /// The next pen of the pen type in use (wraps). Shows its name for a moment.
    void cycleMultiPen();

    /// The button that opens the pen window: shown with one pen icon only.
    void setWindowButtonVisible(bool visible);
    void setDarkMode(bool darkMode) override;

signals:
    /// The pen window button was tapped.
    void windowRequested();
    void penColorChanged(const QColor& color);
    void penThicknessChanged(qreal thickness);
    /// Fires alongside penThicknessChanged when the preset or its minimum changes.
    void penMinStrokeWidthChanged(qreal minWidth);
    /// Pen type, line style or arrow heads changed.
    void penStyleChanged(const StrokeStyle::Style& style);

private:
    void loadFromSettings();
    /// The three presets of the pen in use (the multi-pen one, else the usual ones).
    void loadPresets();
    QString presetGroup() const;
    void showMultiPenName(const QString& name);
    void saveToSettings();
    /// Write only the two selection indexes, and only when they changed.
    void saveSelectionToSettings();
    void onColorSelected();
    void onPresetClicked(int index);
    void onPresetEditRequested(int index);
    /// Store @p preset in slot @p index, show it, save, and apply it if selected.
    void applyPreset(int index, const StrokePreset& preset);
    void selectPreset(int index);
    void updatePresetButtons();
    void emitPreset();
    StrokePreset defaultPreset(int index) const;

    // Widgets
    IconPresetButton* m_windowButton = nullptr;
    IconPresetButton* m_multiPenButton = nullptr;
    QLabel* m_multiPenLabel = nullptr;   // The name shown for a moment, made on first use
    QTimer* m_multiPenTimer = nullptr;
    QString m_multiPenId;                // Empty: the usual presets
    ThicknessPresetButton* m_presetButtons[3] = {nullptr, nullptr, nullptr};
    ColorListRow* m_colors = nullptr;

    // Current state
    StrokePreset m_presets[3];
    int m_selectedPreset = 0;   // Default: thin (index 0)
    quint8 m_penType = StrokeStyle::Fountain;
    qreal m_legacyMinWidth = 0.3;
    int m_savedColorIndex = -1;
    int m_savedPreset = -1;

    // Per-tab state: the colour list and presets are global, only the picks are per tab.
    struct TabState {
        int colorIndex = 0;
        int presetIndex = 0;
        bool initialized = false;
    };
    QHash<int, TabState> m_tabStates;

    static constexpr int NUM_PRESETS = 3;
    static constexpr qreal DEFAULT_THICKNESSES[NUM_PRESETS] = {2.0, 5.0, 10.0};
    static constexpr qreal DEFAULT_MIN_WIDTH = 0.3;
};

#endif // PENSUBTOOLBAR_H
