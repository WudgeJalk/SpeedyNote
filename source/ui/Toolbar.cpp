#include "Toolbar.h"
#include "../compat/qt_compat.h"
#include "launcher/KineticScrollHelper.h"
#include "widgets/ExpandableToolButton.h"
#include "subtoolbars/PenSubToolbar.h"
#include "subtoolbars/MarkerSubToolbar.h"
#include "subtoolbars/EraserSubToolbar.h"
#include "subtoolbars/HighlighterSubToolbar.h"
#include "subtoolbars/OcrSubToolbar.h"
#include "subtoolbars/ShapeSubToolbar.h"
#include "subtoolbars/LaserSubToolbar.h"
#include "widgets/ColorListRow.h"

#include <QBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPainterPath>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRegion>
#include <QSettings>
#include <algorithm>
#include <climits>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QtMath>

namespace {

// The rounded card behind a floating pill or the docked option bar. The
// shadow stays inside @p rect, so a mask of @p rect does not cut it.
void paintPill(QPainter& painter, const QRect& rect, bool dark)
{
    constexpr qreal radius = 10;
    const QRectF card = QRectF(rect).adjusted(2, 2, -2, -2);
    painter.setPen(Qt::NoPen);
    for (int i = 2; i >= 1; --i) {
        QPainterPath shadow;
        shadow.addRoundedRect(card.adjusted(-i, -i, i, i), radius + i, radius + i);
        painter.setBrush(dark ? QColor(255, 255, 255, 10 + i * 5) : QColor(0, 0, 0, 10 + i * 5));
        painter.drawPath(shadow);
    }
    QPainterPath path;
    path.addRoundedRect(card, radius, radius);
    painter.setPen(QPen(dark ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 25), 0.5));
    painter.setBrush(dark ? QColor(0, 0, 0) : QColor(255, 255, 255));
    painter.drawPath(path);
}

constexpr int FLOAT_PADDING = 5;   // Inside a floating pill, around its buttons

} // namespace

Toolbar::Toolbar(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    connectSignals();
    updateTheme(false);
}

