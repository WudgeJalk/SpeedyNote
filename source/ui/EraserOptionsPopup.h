#pragma once

#include <QFrame>

/**
 * @brief The eraser window: its options button (or a second tap on the eraser
 *        when Settings > Tools does not let that tap hide the eraser bar).
 *
 * Holds the eraser filter (one switch per tool), auto deselect and
 * "Clear the page". A switch is saved at once and DrawingSettings reloaded,
 * so nothing on the pen path reads QSettings. Deletes itself when it closes.
 */
class EraserOptionsPopup : public QFrame {
    Q_OBJECT

public:
    explicit EraserOptionsPopup(QWidget* parent = nullptr);

signals:
    /// "Clear the page" was pressed (the window closes itself).
    void clearPageRequested();
};
