#ifndef TOOLBAR_H
#define TOOLBAR_H

#include <QWidget>
#include <QButtonGroup>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QIcon>
#include <QPair>
#include <QStringList>
#include "ToolbarButtons.h"
#include "../core/ToolType.h"
#include "../core/DocumentViewport.h"

class QAbstractButton;
class QBoxLayout;
class QLabel;
class QPaintEvent;
class QPropertyAnimation;
class ExpandableToolButton;
class SubToolbar;
class PenSubToolbar;
class MarkerSubToolbar;
class EraserSubToolbar;
class HighlighterSubToolbar;
class OcrSubToolbar;
class ShapeSubToolbar;
class LaserSubToolbar;

/**
 * Toolbar - the main bar (tool families) and the option bar (the options of
 * the tool in use).
 *
 * Two looks:
 * - Docked: one row in the window layout, as before; the option bar sits in
 *   the row after the last button.
 * - Floating: two pills above the page, only as long as their buttons. The
 *   grip drags it; on release it snaps to the nearest edge. On the left and
 *   right edges both pills are vertical.
 *
 * The row: [pager back] stretch [buttons] stretch [pager next][leave full screen][grip]
 */
class Toolbar : public QWidget {
    Q_OBJECT

    // Allow test class to access private members
    friend class ToolbarButtonTests;

public:
    explicit Toolbar(QWidget *parent = nullptr);

    enum class Edge { Top, Bottom, Left, Right };
    /// Default gap between a floating bar and each edge of its area ("toolbar/edgeGap")
    static constexpr int FLOAT_MARGIN = 8;

    /// Docked: the caller puts the bar in its layout (Left and Right fall back
    /// to Top). Floating: the caller makes the bar a child of the page area and
    /// calls placeFloating() whenever that area changes size.
    void setPlacement(Edge edge, bool floating);
    Edge edge() const { return m_edge; }
    bool isFloating() const { return m_floating; }
    /// Size and place the floating bar on its edge of @p area (parent coordinates).
    /// @p animate slides it there (after a drag), if animations are on.
    void placeFloating(const QRect &area, bool animate = false);
    static Edge nearestEdge(QPoint centre, QSize area);
    /// "top", "bottom", "left", "right" <-> Edge (anything else reads as Top).
    static Edge edgeFromName(const QString &name);
    static QString edgeName(Edge edge);

    void setCurrentTool(ToolType tool);
    void setObjectInsertMode(DocumentViewport::ObjectInsertMode mode);
    void setTouchGestureMode(int mode);
    void updateTheme(bool darkMode);
    void setUndoEnabled(bool enabled);
    void setRedoEnabled(bool enabled);
    void setStraightLineMode(bool enabled);
    /// Check or uncheck the ruler toggle without emitting rulerToggled().
    void setRulerChecked(bool visible);

    /// The options shown in the option bar for @p tool, or null (no option bar).
    SubToolbar* optionsForTool(ToolType tool) const;
    /// The main-bar button of @p tool (for ObjectSelect: the current insert mode's).
    QAbstractButton* buttonForTool(ToolType tool) const;
    /// The dot on a tool's button that shows its colour.
    void setToolColor(ToolType tool, const QColor &color);
    /// Show a tool's window (a Qt::Popup) just past the bar on the page side,
    /// in line with @p anchor (a button of the bar), and inside the screen.
    void showToolWindow(QWidget *window, QWidget *anchor) const;

    // Per-tab state management keyed by unique tab IDs (from TabManager)
    void onTabChanged(int newTabId, int oldTabId);
    void clearTabState(int tabId);

    // Subtoolbar accessors for MainWindow signal wiring
    PenSubToolbar* penSubToolbar() const { return m_penSubToolbar; }
    MarkerSubToolbar* markerSubToolbar() const { return m_markerSubToolbar; }
    EraserSubToolbar* eraserSubToolbar() const { return m_eraserSubToolbar; }
    HighlighterSubToolbar* highlighterSubToolbar() const { return m_highlighterSubToolbar; }
    OcrSubToolbar* ocrSubToolbar() const { return m_ocrSubToolbar; }
    ShapeSubToolbar* shapeSubToolbar() const { return m_shapeSubToolbar; }

