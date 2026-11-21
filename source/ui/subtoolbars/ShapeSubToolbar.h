#ifndef SHAPESUBTOOLBAR_H
#define SHAPESUBTOOLBAR_H

#include "SubToolbar.h"
#include "../StrokePresetDialog.h"
#include "../../strokes/StrokeStyle.h"
#include <QColor>

class ColorListRow;
class IconPresetButton;
class ThicknessPresetButton;

/**
 * @brief Subtoolbar for the Shape tool.
 *
 * Layout: 8 kinds | stroke preset | the shape colours (any number).
 * Kinds: free shape, right-angle connector, curved connector, rectangle,
 * ellipse, triangle, diamond, rounded rectangle.
 *
 * Saved under "shapes/". A tap on the preset opens the stroke preset window
 * (width, solid / dashed / dotted, rounded corners, arrow heads).
 */
class ShapeSubToolbar : public SubToolbar {
    Q_OBJECT

public:
    enum Kind : int { Free = 0, Elbow, Curved, Rect, Ellipse, Triangle, Diamond, RoundRect, KindCount };

    explicit ShapeSubToolbar(QWidget* parent = nullptr);

    void refreshFromSettings() override;
    void restoreTabState(int) override {}
    void saveTabState(int) override {}
    void setDarkMode(bool darkMode) override;

    int currentKind() const { return m_kind; }
    QColor currentColor() const;
    qreal currentThickness() const { return m_preset.width; }
    StrokeStyle::Style currentStyle() const;

    void emitCurrentValues();
    void cycleColor();

    /// A kind's picture, drawn in code (also used in tests and menus).
    static QIcon kindIcon(int kind, bool dark);

signals:
    void shapeKindChanged(int kind);
    void shapeColorChanged(const QColor& color);
    /// Width, line style, arrow heads or rounded corners changed.
    void shapeStrokeChanged(qreal width, const StrokeStyle::Style& style);

private:
    void selectKind(int kind);
    void editPreset();
    void updateButtons();

    IconPresetButton* m_kindButtons[KindCount] = {};
    ThicknessPresetButton* m_presetButton = nullptr;
    ColorListRow* m_colors = nullptr;
    int m_kind = Free;
    StrokePreset m_preset;
};

#endif // SHAPESUBTOOLBAR_H
