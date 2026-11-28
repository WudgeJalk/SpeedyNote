#ifndef NAVIGATIONBAR_H
#define NAVIGATIONBAR_H

#include <QWidget>
#include <QColor>
#include <QPushButton>
#include "ToolbarButtons.h"
#include "../core/AutoSaver.h"

class QLabel;
class QIcon;

/**
 * NavigationBar - Top bar for global/app-wide actions.
 * 
 * Layout:
 * ┌──────────────────────────────────────────────────────────────────────────┐
 * │ [←][📁][💾][+]          document_name.snb          [⛶][📤][📝][⋮]      │
 * └──────────────────────────────────────────────────────────────────────────┘
 *   Left side              Center (clickable)           Right side
 * 
 * Left: Launcher, Left Sidebar Toggle, Save, Add
 * Center: Filename (click to toggle tab bar)
 * Right: Fullscreen, Share, Right Sidebar Toggle, Menu
 */
class NavigationBar : public QWidget {
    Q_OBJECT

public:
    explicit NavigationBar(QWidget *parent = nullptr);
    
    /**
     * Set the displayed filename.
     * Long names are elided with "..." in the middle.
     */
    void setFilename(const QString &filename);
    
    /**
     * Update theme colors.
     * @param darkMode True for dark theme icons
     * @param accentColor Background color for the navigation bar
     */
    void updateTheme(bool darkMode, const QColor &accentColor);
    
    /**
     * Set left sidebar toggle state (for external sync).
     */
    void setLeftSidebarChecked(bool checked);
    
    /**
     * Set right sidebar toggle state (for external sync).
     */
    void setRightSidebarChecked(bool checked);
    
    /**
     * Set fullscreen toggle state (for external sync).
     */
    void setFullscreenChecked(bool checked);
    
    /**
     * Get the add button widget for positioning menus.
     * Phase P.4.3: Used to position the "New" dropdown menu.
     */
    QWidget* addButton() const { return m_addButton; }

    /**
     * Use this bar as the window's title row (Windows, see MainWindow).
     * Shows the app icon on the left, adds the app name to the title, and
     * keeps @p captionReserve px free on the right for minimise / maximise / close.
     */
    void setTitleBarMode(const QIcon &appIcon, int captionReserve);
    /// True when @p pos (our coordinates) is on one of our buttons (minimise /
    /// maximise / close too). Windows must not treat that point as title bar.
    bool hasOwnButtonAt(const QPoint &pos) const;
    /// Bottom right corner of the menu button, in screen coordinates.
    QPoint menuAnchor() const;
    /// Swap the maximise button between "maximise" and "restore".
    void setWindowMaximized(bool maximized);

    /// The small autosave sign next to the name (Step R1): a check, a loader,
    /// a red alert, or nothing (Off). @p tip is its tooltip.
    void setSaveState(AutoSaver::State state, const QString &tip);

signals:
    void searchClicked();  // Find in the open document
    void minimizeClicked();
    void maximizeClicked();
    void closeClicked();
    // Left side
    void launcherClicked();
    void leftSidebarToggled(bool checked);
    void saveClicked();
    void addClicked();  // Stubbed - will show menu in future
    
    // Center
    void filenameClicked();  // Toggle tab container visibility
    void saveStateClicked(); // The autosave sign
    
    // Right side
    void fullscreenToggled(bool checked);
    void shareClicked();  // Opens the combined PDF/package export dialog
    void rightSidebarToggled(bool checked);  // Markdown notes
    void menuRequested();

private:
    void setupUi();
    void connectSignals();
    QString elideFilename(const QString &filename, int maxWidth);
    
    // Left buttons
    ActionButton *m_launcherButton;
    ToggleButton *m_leftSidebarButton;
    ActionButton *m_saveButton;
    ActionButton *m_addButton;
    ActionButton *m_searchButton;
    QLabel *m_appIconLabel;          // Shown in title bar mode only
    QWidget *m_captionSpacer;        // Room for system window buttons, if any are kept
    QWidget *m_windowButtons;        // Holds the three window buttons, no gaps
    ActionButton *m_minimizeButton;  // Our own window buttons (title bar mode)
    ActionButton *m_maximizeButton;
    ActionButton *m_closeButton;
    bool m_titleBarMode = false;

    // Center
    QPushButton *m_filenameButton;
    QString m_fullFilename;  // Store full name for tooltip
    ActionButton *m_saveStateButton;
    AutoSaver::State m_saveState = AutoSaver::Off;
    void applySaveStateIcon();
    
    // Right buttons
    ToggleButton *m_fullscreenButton;
    ActionButton *m_shareButton;
    ToggleButton *m_rightSidebarButton;
    ActionButton *m_menuButton;
    
    // State
    bool m_darkMode = false;
    QColor m_accentColor = QColor(0x31, 0x68, 0x82);  // #316882 default
};

#endif // NAVIGATIONBAR_H