    void setOcrAvailable(bool available);
    // Separate from setOcrAvailable: cached text stays viewable without an
    // engine, but a confidence score depends on what produced the data.
    void setOcrConfidenceSupported(bool supported);

    /**
     * @brief The wider single page plus one pager, not the whole row.
     *
     * Reporting the whole row would keep the window too wide to ever reach
     * the width where paging takes over.
     */
    QSize minimumSizeHint() const override;

    // ----- Buttons the user can hide (Settings > Tools) -----
    // A hidden tool still works from its key, a pen button or the quick bar.
    struct ButtonChoice { QString id; QString label; QIcon icon; };
    /// Every button the user can move or hide, in its current order.
    QVector<ButtonChoice> buttonChoices() const;
    /// The saved list of hidden button ids ("toolbar/hiddenButtons").
    static QStringList hiddenButtons();
    /// The saved order of button ids ("toolbar/buttonOrder"). Empty = the built-in order.
    static QStringList buttonOrder();
    /// Save the order and the hidden list, and apply them now.
    void setButtonLayout(const QStringList& order, const QStringList& hidden);
    /// True: three pen icons (fountain, ball, brush), no pen window. False: one
    /// pen icon, and the pen bar has a button for the pen window. Applies now; the caller
    /// saves "toolbar/threePenIcons".
    void setThreePenIcons(bool three);
    /// True (default): a tap on the tool in use hides its option bar, another
    /// tap shows it again; hidden stays hidden across tools until then (not
    /// saved). False: that tap opens the tool's window. Applies now; the
    /// caller saves "toolbar/tapHidesOptions".
    void setTapHidesOptions(bool on);

    /// Show the "leave full screen" button at the end of the bar.
    void setExitFullscreenVisible(bool visible) { m_exitFullscreenButton->setVisible(visible); }

signals:
    void exitFullscreenClicked();
    void toolSelected(ToolType tool);
    /// A second tap on the button of the tool already in use, when that tap
    /// does not hide the options (setTapHidesOptions(false)): opens its window.
    void toolWindowRequested(ToolType tool);
    /// The floating bar moved or changed size (the action bar may need to move).
    void placementChanged();
    void objectInsertModeSelected(DocumentViewport::ObjectInsertMode mode);
    void straightLineToggled(bool enabled);
    void rulerToggled(bool visible);
    void undoClicked();
    void redoClicked();
    void touchGestureModeChanged(int mode);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onOcrExpanded(bool expanded);

private:
    void setupUi();
    void connectSignals();
    /// Make @p tool the current one and show its options.
    void selectTool(ToolType tool);
    /// Show the current tool's options (or hide the option bar), then re-measure.
    void updateOptionBar(bool animate);
    /// updateOptionBar(), then size and place the bar for it at once.
    void relayoutOptions(bool animate);
    /// Hide or show the option bar (a tap on the tool in use).
    void toggleOptions();
    bool isVertical() const { return m_floating && (m_edge == Edge::Left || m_edge == Edge::Right); }
    void updateGripIcon();
    void updateFloatingMask();
    bool handleGripEvent(QEvent *event);

    QWidget* createGapWidget(int width);

    int groupLength(const QVector<QWidget*>& widgets) const;

    /**
     * @brief Length the button row wants right now, along its axis, ignoring
     * page visibility.
     *
     * Measured from size hints, so a docked option bar counts: picking a tool
     * with a wide option bar can be what tips the row into paging.
     */
    int naturalLength() const;

    /**
     * @brief Enter or leave paged mode based on the current width.
     */
    void updatePagination();

    void applyPageVisibility();
    void setToolbarPage(int page);

    /**
     * @brief Switch to the page owning @p widget, if paging is active.
     *
     * Keeps a tool selected by shortcut from checking a button nobody can see.
     */
    void revealWidget(QWidget* widget);

    void installSwipeFilter(QWidget* root);

    // Layout: m_outer holds the row, and the option bar when floating.
    QBoxLayout *m_outer = nullptr;
    QWidget *m_row = nullptr;
    QBoxLayout *m_rowLayout = nullptr;
    QWidget *m_optionBar = nullptr;
    QBoxLayout *m_optionLayout = nullptr;
    QLabel *m_grip = nullptr;