void Toolbar::setupUi()
{
    setFixedHeight(TOOLBAR_HEIGHT);

    // m_outer: the row, and the option bar when floating. The row keeps the
    // layout the toolbar always had.
    m_outer = new QBoxLayout(QBoxLayout::TopToBottom, this);
    m_outer->setContentsMargins(0, 0, 0, 0);
    m_outer->setSpacing(2);

    m_row = new QWidget(this);
    m_rowLayout = new QBoxLayout(QBoxLayout::LeftToRight, m_row);
    m_rowLayout->setContentsMargins(4, 4, 4, 4);
    m_rowLayout->setSpacing(2);
    m_outer->addWidget(m_row);
    QBoxLayout *mainLayout = m_rowLayout;

    m_optionBar = new QWidget(this);
    m_optionLayout = new QBoxLayout(QBoxLayout::LeftToRight, m_optionBar);
    m_optionLayout->setContentsMargins(0, 0, 0, 0);
    m_optionLayout->setSpacing(0);
    m_optionBar->hide();

    // Outside the stretches so each pager pins to its edge instead of drifting
    // with the centred button group. Back sits on the left, where a reader
    // expects to go back from.
    m_pagerBackButton = new ActionButton(this);
    m_pagerBackButton->setThemedIcon("left_arrow");
    m_pagerBackButton->setToolTip(tr("Back to drawing tools"));
    m_pagerBackButton->hide();
    mainLayout->addWidget(m_pagerBackButton);
    connect(m_pagerBackButton, &QPushButton::clicked,
            this, [this]() { setToolbarPage(0); });

    mainLayout->addStretch(1);

    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);

    // --- Pen ---
    m_penSubToolbar = new PenSubToolbar();
    m_penButton = new ToolButton(this);
    m_penButton->setThemedIcon("pen");
    m_penButton->setToolTip(tr("Pen Tool (B)") + QLatin1Char('\n') + tr("Tap again to hide or show its options."));
    m_toolGroup->addButton(m_penButton);
    mainLayout->addWidget(m_penButton);

    // --- The three pen types, each its own tool (Settings > Tools: "three pen icons") ---
    const char* penIcons[] = {"pen_fountain", "pen", "brush"};
    const QString penTips[] = {tr("Fountain pen (B)"), tr("Ball pen (B)"), tr("Brush pen (B)")};
    for (int type = 0; type < 3; ++type) {
        m_penTypeButtons[type] = new ToolButton(this);
        m_penTypeButtons[type]->setThemedIcon(penIcons[type]);
        m_penTypeButtons[type]->setToolTip(penTips[type]);
        m_toolGroup->addButton(m_penTypeButtons[type]);
        mainLayout->addWidget(m_penTypeButtons[type]);
    }
    m_threePenIcons = QSettings("SpeedyNote", "App").value("toolbar/threePenIcons", true).toBool();
    m_tapHidesOptions = QSettings("SpeedyNote", "App").value("toolbar/tapHidesOptions", true).toBool();

    // --- Marker ---
    m_markerSubToolbar = new MarkerSubToolbar();
    m_markerButton = new ToolButton(this);
    m_markerButton->setThemedIcon("marker");
    m_markerButton->setToolTip(tr("Marker Tool (M)"));
    m_toolGroup->addButton(m_markerButton);
    mainLayout->addWidget(m_markerButton);

    // --- Eraser ---
    m_eraserSubToolbar = new EraserSubToolbar();
    m_eraserButton = new ToolButton(this);
    m_eraserButton->setThemedIcon("eraser");
    m_eraserButton->setToolTip(tr("Eraser Tool (E)"));
    m_toolGroup->addButton(m_eraserButton);
    mainLayout->addWidget(m_eraserButton);

    // --- Straight Line Toggle ---
    m_straightLineButton = new ToggleButton(this);
    m_straightLineButton->setThemedIcon("straightLine");
    m_straightLineButton->setToolTip(tr("Straight Line Mode (/)"));
    mainLayout->addWidget(m_straightLineButton);

    // --- Shapes ---
    m_shapeSubToolbar = new ShapeSubToolbar();
    m_shapeButton = new ToolButton(this);
    m_shapeButton->setThemedIcon("shape");
    m_shapeButton->setToolTip(tr("Shape Tool (S)"));
    m_toolGroup->addButton(m_shapeButton);
    mainLayout->addWidget(m_shapeButton);

    // --- Laser pointer ---
    m_laserSubToolbar = new LaserSubToolbar();
    m_laserButton = new ToolButton(this);
    m_laserButton->setThemedIcon("laser");
    m_laserButton->setToolTip(tr("Laser Pointer (K)"));
    m_toolGroup->addButton(m_laserButton);
    mainLayout->addWidget(m_laserButton);

    // --- Ruler (a toggle, not a tool: the pen follows its edge) ---
    m_rulerButton = new ToggleButton(this);
    m_rulerButton->setThemedIcon("ruler");
    m_rulerButton->setToolTip(tr("Ruler (R)"));
    mainLayout->addWidget(m_rulerButton);

    // --- Lasso (no subtoolbar) ---
    m_lassoButton = new ToolButton(this);
    m_lassoButton->setThemedIcon("rope");
    m_lassoButton->setToolTip(tr("Lasso Selection Tool (L)"));
    m_toolGroup->addButton(m_lassoButton);
    mainLayout->addWidget(m_lassoButton);

    // --- Object tools (one internal ObjectSelect tool, three insert modes) ---
    // The Link tool's controls live in a floating, viewport-owned LinkObjectBar
    // anchored to the selected LinkObject, so this is a plain toggle like the
    // other two insert modes.
    m_objectImageButton = new ToolButton(this);
    m_objectImageButton->setThemedIcon("objectinsert");
    m_objectImageButton->setToolTip(tr("Image Object Tool (I)"));
    m_toolGroup->addButton(m_objectImageButton);
    mainLayout->addWidget(m_objectImageButton);

    m_objectLinkButton = new ToolButton(this);
    m_objectLinkButton->setThemedIcon("linkicon");
    m_objectLinkButton->setToolTip(tr("Link Object Tool (Ctrl+.)"));
    m_toolGroup->addButton(m_objectLinkButton);
    mainLayout->addWidget(m_objectLinkButton);

    m_objectTextButton = new ToolButton(this);
    m_objectTextButton->setThemedIcon("auto");
    m_objectTextButton->setToolTip(tr("Text Object Tool (Ctrl+T)"));
    m_toolGroup->addButton(m_objectTextButton);
    mainLayout->addWidget(m_objectTextButton);

    // --- Highlighter ---
    m_highlighterSubToolbar = new HighlighterSubToolbar();
    m_highlighterButton = new ToolButton(this);
    m_highlighterButton->setThemedIcon("text");
    m_highlighterButton->setToolTip(tr("Text Highlighter Tool (T)"));
    m_toolGroup->addButton(m_highlighterButton);
    mainLayout->addWidget(m_highlighterButton);

    m_page1Widgets = {
        m_penButton, m_penTypeButtons[0], m_penTypeButtons[1], m_penTypeButtons[2],
        m_markerButton, m_eraserButton,
        m_straightLineButton, m_shapeButton, m_laserButton, m_rulerButton, m_lassoButton, m_objectImageButton,
        m_objectLinkButton, m_objectTextButton, m_highlighterButton
    };

    // The options of the tool in use. Only the current tool's set is shown.
    // A later tool with options adds its set here and in optionsForTool().
    m_optionSets = { m_penSubToolbar, m_markerSubToolbar,
                     m_eraserSubToolbar, m_highlighterSubToolbar, m_shapeSubToolbar,
                     m_laserSubToolbar };
    for (SubToolbar* sub : m_optionSets) {
        m_optionLayout->addWidget(sub);
        sub->hide();
    }

    // --- OCR (not in tool group, hover-to-expand) ---
    m_ocrSubToolbar = new OcrSubToolbar();
    m_ocrExpandable = new ExpandableToolButton(this);
    m_ocrExpandable->setThemedIcon("ocr");
    m_ocrExpandable->toolButton()->setToolTip(tr("OCR - Text Recognition"));
    m_ocrExpandable->setContentWidget(m_ocrSubToolbar);
    m_ocrExpandable->setHoverExpand(true);
    connect(m_ocrExpandable, &ExpandableToolButton::expandedChanged,
            this, &Toolbar::onOcrExpanded);
    mainLayout->addWidget(m_ocrExpandable);

    // --- Pan (no subtoolbar) ---
    m_panButton = new ToolButton(this);
    m_panButton->setThemedIcon("move");
    m_panButton->setToolTip(tr("Pan Tool (H)"));
    m_toolGroup->addButton(m_panButton);
    mainLayout->addWidget(m_panButton);

    // Real widgets rather than addSpacing(): a QSpacerItem cannot be hidden,
    // so the gaps would survive on page 1 as a hole where this group was.
    QWidget* undoGap = createGapWidget(16);
    mainLayout->addWidget(undoGap);

    // --- Undo / Redo ---
    m_undoButton = new ActionButton(this);
    m_undoButton->setThemedIcon("undo");
    m_undoButton->setToolTip(tr("Undo (Ctrl+Z)"));
    mainLayout->addWidget(m_undoButton);

    m_redoButton = new ActionButton(this);
    m_redoButton->setThemedIcon("redo");
    m_redoButton->setToolTip(tr("Redo (Ctrl+Shift+Z / Ctrl+Y)"));
    mainLayout->addWidget(m_redoButton);

    QWidget* touchGap = createGapWidget(8);
    mainLayout->addWidget(touchGap);

    // --- Touch gesture mode ---
    m_touchGestureButton = new ThreeStateButton(this);
    m_touchGestureButton->setThemedIcon("hand");
    m_touchGestureButton->setToolTip(tr("Touch Gesture Mode\n0: Off\n1: Y-axis scroll only\n2: Full gestures"));
    mainLayout->addWidget(m_touchGestureButton);

    m_page2Widgets = {
        m_ocrExpandable, m_panButton, undoGap,
        m_undoButton, m_redoButton, touchGap, m_touchGestureButton
    };

    mainLayout->addStretch(1);

    m_pagerNextButton = new ActionButton(this);
    m_pagerNextButton->setThemedIcon("right_arrow");
    m_pagerNextButton->setToolTip(tr("More tools"));
    m_pagerNextButton->hide();
    mainLayout->addWidget(m_pagerNextButton);
    connect(m_pagerNextButton, &QPushButton::clicked,
            this, [this]() { setToolbarPage(1); });

    // Leaves full screen. At the far end of the bar, and only shown in full
    // screen: the title row with the normal button is hidden then.
    m_exitFullscreenButton = new ActionButton(this);
    m_exitFullscreenButton->setThemedIcon("exit_fullscreen");
    m_exitFullscreenButton->setToolTip(tr("Leave full screen (F11)"));
    m_exitFullscreenButton->hide();
    mainLayout->addWidget(m_exitFullscreenButton);
    connect(m_exitFullscreenButton, &QPushButton::clicked,
            this, &Toolbar::exitFullscreenClicked);

    // Drags the floating bar (handled in eventFilter). Touch and pen arrive
    // as synthesized mouse events, so any pointer can drag it.
    m_grip = new QLabel(this);
    m_grip->setAlignment(Qt::AlignCenter);
    m_grip->setCursor(Qt::SizeAllCursor);
    m_grip->setToolTip(tr("Drag to move the toolbar"));
    m_grip->hide();
    mainLayout->addWidget(m_grip);

    // Docked, the option bar sits in the row after the last button.
    m_rowLayout->insertWidget(m_rowLayout->indexOf(m_pagerNextButton) - 1, m_optionBar);

    m_allPage1Widgets = m_page1Widgets;
    m_allPage2Widgets = m_page2Widgets;
    applyButtonLayout(buttonOrder(), hiddenButtons());  // Ends with applyPageVisibility()
    buttonForTool(ToolType::Pen)->setChecked(true);
    updateOptionBar(false);
    updateGripIcon();

    installSwipeFilter(this);
}

