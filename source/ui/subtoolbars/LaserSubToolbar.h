#ifndef LASERSUBTOOLBAR_H
#define LASERSUBTOOLBAR_H

#include "SubToolbar.h"

class IconPresetButton;

/**
 * @brief Subtoolbar for the laser pointer: Point (a dot) or Line (a trail).
 *
 * The choice is saved as "laser/mode" and read through DrawingSettings, so
 * nothing needs wiring. Colour and fade times are in Settings > Drawing.
 */
class LaserSubToolbar : public SubToolbar {
    Q_OBJECT

public:
    explicit LaserSubToolbar(QWidget* parent = nullptr);

    void refreshFromSettings() override;
    void restoreTabState(int) override {}
    void saveTabState(int) override {}
    void setDarkMode(bool darkMode) override;

    /// A mode's picture (0 point, 1 line), drawn in code.
    static QIcon modeIcon(int mode, bool dark);

private:
    void select(int mode);

    IconPresetButton* m_modeButtons[2] = {};
};

#endif // LASERSUBTOOLBAR_H