    // Tool buttons with options in the option bar
    ToolButton *m_penButton;
    ToolButton *m_penTypeButtons[3] = {};   ///< Fountain, ball, brush (StrokeStyle pen types)
    bool m_threePenIcons = true;            ///< "toolbar/threePenIcons"
    bool m_tapHidesOptions = true;          ///< "toolbar/tapHidesOptions"
    bool m_optionsHidden = false;           ///< The user tapped the tool in use: no option bar
    ToolButton *m_markerButton;
    ToolButton *m_eraserButton;
    ToolButton *m_highlighterButton;
    ToolButton *m_shapeButton;
    ToolButton *m_laserButton;
    // OCR keeps its controls inline (hover to expand)
    ExpandableToolButton *m_ocrExpandable;

    // Plain tool buttons (no subtoolbar)
    ToolButton *m_lassoButton;
    ToolButton *m_objectImageButton;
    /// The Link tool's controls float in the viewport (LinkObjectBar), so this
    /// button carries no subtoolbar of its own.
    ToolButton *m_objectLinkButton;
    ToolButton *m_objectTextButton;
    ToolButton *m_panButton;

    // Non-exclusive toggles
    ToggleButton *m_straightLineButton;
    ToggleButton *m_rulerButton;

    // Action buttons
    ActionButton *m_undoButton;
    ActionButton *m_redoButton;

    // Tab-specific mode
    ThreeStateButton *m_touchGestureButton;

    // Overflow paging
    ActionButton *m_pagerBackButton = nullptr;
    ActionButton *m_pagerNextButton = nullptr;
    ActionButton *m_exitFullscreenButton = nullptr;
    QVector<QWidget*> m_page1Widgets;   ///< Shown buttons of page 1 (user-hidden ones removed)
    QVector<QWidget*> m_page2Widgets;
    QVector<QWidget*> m_allPage1Widgets;  ///< Every button of page 1, hidden or not
    QVector<QWidget*> m_allPage2Widgets;
    QVector<QPair<QString, QWidget*>> customizableButtons() const;
    /// @p order with each id it lacks inserted after its built-in predecessor.
    QStringList completeOrder(const QStringList& order) const;
    /// The pen button ids the current pen mode does not use.
    QStringList penModeHidden() const;
    void applyButtonLayout(const QStringList& order, const QStringList& userHidden);

    // Tool group for exclusive selection
    QButtonGroup *m_toolGroup;

    // Subtoolbar instances (owned by this Toolbar; all but OCR live in the option bar)
    PenSubToolbar *m_penSubToolbar;
    MarkerSubToolbar *m_markerSubToolbar;
    EraserSubToolbar *m_eraserSubToolbar;
    HighlighterSubToolbar *m_highlighterSubToolbar;
    OcrSubToolbar *m_ocrSubToolbar;
    ShapeSubToolbar *m_shapeSubToolbar;
    LaserSubToolbar *m_laserSubToolbar;
    QVector<SubToolbar*> m_optionSets;  ///< Every set the option bar can show

    // Placement
    Edge m_edge = Edge::Top;
    bool m_floating = false;
    bool m_animations = true;           ///< "toolbar/animations", read in setPlacement()
    int m_floatMargin = FLOAT_MARGIN;   ///< "toolbar/edgeGap", read in setPlacement(): the same on every edge
    bool m_optionWanted = false;        ///< The current tool has options to show
    QRect m_area;                       ///< Last area given to placeFloating()
    QRect m_lastTarget;
    int m_availableLength = 0;          ///< Floating: room along the row's axis
    QPropertyAnimation *m_posAnimation = nullptr;
    bool m_dragging = false;
    QPoint m_dragOffset;

    // State
    bool m_darkMode = false;
    bool m_paged = false;
    int m_currentPage = 0;
    bool m_updatingPagination = false;
    QPoint m_swipeStart;
    bool m_swipeTracking = false;
    bool m_swipeConsumed = false;
    QColor m_borderColor;
    ToolType m_currentTool = ToolType::Pen;
    DocumentViewport::ObjectInsertMode m_objectInsertMode =
        DocumentViewport::ObjectInsertMode::Image;

    static constexpr int TOOLBAR_HEIGHT = 44;
    static constexpr int PAGING_HYSTERESIS = 12;
    static constexpr int SWIPE_THRESHOLD = 48;
};

#endif // TOOLBAR_H