// ===== Buttons the user can hide =====

QVector<QPair<QString, QWidget*>> Toolbar::customizableButtons() const
{
    return {
        { QStringLiteral("pen"),           m_penButton },
        { QStringLiteral("pen_fountain"),  m_penTypeButtons[0] },
        { QStringLiteral("pen_ball"),      m_penTypeButtons[1] },
        { QStringLiteral("pen_brush"),     m_penTypeButtons[2] },
        { QStringLiteral("marker"),        m_markerButton },
        { QStringLiteral("eraser"),        m_eraserButton },
        { QStringLiteral("straight_line"), m_straightLineButton },
        { QStringLiteral("shapes"),        m_shapeButton },
        { QStringLiteral("laser"),         m_laserButton },
        { QStringLiteral("ruler"),         m_rulerButton },
        { QStringLiteral("lasso"),         m_lassoButton },
        { QStringLiteral("image"),         m_objectImageButton },
        { QStringLiteral("link"),          m_objectLinkButton },
        { QStringLiteral("text"),          m_objectTextButton },
        { QStringLiteral("highlighter"),   m_highlighterButton },
        { QStringLiteral("ocr"),           m_ocrExpandable },
        { QStringLiteral("pan"),           m_panButton },
        { QStringLiteral("undo"),          m_undoButton },
        { QStringLiteral("redo"),          m_redoButton },
        { QStringLiteral("touch"),         m_touchGestureButton },
    };
}

QStringList Toolbar::completeOrder(const QStringList& order) const
{
    // A button newer than the saved order goes right after its built-in
    // predecessor, so new tools join the tools and the page split keeps
    // undo, redo and touch together.
    QStringList result = order;
    const auto all = customizableButtons();
    for (int i = 0; i < all.size(); ++i) {
        if (result.contains(all[i].first)) continue;
        int at = 0;
        for (int j = i - 1; j >= 0 && at == 0; --j)
            at = result.indexOf(all[j].first) + 1;
        result.insert(at, all[i].first);
    }
    return result;
}

QVector<Toolbar::ButtonChoice> Toolbar::buttonChoices() const
{
    // In the saved order, completed with the buttons it does not know.
    auto all = customizableButtons();
    const QStringList order = completeOrder(buttonOrder());
    std::stable_sort(all.begin(), all.end(), [&order](const auto& a, const auto& b) {
        const int ia = order.indexOf(a.first), ib = order.indexOf(b.first);
        return (ia < 0 ? INT_MAX : ia) < (ib < 0 ? INT_MAX : ib);
    });

    QVector<ButtonChoice> choices;
    const QStringList unused = penModeHidden();
    for (const auto& entry : all) {
        if (unused.contains(entry.first)) continue;  // Not in the bar in this pen mode
        // The tooltip is the button's translated name; keep its first line.
        auto* expandable = qobject_cast<ExpandableToolButton*>(entry.second);
        const QAbstractButton* button = expandable
            ? static_cast<QAbstractButton*>(expandable->toolButton())
            : qobject_cast<QAbstractButton*>(entry.second);
        const QString tip = button ? button->toolTip() : entry.second->toolTip();
        // Not button->icon(): the selected tool's icon is the inverted one.
        const auto* themed = qobject_cast<const ToolbarButton*>(button);
        choices.append({ entry.first, tip.section(QLatin1Char('\n'), 0, 0),
                         themed ? themed->themedIcon() : button ? button->icon() : QIcon() });
    }
    return choices;
}

QStringList Toolbar::hiddenButtons()
{
    return QSettings("SpeedyNote", "App").value("toolbar/hiddenButtons").toStringList();
}

QStringList Toolbar::buttonOrder()
{
    return QSettings("SpeedyNote", "App").value("toolbar/buttonOrder").toStringList();
}

void Toolbar::setButtonLayout(const QStringList& order, const QStringList& hidden)
{
    QSettings settings("SpeedyNote", "App");
    settings.setValue("toolbar/buttonOrder", order);
    settings.setValue("toolbar/hiddenButtons", hidden);
    applyButtonLayout(order, hidden);
}

QStringList Toolbar::penModeHidden() const
{
    if (m_threePenIcons) return { QStringLiteral("pen") };
    return { QStringLiteral("pen_fountain"), QStringLiteral("pen_ball"), QStringLiteral("pen_brush") };
}

void Toolbar::setThreePenIcons(bool three)
{
    const bool wasPen = m_currentTool == ToolType::Pen;
    m_threePenIcons = three;
    applyButtonLayout(buttonOrder(), hiddenButtons());
    if (wasPen) setCurrentTool(ToolType::Pen);  // Check the button the pen has now
}

void Toolbar::applyButtonLayout(const QStringList& order, const QStringList& userHidden)
{
    // The pen buttons of the other pen mode are never shown.
    const QStringList hidden = userHidden + penModeHidden();
    m_penSubToolbar->setWindowButtonVisible(!m_threePenIcons);
    QBoxLayout* mainLayout = m_rowLayout;
    const auto all = customizableButtons();

    // The two page lists drive visibility and the width measured for paging.
    // Built-in order: the lists as setupUi made them (with the two gaps).
    // User order: the buttons in that order, no gaps, split at the same count.
    QVector<QWidget*> page1 = m_allPage1Widgets;
    QVector<QWidget*> page2 = m_allPage2Widgets;
    if (!order.isEmpty()) {
        QVector<QWidget*> ordered;
        for (const QString& id : completeOrder(order)) {
            for (const auto& entry : all) {
                if (entry.first == id && !ordered.contains(entry.second)) ordered << entry.second;
            }
        }
        page1 = ordered.mid(0, m_allPage1Widgets.size());
        page2 = ordered.mid(m_allPage1Widgets.size());
    }

    // Put the row back in that order. It sits after the back pager and the
    // left stretch, so it starts at layout index 2.
    for (QWidget* widget : m_allPage1Widgets + m_allPage2Widgets) {
        mainLayout->removeWidget(widget);
        widget->hide();
    }
    int at = 2;
    for (QWidget* widget : page1 + page2) mainLayout->insertWidget(at++, widget);

    for (const auto& entry : all) {
        if (!hidden.contains(entry.first)) continue;
        page1.removeAll(entry.second);
        page2.removeAll(entry.second);
    }
    m_page1Widgets = page1;
    m_page2Widgets = page2;
    applyPageVisibility();
    // Not during construction: the width is still 0 then, which would read as
    // "too narrow" and switch the paging on. The first resize measures it.
    if (isVisible()) {
        updatePagination();
        updateGeometry();
    }
}

