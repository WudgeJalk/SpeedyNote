#pragma once

// ============================================================================
// QuickBar - tools and colours, opened where the pen (or the mouse) is
// ============================================================================
// Opened by the "view.quick_bar" command, so it works from a pen button, a key
// or anything else that can run a command. It closes when you pick something
// or press anywhere outside it.
//
// With a pen: hold the side button, move over an item, release = pick.
// ============================================================================

#include <QFrame>
#include <QList>
#include <QColor>
#include <QTimer>

class QToolButton;
class ToolbarButton;

class QuickBar : public QFrame {
    Q_OBJECT

public:
    explicit QuickBar(QWidget* parent);

    /**
     * @brief Show the bar just above @p tip (parent coordinates), kept inside the parent.
     * @param colors        the pen colours to offer
     * @param selectedColor index of the active one, or -1
     */
    void popupAt(const QPoint& tip, const QList<QColor>& colors, int selectedColor, bool darkMode);

signals:
    void commandPicked(const QString& actionId);  ///< A ShortcutManager action id
    void colorPicked(int index);
    void straightLinePicked();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void applyTheme(bool darkMode);

    QList<ToolbarButton*> m_buttons;
    QList<QToolButton*> m_colorButtons;
    QTimer m_releaseTimer;   ///< Watches for the side button release (pick on release)
    int m_themedDark = -1;   ///< Theme the styles were built for; -1 = not yet
};
