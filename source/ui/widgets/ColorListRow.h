#ifndef COLORLISTROW_H
#define COLORLISTROW_H

#include <QColor>
#include <QList>
#include <QWidget>
#include <climits>

class ColorPresetButton;
class QBoxLayout;
class QScrollArea;
class QToolButton;

/**
 * @brief A tool's colours: any number of swatches, then an "add" button.
 *
 * The list is saved as "<group>/colors" (#AARRGGBB strings), written only on
 * an edit. Until then it is read from the old "<group>/color1..3" keys, then
 * the defaults, so a user who changes nothing sees the same first colours.
 * The owner keeps and saves the selected index.
 *
 * Tap a swatch to pick it; tap the picked one again for a menu (change, move,
 * remove). The swatches scroll (drag or wheel) past "toolbar/visibleColors".
 */
class ColorListRow : public QWidget {
    Q_OBJECT

public:
    ColorListRow(const QString& group, const QList<QColor>& defaults, QWidget* parent = nullptr);

    QList<QColor> colors() const { return m_colors; }
    int selectedIndex() const { return m_selected; }
    QColor currentColor() const { return m_colors.value(m_selected); }
    /// Pick @p index (clamped). With @p emitSignal, colorSelected fires even if
    /// it was already picked: a tap always applies the colour.
    void setSelectedIndex(int index, bool emitSignal);
    void cycle();
    /// Read the list and "toolbar/visibleColors" again (the marker and
    /// highlighter share their list; the settings change the count).
    void reload();
    void setOrientation(Qt::Orientation orientation);
    void setDarkMode(bool dark);
    /// Take at most @p px along the row, "add" button included, by showing
    /// fewer swatches (at least one); the rest scroll. For a bar with little room.
    void fitTo(int px);

    // Edits: each one saves the list and emits listChanged.
    void addColor(const QColor& color);   ///< Appends and picks it
    void setColorAt(int index, const QColor& color);
    void moveColor(int from, int to);     ///< The picked colour stays picked
    void removeColor(int index);          ///< Never removes the last colour

signals:
    void colorSelected(const QColor& color);
    void listChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QList<QColor> load() const;
    void save() const;
    static int readVisibleCount();
    void sync();        ///< Swatches follow m_colors and m_selected
    void showMenu(int index);

    QString m_group;
    QList<QColor> m_defaults;
    QList<QColor> m_colors;
    int m_selected = 0;
    int m_visible = 8;
    int m_room = INT_MAX;   ///< Length the swatches may take (fitTo)
    bool m_vertical = false;

    QBoxLayout* m_layout = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_inner = nullptr;
    QBoxLayout* m_innerLayout = nullptr;
    QToolButton* m_add = nullptr;
    QList<ColorPresetButton*> m_swatches;
};

#endif // COLORLISTROW_H