QWidget* Toolbar::createGapWidget(int width)
{
    auto* gap = new QWidget(this);
    gap->setFixedSize(width, width);  // Square: the same gap in a row or a column
    return gap;
}

void Toolbar::connectSignals()
{
    // A tap on the tool in use hides or shows its options (Settings > Tools).
    // With that setting off, it asks for the tool's window (pen type, eraser options...).
    auto pick = [this](ToolType tool) {
        const bool again = m_currentTool == tool;
        selectTool(tool);
        emit toolSelected(tool);
        if (!again) return;
        if (m_tapHidesOptions) toggleOptions();
        else emit toolWindowRequested(tool);
    };
    connect(m_penButton, &QPushButton::clicked, this, [pick]() { pick(ToolType::Pen); });
    // A pen type icon picks its type; a second tap opens no window.
    for (int type = 0; type < 3; ++type) {
        connect(m_penTypeButtons[type], &QPushButton::clicked, this, [this, type]() {
            const bool again = m_currentTool == ToolType::Pen
                               && m_penSubToolbar->currentStyle().penType == type;
            m_penSubToolbar->setPenType(quint8(type));
            selectTool(ToolType::Pen);
            emit toolSelected(ToolType::Pen);
            if (again && m_tapHidesOptions) toggleOptions();
        });
    }
    // The type can also change elsewhere (the pen window): check its icon.
    connect(m_penSubToolbar, &PenSubToolbar::penStyleChanged, this, [this]() {
        if (m_currentTool == ToolType::Pen) buttonForTool(ToolType::Pen)->setChecked(true);
    });
    connect(m_markerButton, &QPushButton::clicked, this, [pick]() { pick(ToolType::Marker); });
    connect(m_eraserButton, &QPushButton::clicked, this, [pick]() { pick(ToolType::Eraser); });
    connect(m_lassoButton, &QPushButton::clicked, this, [pick]() { pick(ToolType::Lasso); });
    connect(m_shapeButton, &QPushButton::clicked, this, [pick]() { pick(ToolType::Shape); });
    connect(m_laserButton, &QPushButton::clicked, this, [pick]() { pick(ToolType::Laser); });
    connect(m_highlighterButton, &QPushButton::clicked, this, [pick]() { pick(ToolType::Highlighter); });
    connect(m_panButton, &QPushButton::clicked, this, [pick]() { pick(ToolType::Pan); });

    connect(m_objectImageButton, &QPushButton::clicked, this, [this]() {
        setObjectInsertMode(DocumentViewport::ObjectInsertMode::Image);
        selectTool(ToolType::ObjectSelect);
        emit objectInsertModeSelected(DocumentViewport::ObjectInsertMode::Image);
        emit toolSelected(ToolType::ObjectSelect);
    });
    connect(m_objectLinkButton, &QPushButton::clicked, this, [this]() {
        setObjectInsertMode(DocumentViewport::ObjectInsertMode::Link);
        selectTool(ToolType::ObjectSelect);
        emit objectInsertModeSelected(DocumentViewport::ObjectInsertMode::Link);
        emit toolSelected(ToolType::ObjectSelect);
    });
    connect(m_objectTextButton, &QPushButton::clicked, this, [this]() {
        setObjectInsertMode(DocumentViewport::ObjectInsertMode::Text);
        selectTool(ToolType::ObjectSelect);
        emit objectInsertModeSelected(DocumentViewport::ObjectInsertMode::Text);
        emit toolSelected(ToolType::ObjectSelect);
    });

    connect(m_straightLineButton, &ToggleButton::toggled,
            this, &Toolbar::straightLineToggled);
    connect(m_rulerButton, &ToggleButton::toggled, this, &Toolbar::rulerToggled);

    connect(m_undoButton, &QPushButton::clicked,
            this, &Toolbar::undoClicked);
    connect(m_redoButton, &QPushButton::clicked,
            this, &Toolbar::redoClicked);

    connect(m_touchGestureButton, &ThreeStateButton::stateChanged,
            this, &Toolbar::touchGestureModeChanged);
}

// ===== Tools and their options =====

SubToolbar* Toolbar::optionsForTool(ToolType tool) const
{
    switch (tool) {
    case ToolType::Pen:         return m_penSubToolbar;
    case ToolType::Marker:      return m_markerSubToolbar;
    case ToolType::Eraser:      return m_eraserSubToolbar;
    case ToolType::Highlighter: return m_highlighterSubToolbar;
    case ToolType::Shape:       return m_shapeSubToolbar;
    case ToolType::Laser:       return m_laserSubToolbar;
    default:                    return nullptr;
    }
}

QAbstractButton* Toolbar::buttonForTool(ToolType tool) const
{
    switch (tool) {
    case ToolType::Pen:
        // Three icons: the one of the pen type used last.
        return m_threePenIcons ? m_penTypeButtons[qMin(int(m_penSubToolbar->currentStyle().penType), 2)]
                               : m_penButton;
    case ToolType::Marker:      return m_markerButton;
    case ToolType::Eraser:      return m_eraserButton;
    case ToolType::Highlighter: return m_highlighterButton;
    case ToolType::Lasso:       return m_lassoButton;
    case ToolType::Pan:         return m_panButton;
    case ToolType::Shape:       return m_shapeButton;
    case ToolType::Laser:       return m_laserButton;
    case ToolType::ObjectSelect:
        switch (m_objectInsertMode) {
        case DocumentViewport::ObjectInsertMode::Image: return m_objectImageButton;
        case DocumentViewport::ObjectInsertMode::Link:  return m_objectLinkButton;
        case DocumentViewport::ObjectInsertMode::Text:  return m_objectTextButton;
        }
        break;
    }
    return nullptr;
}

