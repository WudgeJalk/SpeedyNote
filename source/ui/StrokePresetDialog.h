#ifndef STROKEPRESETDIALOG_H
#define STROKEPRESETDIALOG_H

#include "widgets/ThicknessPresetButton.h"  // ThicknessEditDialog

class QButtonGroup;
class QCheckBox;
class QComboBox;

/// One stroke preset of the pen (or the shapes): a width AND a line style.
struct StrokePreset {
    qreal width = 2.0;      ///< Page units
    qreal minWidth = 0.3;   ///< Page units; negative = the tool has no minimum width
    quint8 lineStyle = 0;   ///< StrokeStyle::LineStyle
    quint8 arrowStart = 0;  ///< StrokeStyle::Arrow
    quint8 arrowEnd = 0;
    bool rounded = true;

    bool operator==(const StrokePreset& o) const {
        return qFuzzyCompare(width, o.width) && qFuzzyCompare(minWidth + 1.0, o.minWidth + 1.0)
            && lineStyle == o.lineStyle && arrowStart == o.arrowStart
            && arrowEnd == o.arrowEnd && rounded == o.rounded;
    }
    bool operator!=(const StrokePreset& o) const { return !(*this == o); }
};

/**
 * @brief The stroke preset window: line style, width (slider and box, in the
 * user's unit), minimum width, rounded corners, arrow heads, and Reset.
 */
class StrokePresetDialog : public ThicknessEditDialog {
    Q_OBJECT

public:
    /// @p defaults is what Reset brings back.
    StrokePresetDialog(const StrokePreset& current, const StrokePreset& defaults,
                       qreal minWidthLimit, qreal maxWidthLimit, QWidget* parent = nullptr);

    StrokePreset preset() const;

    /// How a hand-drawn head is read, in one line (this window and Settings > Drawing).
    static QString handArrowHint()
    {
        return tr("To draw an arrow by hand, draw a line, then at its end a V for an open head "
                  "or a closed triangle for a filled head.");
    }

private:
    void showPreset(const StrokePreset& preset);

    StrokePreset m_defaults;
    QButtonGroup* m_lineStyle = nullptr;
    QCheckBox* m_rounded = nullptr;
    QComboBox* m_arrowStart = nullptr;
    QComboBox* m_arrowEnd = nullptr;
};

#endif // STROKEPRESETDIALOG_H
