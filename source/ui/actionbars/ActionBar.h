#ifndef ACTIONBAR_H
#define ACTIONBAR_H

#include <QWidget>
#include <QVBoxLayout>

class QFrame;
class ActionBarButton;

/**
 * @brief Abstract base class for all action bars.
 * 
 * Action bars provide context-sensitive editing operations and float on
 * the right side of the DocumentViewport, vertically centered.
 * 
 * Unlike subtoolbars which persist settings, action bars are purely reactive:
 * they appear/disappear based on selection state and tool context.
 * 
 * Subclasses must implement:
 * - updateButtonStates(): Update button visibility based on current state
 * 
 * Styling:
 * - Fixed width: ~44px (36 button + 8 padding)
 * - Rounded corners (8px radius)
 * - Shadow/border for depth
 * - Theme-aware background color
 * - Same visual style as SubToolbar (symmetrical appearance)
 */
class ActionBar : public QWidget {
    Q_OBJECT

public:
    explicit ActionBar(QWidget* parent = nullptr);
    virtual ~ActionBar() = default;
    
    /**
     * @brief Update button visibility based on current state.
     * 
     * Called when selection state, clipboard state, or other relevant
     * context changes. Subclasses should show/hide buttons as appropriate.
     */
    virtual void updateButtonStates() = 0;
    
    /**
     * @brief Set dark mode and update styling.
     * @param darkMode True for dark mode, false for light mode.
     * 
     * Subclasses should override this to propagate dark mode to their buttons.
     */
    virtual void setDarkMode(bool darkMode);

    /// Vertical: the column at the side of the window. Horizontal: the pill
    /// next to the selection.
    void setOrientation(Qt::Orientation orientation);

    /// The shape buttons ("Edit points" and the corner roundness), for bars that
    /// added them. @p canEditPoints false hides both; @p fraction < 0 hides the
    /// roundness, else it is where its slider opens (0 sharp .. 1 round).
    void setShapeEdit(bool canEditPoints, bool editPointsOn, qreal fraction);

signals:
    /// "Edit points" turned on or off.
    void editPointsToggled(bool on);
    /// The roundness slider: pressed, moved (0 sharp .. 1 round), released.
    void cornerRadiusBegin();
    void cornerRadiusPreview(qreal fraction);
    void cornerRadiusCommit();

protected:
    /**
     * @brief Add a button widget to the action bar layout.
     * @param button The button widget to add.
     */
    void addButton(QWidget* button);
    
    /// Add the two shape buttons here, hidden until setShapeEdit() shows them.
    void addShapeEditButtons();

    /**
     * @brief Add a horizontal separator line between button groups.
     */
    void addSeparator();
    
    /**
     * @brief Add a stretch to push remaining widgets up.
     */
    void addStretch();
    
    /**
     * @brief Apply shared styling (background, border, shadow).
     * 
     * Called automatically in constructor. Can be called again when
     * theme changes.
     */
    virtual void setupStyle();
    
    /**
     * @brief Check if the application is in dark mode.
     */
    bool isDarkMode() const;
    
    /**
     * @brief The main vertical layout for button arrangement.
     */
    QVBoxLayout* m_layout = nullptr;
    
    bool m_darkMode = false;
    
    /// Fixed width for all action bars (same as subtoolbar)
    static constexpr int ACTIONBAR_WIDTH = 44;
    
    /// Padding around buttons
    static constexpr int PADDING = 4;
    
    /// Border radius for rounded corners
    static constexpr int BORDER_RADIUS = 8;

private:
    ActionBarButton* m_editPointsButton = nullptr;
    ActionBarButton* m_cornersButton = nullptr;
    qreal m_cornerFraction = -1.0;
};

#endif // ACTIONBAR_H

