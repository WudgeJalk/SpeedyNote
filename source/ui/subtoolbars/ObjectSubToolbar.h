#ifndef OBJECTSUBTOOLBAR_H
#define OBJECTSUBTOOLBAR_H

#include "SubToolbar.h"

class IconPresetButton;

/**
 * @brief Options of the picture / object tool, under the toolbar.
 *
 * The user, 2026-11-23: these were a small bar at the side of the page; the
 * options of a tool belong under the toolbar. Select (tap a picture to choose
 * it), Add (tap the page to put a picture there), Paste. MainWindow wires the
 * signals to the viewport, the same way as the object action bar.
 */
class ObjectSubToolbar : public SubToolbar {
    Q_OBJECT

public:
    explicit ObjectSubToolbar(QWidget* parent = nullptr);

    void refreshFromSettings() override {}
    void restoreTabState(int) override {}
    void saveTabState(int) override {}
    void setDarkMode(bool darkMode) override;

    /// Shows which mode the viewport is in.
    void setCreateMode(bool create);

signals:
    void createModeRequested(bool create);
    void pasteRequested();

private:
    IconPresetButton* m_selectButton = nullptr;
    IconPresetButton* m_addButton = nullptr;
    IconPresetButton* m_pasteButton = nullptr;
};

#endif // OBJECTSUBTOOLBAR_H