void Toolbar::setToolColor(ToolType tool, const QColor &color)
{
    if (tool == ToolType::Pen) {
        // Every pen icon shows the pen colour, whichever mode is on.
        m_penButton->setAccentColor(color);
        for (ToolButton* button : m_penTypeButtons) button->setAccentColor(color);
        return;
    }
    if (auto* button = qobject_cast<ToolButton*>(buttonForTool(tool)))
        button->setAccentColor(color);
}

void Toolbar::showToolWindow(QWidget *window, QWidget *anchor) const
{
    window->adjustSize();
    const QSize size = window->size();
    const QRect bar(mapToGlobal(QPoint(0, 0)), this->size());
    const QWidget *from = (anchor && anchor->isVisible()) ? anchor : this;
    const QPoint centre = from->mapToGlobal(from->rect().center());
    constexpr int gap = 4;
    QPoint pos;
    switch (m_edge) {
    case Edge::Top:    pos = QPoint(centre.x() - size.width() / 2, bar.bottom() + gap); break;
    case Edge::Bottom: pos = QPoint(centre.x() - size.width() / 2, bar.top() - gap - size.height()); break;
    case Edge::Left:   pos = QPoint(bar.right() + gap, centre.y() - size.height() / 2); break;
    case Edge::Right:  pos = QPoint(bar.left() - gap - size.width(), centre.y() - size.height() / 2); break;
    }
    if (const QScreen *screen = QGuiApplication::screenAt(centre)) {
        const QRect room = screen->availableGeometry();
        pos.setX(qBound(room.left(), pos.x(), qMax(room.left(), room.right() + 1 - size.width())));
        pos.setY(qBound(room.top(), pos.y(), qMax(room.top(), room.bottom() + 1 - size.height())));
    }
    window->move(pos);
    window->show();
}

void Toolbar::selectTool(ToolType tool)
{
    if (m_currentTool == tool)
        return;

    m_currentTool = tool;
    // Marker and highlighter share colours: pick up what the other one changed.
    if (SubToolbar* options = optionsForTool(tool))
        options->syncSharedState();
    relayoutOptions(true);
}

void Toolbar::relayoutOptions(bool animate)
{
    updateOptionBar(animate);

    // Synchronously, not via the queued LayoutRequest: an option bar that has
    // just appeared past the available width would otherwise be laid out once
    // while still unpaged, and the layout squeezes items below their minimum
    // to make an oversized row fit.
    updateGeometry();
    updatePagination();
    placeFloating(m_area);
}

void Toolbar::toggleOptions()
{
    // A tool without options has nothing to hide: the tap changes nothing.
    if (!optionsForTool(m_currentTool)) return;
    m_optionsHidden = !m_optionsHidden;
    relayoutOptions(true);
}

void Toolbar::setTapHidesOptions(bool on)
{
    m_tapHidesOptions = on;
    if (!on && m_optionsHidden) {   // Else nothing could show them again
        m_optionsHidden = false;
        relayoutOptions(false);
    }
}

