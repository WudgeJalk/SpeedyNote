#include "NavigationBar.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include <QFontMetrics>
#include <QPalette>
#include <QDebug>

NavigationBar::NavigationBar(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    connectSignals();
    // Note: Don't call updateTheme() here - MainWindow::loadThemeSettings() 
    // will call it after loading user preferences to avoid double initialization
}

void NavigationBar::setupUi()
{
    // Fixed height for navigation bar
    setFixedHeight(44);
    
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);
    
    // App icon: only in title bar mode, where this bar replaces the title row.
    m_appIconLabel = new QLabel(this);
    m_appIconLabel->setFixedSize(28, 28);
    m_appIconLabel->setAlignment(Qt::AlignCenter);
    m_appIconLabel->hide();
    mainLayout->addWidget(m_appIconLabel);

    // === Left side buttons ===
    m_launcherButton = new ActionButton(this);
    m_launcherButton->setThemedIcon("home");  // Back to the launcher (the home page)
    m_launcherButton->setToolTip(tr("Home"));
    mainLayout->addWidget(m_launcherButton);
    
    m_leftSidebarButton = new ToggleButton(this);
    m_leftSidebarButton->setThemedIcon("leftsidebar");
    m_leftSidebarButton->setToolTip(tr("Toggle Left Sidebar"));
    mainLayout->addWidget(m_leftSidebarButton);
    
    m_saveButton = new ActionButton(this);
    m_saveButton->setThemedIcon("save");
    m_saveButton->setToolTip(tr("Save (Ctrl+S)"));
    mainLayout->addWidget(m_saveButton);
    
    m_addButton = new ActionButton(this);
    m_addButton->setThemedIcon("addtab");
    m_addButton->setToolTip(tr("New Document"));
    mainLayout->addWidget(m_addButton);

    m_searchButton = new ActionButton(this);
    m_searchButton->setThemedIcon("search");
    m_searchButton->setToolTip(tr("Find in Document (Ctrl+F)"));
    mainLayout->addWidget(m_searchButton);

    // === Center - Filename (with stretch on both sides) ===
    mainLayout->addStretch(1);
    
    m_filenameButton = new QPushButton(this);
    m_filenameButton->setText(tr("Untitled"));
    m_filenameButton->setFlat(true);
    m_filenameButton->setCursor(Qt::PointingHandCursor);
    m_filenameButton->setToolTip(tr("Click to toggle tab bar"));
    // Style will be set in updateTheme()
    mainLayout->addWidget(m_filenameButton);
    
    mainLayout->addStretch(1);
    
    // === Right side buttons ===
    m_fullscreenButton = new ToggleButton(this);
    m_fullscreenButton->setThemedIcon("fullscreen");
    m_fullscreenButton->setToolTip(tr("Toggle Fullscreen"));
    mainLayout->addWidget(m_fullscreenButton);
    
    m_shareButton = new ActionButton(this);
    m_shareButton->setThemedIcon("export");
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    m_shareButton->setToolTip(tr("Share Notebook"));
#else
    m_shareButton->setToolTip(tr("Export Notebook"));
#endif
    mainLayout->addWidget(m_shareButton);
    
    m_rightSidebarButton = new ToggleButton(this);
    m_rightSidebarButton->setThemedIcon("rightsidebar");
    m_rightSidebarButton->setToolTip(tr("Toggle Markdown Notes"));
    mainLayout->addWidget(m_rightSidebarButton);
    
    m_menuButton = new ActionButton(this);
    m_menuButton->setThemedIcon("menu");
    m_menuButton->setToolTip(tr("Menu"));
    mainLayout->addWidget(m_menuButton);

    m_captionSpacer = new QWidget(this);
    m_captionSpacer->setFixedWidth(0);
    mainLayout->addWidget(m_captionSpacer);

    // Our own minimise / maximise / close, shown in title bar mode only.
    // Each one is 46 px wide with no gap, flush with the right edge: that is
    // exactly where Windows/Qt keep the real button zones (1.5 x the 31 px
    // title height). A click in the top part is handled by Windows itself;
    // a click lower down, or a pen tap, reaches these buttons. Same result.
    m_windowButtons = new QWidget(this);
    m_windowButtons->hide();
    auto *windowLayout = new QHBoxLayout(m_windowButtons);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->setSpacing(0);
    mainLayout->addWidget(m_windowButtons);
    auto addWindowButton = [&](const char *icon, const QString &tip, void (NavigationBar::*signal)()) {
        auto *button = new ActionButton(m_windowButtons);
        button->setThemedIcon(QLatin1String(icon));
        button->setToolTip(tip);
        button->setFixedWidth(46);
        connect(button, &QPushButton::clicked, this, signal);
        windowLayout->addWidget(button);
        return button;
    };
    m_minimizeButton = addWindowButton("win_min", tr("Minimize"), &NavigationBar::minimizeClicked);
    m_maximizeButton = addWindowButton("win_max", tr("Maximize"), &NavigationBar::maximizeClicked);
    m_closeButton = addWindowButton("cross", tr("Close"), &NavigationBar::closeClicked);
}

void NavigationBar::setTitleBarMode(const QIcon &appIcon, int captionReserve)
{
    m_titleBarMode = true;
    m_appIconLabel->setPixmap(appIcon.pixmap(22, 22));
    m_appIconLabel->show();
    m_captionSpacer->setFixedWidth(captionReserve);
    m_windowButtons->show();
    // Flush with the right edge, so the icons sit on the real button zones.
    QMargins margins = layout()->contentsMargins();
    margins.setRight(0);
    layout()->setContentsMargins(margins);
    setFilename(m_fullFilename);
}

