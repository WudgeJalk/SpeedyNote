#pragma once

#include <QColor>
#include <QFrame>
#include <QPixmap>

/**
 * @brief The pen window: the button of the pen bar with one pen icon (or a
 *        second tap on the pen when Settings > Tools does not let that tap hide the pen bar).
 *
 * Holds the pen type (fountain, ball, brush) with a drawn preview of each,
 * "Stroke stabilisation", "Draw and hold" with its hold time, and a link to
 * the pen button settings. A setting is saved at once and DrawingSettings
 * reloaded, so nothing on the pen path reads QSettings. Deletes itself when
 * it closes.
 */
class PenSettingsPopup : public QFrame {
    Q_OBJECT

public:
    /// @p penType is the pen's current type.
    explicit PenSettingsPopup(quint8 penType, QWidget* parent = nullptr);

    /// An S-curve with rising pressure, drawn by the stroke renderer.
    static QPixmap penPreview(quint8 penType, const QColor& color, qreal dpr);

signals:
    void penTypeChosen(quint8 penType);
    /// "Pen button settings..." was pressed (the window closes itself).
    void penButtonSettingsRequested();
};