void Toolbar::updateOptionBar(bool animate)
{
    SubToolbar* current = optionsForTool(m_currentTool);
    for (SubToolbar* sub : m_optionSets)
        sub->setVisible(sub == current);

    // Docked, an open OCR strip takes the option bar's place in the row, as it
    // took the expanded tool's place before.
    m_optionWanted = current && !m_optionsHidden && (m_floating || !m_ocrExpandable->isExpanded());
    applyPageVisibility();

    // Costs nothing while the pen writes: it only runs when the tool changes.
    if (!animate || !m_animations || !m_optionBar->isVisible())
        return;
    auto* effect = new QGraphicsOpacityEffect(m_optionBar);
    m_optionBar->setGraphicsEffect(effect);  // Replaces (deletes) a fade still running
    auto* fade = new QPropertyAnimation(effect, "opacity", this);
    fade->setDuration(120);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    const QPointer<QGraphicsOpacityEffect> guard(effect);
    connect(fade, &QPropertyAnimation::finished, this, [this, guard]() {
        if (guard) m_optionBar->setGraphicsEffect(nullptr);
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void Toolbar::setCurrentTool(ToolType tool)
{
    QAbstractButton* button = buttonForTool(tool);
    m_toolGroup->blockSignals(true);
    if (button) button->setChecked(true);
    m_toolGroup->blockSignals(false);

    // A shortcut can select Pan while page 1 is showing, which would check a
    // button nobody can see and leave the toolbar looking unselected.
    revealWidget(button);

    selectTool(tool);
}

void Toolbar::setObjectInsertMode(DocumentViewport::ObjectInsertMode mode)
{
    m_objectInsertMode = mode;
    if (m_currentTool != ToolType::ObjectSelect)
        return;

    QAbstractButton* button = buttonForTool(ToolType::ObjectSelect);
    m_toolGroup->blockSignals(true);
    if (button) button->setChecked(true);
    m_toolGroup->blockSignals(false);

    revealWidget(button);
}

void Toolbar::onOcrExpanded(bool expanded)
{
    Q_UNUSED(expanded);
    relayoutOptions(false);
}

// ===== Placement =====

Toolbar::Edge Toolbar::edgeFromName(const QString &name)
{
    if (name == QLatin1String("bottom")) return Edge::Bottom;
    if (name == QLatin1String("left")) return Edge::Left;
    if (name == QLatin1String("right")) return Edge::Right;
    return Edge::Top;
}

QString Toolbar::edgeName(Edge edge)
{
    switch (edge) {
    case Edge::Bottom: return QStringLiteral("bottom");
    case Edge::Left:   return QStringLiteral("left");
    case Edge::Right:  return QStringLiteral("right");
    case Edge::Top:    break;
    }
    return QStringLiteral("top");
}

Toolbar::Edge Toolbar::nearestEdge(QPoint centre, QSize area)
{
    const int left = centre.x(), right = area.width() - centre.x();
    const int top = centre.y(), bottom = area.height() - centre.y();
    const int best = qMin(qMin(left, right), qMin(top, bottom));
    if (best == top) return Edge::Top;
    if (best == bottom) return Edge::Bottom;
    return best == left ? Edge::Left : Edge::Right;
}

void Toolbar::setPlacement(Edge edge, bool floating)
{
    if (!floating && (edge == Edge::Left || edge == Edge::Right))
        edge = Edge::Top;  // A docked bar is a row of the window: top or bottom
    QSettings settings("SpeedyNote", "App");
    m_animations = settings.value("toolbar/animations", true).toBool();
    m_floatMargin = qBound(0, settings.value("toolbar/edgeGap", FLOAT_MARGIN).toInt(), 64);

    if (floating != m_floating) {
        if (floating) {
            m_rowLayout->removeWidget(m_optionBar);
            m_outer->addWidget(m_optionBar);
        } else {
            m_outer->removeWidget(m_optionBar);
            m_rowLayout->insertWidget(m_rowLayout->indexOf(m_pagerNextButton) - 1, m_optionBar);
        }
    }
    m_edge = edge;
    m_floating = floating;
    const bool vertical = isVertical();

    // The option bar sits on the page side of the row.
    static const QBoxLayout::Direction kOuter[] = {
        QBoxLayout::TopToBottom, QBoxLayout::BottomToTop,
        QBoxLayout::LeftToRight, QBoxLayout::RightToLeft };
    m_outer->setDirection(floating ? kOuter[static_cast<int>(edge)] : QBoxLayout::TopToBottom);
    // Floating, each pill is only as long as its buttons, centred across the bar.
    const Qt::Alignment across = floating ? (vertical ? Qt::AlignVCenter : Qt::AlignHCenter)
                                          : Qt::Alignment();
    m_outer->setAlignment(m_row, across);
    m_outer->setAlignment(m_optionBar, across);

    const QBoxLayout::Direction along = vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    m_rowLayout->setDirection(along);
    m_optionLayout->setDirection(along);
    const int pad = floating ? FLOAT_PADDING : 4;
    m_rowLayout->setContentsMargins(pad, pad, pad, pad);
    m_optionLayout->setContentsMargins(floating ? QMargins(pad, pad, pad, pad) : QMargins());
    for (SubToolbar* sub : m_optionSets)
        sub->setOrientation(vertical ? Qt::Vertical : Qt::Horizontal);
    m_pagerBackButton->setThemedIcon(vertical ? "layer_uparrow" : "left_arrow");
    m_pagerNextButton->setThemedIcon(vertical ? "layer_downarrow" : "right_arrow");
    m_grip->setVisible(floating);
    updateGripIcon();

    // Docked: a fixed-height row of the window layout. Floating: placeFloating() sizes it.
    m_outer->setSizeConstraint(floating ? QLayout::SetNoConstraint : QLayout::SetDefaultConstraint);
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    if (!floating) {
        setFixedHeight(TOOLBAR_HEIGHT);
        if (m_posAnimation) m_posAnimation->stop();
        clearMask();
        for (ColorListRow* colors : m_optionBar->findChildren<ColorListRow*>())
            colors->fitTo(INT_MAX);  // Docked, the row pages instead (placeFloating fits them)
    }
    setAutoFillBackground(!floating);

    updateOptionBar(false);
    updateGeometry();
    if (!floating && isVisible())
        updatePagination();
    update();
}

void Toolbar::placeFloating(const QRect &area, bool animate)
{
    m_area = area;
    if (!m_floating || area.isEmpty() || m_dragging)
        return;

    const QRect inner = area.adjusted(m_floatMargin, m_floatMargin, -m_floatMargin, -m_floatMargin);
    m_availableLength = isVertical() ? inner.height() : inner.width();
    updatePagination();
    // Measure this pass's buttons, not what the queued layout pass last saw.
    m_rowLayout->activate();
    m_optionLayout->activate();
    // The option bar does not page: when it is longer than the room, its
    // colours give up swatches (they scroll) until it fits.
    // The room is worked out from the rest of the bar, so a second pass
    // asks for the same and changes nothing (no layout request loop).
    SubToolbar* options = m_optionWanted ? optionsForTool(m_currentTool) : nullptr;
    if (auto* colors = options ? options->findChild<ColorListRow*>() : nullptr) {
        const auto measure = [this, options, colors]() {
            // Inner layouts first: their size hints are cached until activated.
            colors->layout()->activate();
            options->layout()->activate();
            m_optionLayout->activate();
        };
        measure();
        const QSize bar = m_optionBar->sizeHint();
        const QSize own = colors->sizeHint();
        const int rest = isVertical() ? bar.height() - own.height() : bar.width() - own.width();
        colors->fitTo(m_availableLength - rest);
        measure();
    }
    const QSize size = sizeHint().boundedTo(inner.size());

    QPoint at;
    switch (m_edge) {
    case Edge::Top:
        at = QPoint(inner.center().x() - size.width() / 2, inner.top());
        break;
    case Edge::Bottom:
        at = QPoint(inner.center().x() - size.width() / 2, inner.bottom() + 1 - size.height());
        break;
    case Edge::Left:
        at = QPoint(inner.left(), inner.center().y() - size.height() / 2);
        break;
    case Edge::Right:
        at = QPoint(inner.right() + 1 - size.width(), inner.center().y() - size.height() / 2);
        break;
    }

    resize(size);
    if (m_posAnimation && m_posAnimation->state() == QAbstractAnimation::Running) {
        m_posAnimation->setEndValue(at);  // A layout pass during the slide moves its end
    } else if (animate && m_animations && isVisible() && pos() != at) {
        if (!m_posAnimation) {
            m_posAnimation = new QPropertyAnimation(this, "pos", this);
            m_posAnimation->setDuration(150);
            m_posAnimation->setEasingCurve(QEasingCurve::OutCubic);
        }
        m_posAnimation->setStartValue(pos());
        m_posAnimation->setEndValue(at);
        m_posAnimation->start();
    } else {
        move(at);
    }
    updateFloatingMask();

    const QRect target(at, size);
    if (target != m_lastTarget) {
        m_lastTarget = target;
        emit placementChanged();
    }
}

void Toolbar::updateFloatingMask()
{
    if (!m_floating) {
        clearMask();
        return;
    }
    // Clicks beside a short pill reach the page, not this widget.
    QRegion region(m_row->geometry());
    if (m_optionBar->isVisibleTo(this))
        region += m_optionBar->geometry();
    setMask(region);
}

void Toolbar::updateGripIcon()
{
    const bool vertical = isVertical();
    const QString path = QStringLiteral(":/resources/icons/%1%2.png")
        .arg(vertical ? QStringLiteral("grip_horizontal") : QStringLiteral("grip"),
             m_darkMode ? QStringLiteral("_reversed") : QString());
    m_grip->setPixmap(QIcon(path).pixmap(20, 20));
    m_grip->setFixedSize(vertical ? QSize(36, 20) : QSize(20, 36));
}

bool Toolbar::handleGripEvent(QEvent *event)
{
    QWidget* area = parentWidget();
    if (!m_floating || !area)
        return false;

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() != Qt::LeftButton)
            return false;
        if (m_posAnimation) m_posAnimation->stop();
        m_dragging = true;
        m_dragOffset = area->mapFromGlobal(SN_MOUSE_GLOBAL_POS(mouseEvent)) - pos();
        return true;
    }
    case QEvent::MouseMove: {
        if (!m_dragging)
            return false;
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint at = area->mapFromGlobal(SN_MOUSE_GLOBAL_POS(mouseEvent)) - m_dragOffset;
        at.setX(qBound(0, at.x(), area->width() - width()));
        at.setY(qBound(0, at.y(), area->height() - height()));
        move(at);
        return true;
    }
    case QEvent::MouseButtonRelease: {
        if (!m_dragging)
            return false;
        m_dragging = false;
        // Snap to the nearest edge, never the middle, and remember it.
        const Edge edge = nearestEdge(geometry().center() - m_area.topLeft(), m_area.size());
        QSettings("SpeedyNote", "App").setValue("toolbar/position", edgeName(edge));
        setPlacement(edge, true);
        placeFloating(m_area, true);
        return true;
    }
    default:
        return false;
    }
}