bool NavigationBar::hasOwnButtonAt(const QPoint &pos) const
{
    const QWidget *child = childAt(pos);
    return child && qobject_cast<const QAbstractButton *>(child) && !m_windowButtons->isAncestorOf(child);
}

QPoint NavigationBar::menuAnchor() const
{
    return m_menuButton->mapToGlobal(QPoint(m_menuButton->width(), m_menuButton->height()));
}

void NavigationBar::setWindowMaximized(bool maximized)
{
    m_maximizeButton->setThemedIcon(maximized ? QStringLiteral("win_restore") : QStringLiteral("win_max"));
    m_maximizeButton->setToolTip(maximized ? tr("Restore") : tr("Maximize"));
}

void NavigationBar::connectSignals()
{
    // Left side
    connect(m_launcherButton, &QPushButton::clicked, 
            this, &NavigationBar::launcherClicked);
    connect(m_leftSidebarButton, &QPushButton::toggled,
            this, &NavigationBar::leftSidebarToggled);
    connect(m_saveButton, &QPushButton::clicked,
            this, &NavigationBar::saveClicked);
    connect(m_addButton, &QPushButton::clicked,
            this, &NavigationBar::addClicked);
    connect(m_searchButton, &QPushButton::clicked,
            this, &NavigationBar::searchClicked);
    
    // Center
    connect(m_filenameButton, &QPushButton::clicked,
            this, &NavigationBar::filenameClicked);
    
    // Right side
    connect(m_fullscreenButton, &QPushButton::toggled,
            this, &NavigationBar::fullscreenToggled);
    connect(m_shareButton, &QPushButton::clicked,
            this, &NavigationBar::shareClicked);
    connect(m_rightSidebarButton, &QPushButton::toggled,
            this, &NavigationBar::rightSidebarToggled);
    connect(m_menuButton, &QPushButton::clicked,
            this, &NavigationBar::menuRequested);
}

void NavigationBar::setFilename(const QString &filename)
{
    m_fullFilename = filename;
    
    // Elide if too long (max ~200px for filename area)
    QString displayName = elideFilename(filename, 250);
    // As the window's title row, it also carries the app name, like a title bar.
    m_filenameButton->setText(m_titleBarMode ? displayName + QStringLiteral(" — SpeedyNote")
                                             : displayName);
    
    // Show full name in tooltip if elided
    if (displayName != filename) {
        m_filenameButton->setToolTip(filename + "\n" + tr("Click to toggle tab bar"));
    } else {
        m_filenameButton->setToolTip(tr("Click to toggle tab bar"));
    }
}

QString NavigationBar::elideFilename(const QString &filename, int maxWidth)
{
    QFontMetrics fm(m_filenameButton->font());
    
    if (fm.horizontalAdvance(filename) <= maxWidth) {
        return filename;
    }
    
    // Elide in the middle to preserve extension
    return fm.elidedText(filename, Qt::ElideMiddle, maxWidth);
}

void NavigationBar::updateTheme(bool darkMode, const QColor &accentColor)
{
    m_darkMode = darkMode;
    m_accentColor = accentColor;
    
    // Clear any existing stylesheet that might interfere with palette
    setStyleSheet(QString());
    
    // Apply background color using palette ONLY (most reliable for custom widgets)
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, accentColor);
    setPalette(pal);

    // Icons and text sit on the accent colour, so they follow ITS brightness,
    // not the app theme. With a light system accent in dark mode they were
    // white on white: the whole bar looked empty.
    darkMode = qGray(accentColor.rgb()) < 150;

    // Apply button stylesheets (hover effects, sizing, etc.)
    ButtonStyles::applyToWidget(this, darkMode);
    
    // Update all button icons for theme
    m_launcherButton->setDarkMode(darkMode);
    m_leftSidebarButton->setDarkMode(darkMode);
    m_saveButton->setDarkMode(darkMode);
    m_addButton->setDarkMode(darkMode);
    m_searchButton->setDarkMode(darkMode);
    for (ActionButton *button : { m_minimizeButton, m_maximizeButton, m_closeButton }) button->setDarkMode(darkMode);
    m_fullscreenButton->setDarkMode(darkMode);
    m_shareButton->setDarkMode(darkMode);
    m_rightSidebarButton->setDarkMode(darkMode);
    m_menuButton->setDarkMode(darkMode);
    
    // Style filename button to match theme
    QString textColor = darkMode ? "#ffffff" : "#000000";
    m_filenameButton->setStyleSheet(QString(
        "QPushButton { "
        "   color: %1; "
        "   background: transparent; "
        "   border: none; "
        "   padding: 4px 12px; "
        "   font-weight: normal; "
        "   font-size: 18px; "
        "} "
        "QPushButton:hover { "
        "   background: rgba(%2, 30); "
        "}"
    ).arg(textColor)
     .arg(darkMode ? "255, 255, 255" : "0, 0, 0"));
}

void NavigationBar::setLeftSidebarChecked(bool checked)
{
    const QSignalBlocker blocker(m_leftSidebarButton);
    m_leftSidebarButton->setChecked(checked);
}

void NavigationBar::setRightSidebarChecked(bool checked)
{
    const QSignalBlocker blocker(m_rightSidebarButton);
    m_rightSidebarButton->setChecked(checked);
}

void NavigationBar::setFullscreenChecked(bool checked)
{
    const QSignalBlocker blocker(m_fullscreenButton);
    m_fullscreenButton->setChecked(checked);
}