// ===== Theme and paging =====

void Toolbar::setTouchGestureMode(int mode)
{
    m_touchGestureButton->setState(mode);
}

void Toolbar::updateTheme(bool darkMode)
{
    m_darkMode = darkMode;

    QPalette sysPalette = QGuiApplication::palette();
    setAutoFillBackground(!m_floating);  // Floating: only the pills are painted
    QPalette pal = palette();
    pal.setColor(QPalette::Window, sysPalette.color(QPalette::Window));
    setPalette(pal);

    m_borderColor = darkMode ? QColor(0x4d, 0x4d, 0x4d) : QColor(0xD0, 0xD0, 0xD0);

    ButtonStyles::applyToWidget(this, darkMode);

    m_exitFullscreenButton->setDarkMode(darkMode);

    m_ocrExpandable->setDarkMode(darkMode);

    // Update subtoolbars
    m_penSubToolbar->setDarkMode(darkMode);
    m_markerSubToolbar->setDarkMode(darkMode);
    m_eraserSubToolbar->setDarkMode(darkMode);
    m_highlighterSubToolbar->setDarkMode(darkMode);
    m_shapeSubToolbar->setDarkMode(darkMode);
    m_laserSubToolbar->setDarkMode(darkMode);
    m_ocrSubToolbar->setDarkMode(darkMode);

    // Update plain buttons
    m_penButton->setDarkMode(darkMode);
    for (ToolButton* button : m_penTypeButtons) button->setDarkMode(darkMode);
    m_markerButton->setDarkMode(darkMode);
    m_eraserButton->setDarkMode(darkMode);
    m_highlighterButton->setDarkMode(darkMode);
    m_straightLineButton->setDarkMode(darkMode);
    m_lassoButton->setDarkMode(darkMode);
    m_shapeButton->setDarkMode(darkMode);
    m_laserButton->setDarkMode(darkMode);
    m_rulerButton->setDarkMode(darkMode);
    m_objectImageButton->setDarkMode(darkMode);
    m_objectLinkButton->setDarkMode(darkMode);
    m_objectTextButton->setDarkMode(darkMode);
    m_panButton->setDarkMode(darkMode);
    m_undoButton->setDarkMode(darkMode);
    m_redoButton->setDarkMode(darkMode);
    m_touchGestureButton->setDarkMode(darkMode);
    m_pagerBackButton->setDarkMode(darkMode);
    m_pagerNextButton->setDarkMode(darkMode);
    updateGripIcon();

    update();
}

int Toolbar::groupLength(const QVector<QWidget*>& widgets) const
{
    const bool vertical = isVertical();
    int total = 0;
    int count = 0;
    for (QWidget* widget : widgets) {
        if (!widget)
            continue;
        // What the layout gives it: the hint, kept within a fixed size.
        const QSize size = widget->sizeHint().expandedTo(widget->minimumSize())
                                             .boundedTo(widget->maximumSize());
        total += vertical ? size.height() : size.width();
        ++count;
    }
    if (count > 1)
        total += m_rowLayout->spacing() * (count - 1);
    return total;
}

int Toolbar::naturalLength() const
{
    const QMargins margins = m_rowLayout->contentsMargins();
    QVector<QWidget*> extras;
    if (!m_floating && m_optionWanted) extras << m_optionBar;   // Part of page 1
    if (m_floating) extras << m_grip;
    if (!m_exitFullscreenButton->isHidden()) extras << m_exitFullscreenButton;
    const int extrasLength = extras.isEmpty() ? 0 : m_rowLayout->spacing() + groupLength(extras);

    return (isVertical() ? margins.top() + margins.bottom() : margins.left() + margins.right())
        + groupLength(m_page1Widgets)
        + m_rowLayout->spacing()
        + groupLength(m_page2Widgets)
        + extrasLength;
}

QSize Toolbar::minimumSizeHint() const
{
    if (m_floating)
        return QWidget::minimumSizeHint();

    // The inherited hint is the whole row, which pins the window wide enough
    // that it can never shrink far enough to trigger paging in the first
    // place. What the toolbar actually needs is the wider single page plus one
    // pager, since only one page and one pager are ever on screen at once.
    const QMargins margins = m_rowLayout->contentsMargins();
    QVector<QWidget*> page1 = m_page1Widgets;
    if (m_optionWanted) page1 << m_optionBar;
    int widest = qMax(groupLength(page1), groupLength(m_page2Widgets));
    int pager = 0;
    if (m_pagerNextButton)
        pager = m_pagerNextButton->sizeHint().width() + m_rowLayout->spacing();

    return QSize(margins.left() + margins.right() + widest + pager,
                 TOOLBAR_HEIGHT);
}

void Toolbar::updatePagination()
{
    if (m_updatingPagination)
        return;

    const int natural = naturalLength();
    const int available = m_floating ? m_availableLength : width();

    // The measurement covers both groups whatever is on screen, so hiding a
    // group cannot feed back into the decision. The exit band only keeps the
    // boundary from fluttering by a pixel during a drag-resize.
    bool paged = m_paged;
    if (!m_paged && natural > available)
        paged = true;
    else if (m_paged && natural + PAGING_HYSTERESIS <= available)
        paged = false;

    if (paged == m_paged)
        return;

    m_updatingPagination = true;
    m_paged = paged;
    if (!m_paged)
        m_currentPage = 0;
    applyPageVisibility();
    m_updatingPagination = false;
    updateGeometry();
}

void Toolbar::applyPageVisibility()
{
    for (QWidget* widget : m_page1Widgets) {
        if (widget)
            widget->setVisible(!m_paged || m_currentPage == 0);
    }
    for (QWidget* widget : m_page2Widgets) {
        if (widget)
            widget->setVisible(!m_paged || m_currentPage == 1);
    }
    // Docked, the option bar is part of page 1; floating, it is its own pill.
    m_optionBar->setVisible(m_optionWanted && (m_floating || !m_paged || m_currentPage == 0));

    if (m_pagerBackButton)
        m_pagerBackButton->setVisible(m_paged && m_currentPage == 1);
    if (m_pagerNextButton)
        m_pagerNextButton->setVisible(m_paged && m_currentPage == 0);
}

void Toolbar::setToolbarPage(int page)
{
    if (!m_paged || page == m_currentPage)
        return;

    m_updatingPagination = true;
    m_currentPage = page;
    applyPageVisibility();
    m_updatingPagination = false;
}

void Toolbar::revealWidget(QWidget* widget)
{
    if (!m_paged || !widget)
        return;

    if (m_page2Widgets.contains(widget))
        setToolbarPage(1);
    else if (m_page1Widgets.contains(widget))
        setToolbarPage(0);
}

void Toolbar::installSwipeFilter(QWidget* root)
{
    if (!root)
        return;
    root->installEventFilter(this);
    for (QObject* child : root->children()) {
        if (auto* childWidget = qobject_cast<QWidget*>(child))
            installSwipeFilter(childWidget);
    }
}

bool Toolbar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_grip && handleGripEvent(event))
        return true;

    // A swipe only means something when there is a second page to reach.
    if (!m_paged)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        // Touch arrives here as a synthesized mouse press, the same way the
        // launcher lists read it. Restricting to touch keeps a mouse user who
        // drags off a button from flipping the page by accident; they have the
        // pager button.
        if (mouseEvent->button() != Qt::LeftButton
            || !KineticScrollHelper::isTouchInput(mouseEvent)) {
            break;
        }
        m_swipeStart = mapFromGlobal(SN_MOUSE_GLOBAL_POS(mouseEvent));
        m_swipeTracking = true;
        m_swipeConsumed = false;
        break;
    }
    case QEvent::MouseMove: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (!m_swipeTracking || m_swipeConsumed
            || !(mouseEvent->buttons() & Qt::LeftButton)) {
            break;
        }
        const QPoint delta =
            mapFromGlobal(SN_MOUSE_GLOBAL_POS(mouseEvent)) - m_swipeStart;
        // Along the row: sideways for a horizontal bar, up and down for a vertical one.
        const int along = isVertical() ? delta.y() : delta.x();
        const int across = isVertical() ? delta.x() : delta.y();
        if (qAbs(along) < SWIPE_THRESHOLD || qAbs(along) <= 2 * qAbs(across)) {
            break;
        }

        m_swipeConsumed = true;
        setToolbarPage(along < 0 ? 1 : 0);
        // The press landed on a button, which would otherwise fire on release.
        if (auto* button = qobject_cast<QAbstractButton*>(watched))
            button->setDown(false);
        return true;
    }
    case QEvent::MouseButtonRelease: {
        const bool consumed = m_swipeConsumed;
        m_swipeTracking = false;
        m_swipeConsumed = false;
        if (consumed)
            return true;
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void Toolbar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updatePagination();
    updateFloatingMask();
}

bool Toolbar::event(QEvent *event)
{
    // Fires whenever a child's size hint changes, which is how an opening
    // option bar or OCR strip reaches the width check without every path
    // calling it.
    if (event->type() == QEvent::LayoutRequest) {
        if (m_floating)
            placeFloating(m_area);
        else
            updatePagination();
    }
    return QWidget::event(event);
}

void Toolbar::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    if (m_floating) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        paintPill(painter, m_row->geometry(), m_darkMode);
        if (m_optionBar->isVisible())
            paintPill(painter, m_optionBar->geometry(), m_darkMode);
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.setPen(QPen(m_borderColor, 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);

    QColor innerShadow = m_darkMode ? QColor(0, 0, 0, 30) : QColor(0, 0, 0, 15);
    painter.setPen(QPen(innerShadow, 1));
    painter.drawLine(0, height() - 2, width(), height() - 2);

    // The docked option bar keeps the card the expanded tool button had.
    if (m_optionBar->isVisible()) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRect options(m_optionBar->mapTo(this, QPoint(0, 0)), m_optionBar->size());
        paintPill(painter, options.adjusted(-2, -2, 2, 2), m_darkMode);
    }
}

void Toolbar::setUndoEnabled(bool enabled)
{
    m_undoButton->setEnabled(enabled);
}

void Toolbar::setRedoEnabled(bool enabled)
{
    m_redoButton->setEnabled(enabled);
}

void Toolbar::setStraightLineMode(bool enabled)
{
    m_straightLineButton->blockSignals(true);
    m_straightLineButton->setChecked(enabled);
    m_straightLineButton->blockSignals(false);
}

void Toolbar::setRulerChecked(bool visible)
{
    const QSignalBlocker block(m_rulerButton);
    m_rulerButton->setChecked(visible);
}

void Toolbar::onTabChanged(int newTabId, int oldTabId)
{
    // Save state for old tab across all subtoolbars
    if (oldTabId >= 0) {
        m_penSubToolbar->saveTabState(oldTabId);
        m_markerSubToolbar->saveTabState(oldTabId);
        m_highlighterSubToolbar->saveTabState(oldTabId);
        m_eraserSubToolbar->saveTabState(oldTabId);
        m_ocrSubToolbar->saveTabState(oldTabId);
    }

    // Restore state for new tab across all subtoolbars
    if (newTabId >= 0) {
        m_penSubToolbar->restoreTabState(newTabId);
        m_markerSubToolbar->restoreTabState(newTabId);
        m_highlighterSubToolbar->restoreTabState(newTabId);
        m_eraserSubToolbar->restoreTabState(newTabId);
        m_ocrSubToolbar->restoreTabState(newTabId);
    }
}

void Toolbar::clearTabState(int tabId)
{
    m_penSubToolbar->clearTabState(tabId);
    m_markerSubToolbar->clearTabState(tabId);
    m_highlighterSubToolbar->clearTabState(tabId);
    m_eraserSubToolbar->clearTabState(tabId);
    m_ocrSubToolbar->clearTabState(tabId);
}

void Toolbar::setOcrAvailable(bool available)
{
    // Always keep the expandable trigger enabled so users can access cached
    // OCR text even on platforms without an OCR engine.
    m_ocrExpandable->toolButton()->setEnabled(true);
    m_ocrSubToolbar->setOcrAvailable(available);
    if (!available) {
        m_ocrExpandable->toolButton()->setToolTip(
            tr("OCR - View cached text (engine unavailable on this platform)"));
    } else {
        m_ocrExpandable->toolButton()->setToolTip(tr("OCR - Text Recognition"));
    }
}

void Toolbar::setOcrConfidenceSupported(bool supported)
{
    m_ocrSubToolbar->setConfidenceSupported(supported);
}
