#include "EraserSubToolbar.h"
#include "../widgets/ThicknessPresetButton.h"
#include "../widgets/IconPresetButton.h"
#include "../../compat/qt_compat.h"

#include <QActionGroup>
#include <QApplication>
#include <QBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QToolButton>
#include <QWidgetAction>

QPixmap eraserModePicture(int mode, bool dark)
{
    // A 44 x 28 drawing: the line in grey, the part this mode removes in red,
    // and the eraser (a small disc) or the loop as a dashed outline.
    const QSize size(44, 28);
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap picture(size * dpr);
    picture.setDevicePixelRatio(dpr);
    picture.fill(Qt::transparent);

    QPainter painter(&picture);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor kept = dark ? QColor(175, 175, 175) : QColor(105, 105, 105);
    const QPen keptPen(kept, 3, Qt::SolidLine, Qt::RoundCap);
    const QPen gonePen(QColor(220, 60, 50), 3, Qt::SolidLine, Qt::RoundCap);

    QPainterPath line(QPointF(4, 20));
    line.cubicTo(QPointF(14, 2), QPointF(28, 30), QPointF(40, 8));

    const bool lasso = DocumentViewport::eraserModeIsLasso(
        static_cast<DocumentViewport::EraserMode>(mode));
    const bool partial = mode == static_cast<int>(DocumentViewport::EraserMode::Partial)
                         || mode == static_cast<int>(DocumentViewport::EraserMode::LassoPartial);
    QPainterPath reach;
    if (lasso) {
        reach.addEllipse(QRectF(11, 3, 21, 22));
    } else {
        reach.addEllipse(QRectF(16, 10.5, 10, 10));  // On the middle of the line
    }

    if (partial) {
        painter.strokePath(line, keptPen);
        painter.setClipPath(reach);
        painter.strokePath(line, gonePen);
        painter.setClipping(false);
    } else {
        painter.strokePath(line, gonePen);
    }
    painter.setPen(QPen(kept, 1.2, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(reach);
    return picture;
}

namespace {

// One entry of the mode list: a picture, the mode in words, one line of help.
// A plain menu entry has room for one line of text only.
class ModeRow : public QWidget {
public:
    ModeRow(QWidgetAction* action, const QString& help, QWidget* parent)
        : QWidget(parent), m_action(action)
    {
        setAttribute(Qt::WA_Hover);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 6, 14, 6);
        layout->setSpacing(10);
        m_picture = new QLabel(this);
        layout->addWidget(m_picture);

        auto* words = new QVBoxLayout;
        words->setSpacing(1);
        auto* title = new QLabel(action->text(), this);
        QFont bold = title->font();
        bold.setBold(true);
        title->setFont(bold);
        auto* hint = new QLabel(help, this);
        hint->setStyleSheet(QStringLiteral("color: gray;"));
        words->addWidget(title);
        words->addWidget(hint);
        layout->addLayout(words, 1);
    }

    void setPicture(const QPixmap& picture) { m_picture->setPixmap(picture); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const bool checked = m_action->isChecked();
        if (!checked && !underMouse()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        QColor fill = palette().color(QPalette::Highlight);
        fill.setAlpha(checked ? 70 : 35);
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(rect()).adjusted(3, 1, -3, -1), 6, 6);
    }

    // The row takes the click itself (a pen tap arrives as a mouse click) and
    // closes the list, which a widget entry does not do on its own.
    void mousePressEvent(QMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (!rect().contains(SN_MOUSE_POS(event).toPoint())) return;
        m_action->trigger();
        if (auto* menu = qobject_cast<QMenu*>(parentWidget())) menu->close();
    }

private:
    QWidgetAction* m_action;
    QLabel* m_picture = nullptr;
};

} // namespace

// Static member definitions
const QColor EraserSubToolbar::PREVIEW_COLOR = QColor(0x80, 0x80, 0x80);  // Gray

const QString EraserSubToolbar::SETTINGS_GROUP = "eraser";
const QString EraserSubToolbar::KEY_ERASER_MODE = "eraserMode";
const QString EraserSubToolbar::KEY_MODE_VERSION = "eraserModeVersion";
const QString EraserSubToolbar::KEY_SLOT_WHOLE = "whole";
const QString EraserSubToolbar::KEY_SLOT_PARTIAL = "partial";
const QString EraserSubToolbar::KEY_SIZE_PREFIX = "size";
const QString EraserSubToolbar::KEY_SELECTED_SIZE = "selectedSize";

EraserSubToolbar::EraserSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    createWidgets();
    setupConnections();
    loadFromSettings();
}

void EraserSubToolbar::createWidgets()
{
    // One button that names the mode in words, rather than four icons the user
    // had to tell apart. Its list shows every mode with a picture and one line
    // of help, so the alternatives stay visible.
    const QString titles[NUM_MODES] = {
        tr("The whole line"),
        tr("Only what I touch"),
        tr("All lines in a loop"),
        tr("What I circle")
    };
    const QString helps[NUM_MODES] = {
        tr("Touch a line once to erase all of it"),
        tr("Erase only the part of a line you rub over"),
        tr("Draw a loop: every line it touches goes"),
        tr("Draw a loop: lines are cut at its edge")
    };

    m_modeButton = new QToolButton(this);
    m_modeButton->setPopupMode(QToolButton::InstantPopup);
    m_modeButton->setAutoRaise(true);
    m_modeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_modeButton->setIconSize(QSize(33, 21));
    m_modeButton->setToolTip(tr("Eraser mode"));
    auto* menu = new QMenu(m_modeButton);
    auto* group = new QActionGroup(menu);  // Exclusive: one mode is checked
    for (int i = 0; i < NUM_MODES; ++i) {
        auto* action = new QWidgetAction(menu);
        action->setText(titles[i]);
        action->setCheckable(true);
        group->addAction(action);
        action->setDefaultWidget(new ModeRow(action, helps[i], menu));
        menu->addAction(action);
        m_modeActions[i] = action;
    }
    m_modeButton->setMenu(menu);
    addWidget(m_modeButton);

    // The eraser window (filter, auto deselect, clear the page). A second tap
    // on the eraser hides this bar (or, with that setting off, opens the window).
    m_optionsButton = new IconPresetButton(this);
    m_optionsButton->setIconName(QStringLiteral("settings"));
    m_optionsButton->setToolTip(tr("Eraser options"));
    addWidget(m_optionsButton);

    m_sizeSeparator = addSeparator();

    // Create size preset buttons
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_sizeButtons[i] = new ThicknessPresetButton(this);
        m_sizeButtons[i]->setThickness(DEFAULT_SIZES[0][i]);
        m_sizeButtons[i]->setLineColor(PREVIEW_COLOR);
        m_sizeButtons[i]->setToolTip(tr("Eraser size preset %1 (click to select, click again to edit)").arg(i + 1));
        addWidget(m_sizeButtons[i]);
    }

    setDarkMode(isDarkMode());  // Draws the pictures
}

void EraserSubToolbar::setupConnections()
{
    for (int i = 0; i < NUM_MODES; ++i) {
        connect(m_modeActions[i], &QAction::triggered, this, [this, i]() { pickMode(i); });
    }
    connect(m_optionsButton, &IconPresetButton::clicked, this, &EraserSubToolbar::optionsRequested);

    // Size button connections
    for (int i = 0; i < NUM_PRESETS; ++i) {
        connect(m_sizeButtons[i], &ThicknessPresetButton::clicked, this, [this, i]() {
            onSizePresetClicked(i);
        });
        connect(m_sizeButtons[i], &ThicknessPresetButton::editRequested, this, [this, i]() {
            onSizeEditRequested(i);
        });
    }
}

int EraserSubToolbar::sizeSlotForMode(int mode)
{
    const auto eraserMode = static_cast<DocumentViewport::EraserMode>(mode);
    // Only disc-based modes own a size; the viewport decides which those are.
    if (!DocumentViewport::eraserModeUsesDisc(eraserMode)) {
        return -1;
    }
    return eraserMode == DocumentViewport::EraserMode::Partial ? 1 : 0;
}

void EraserSubToolbar::flushButtonsToActiveSlot()
{
    const int slot = activeSizeSlot();
    if (slot < 0) {
        return;   // Lasso: the buttons hold the last disc-based values already.
    }
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_sizeSets[slot].sizes[i] = m_sizeButtons[i]->thickness();
    }
    m_sizeSets[slot].selected = m_selectedSizeIndex;
}

void EraserSubToolbar::loadSizeSet(int slot)
{
    if (slot < 0 || slot >= NUM_SIZE_SETS) {
        return;
    }
    const SizeSet& set = m_sizeSets[slot];
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_sizeButtons[i]->setThickness(set.sizes[i]);
    }
    selectSizePreset(qBound(0, set.selected, NUM_PRESETS - 1));
}

void EraserSubToolbar::loadFromSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);

    // Start from the built-in defaults, then overlay whatever is on disk.
    for (int slot = 0; slot < NUM_SIZE_SETS; ++slot) {
        for (int i = 0; i < NUM_PRESETS; ++i) {
            m_sizeSets[slot].sizes[i] = DEFAULT_SIZES[slot][i];
        }
        m_sizeSets[slot].selected = 1;
    }

    // Which settings layout produced the stored values. Builds before the
    // per-mode split kept one shared set plus one mode number.
    const int version = settings.value(KEY_MODE_VERSION, 1).toInt();

    if (version < 3) {
        // Legacy: the shared presets belonged to the whole-stroke eraser. The
        // partial eraser is new enough that its defaults are a better seed
        // than the whole-stroke values, which were tuned for a different job.
        for (int i = 0; i < NUM_PRESETS; ++i) {
            const QString key = KEY_SIZE_PREFIX + QString::number(i + 1);
            const qreal size = settings.value(key, DEFAULT_SIZES[0][i]).toReal();
            m_sizeSets[0].sizes[i] = qBound(MIN_SIZE, size, MAX_SIZE);
        }
        m_sizeSets[0].selected =
            qBound(0, settings.value(KEY_SELECTED_SIZE, 1).toInt(), NUM_PRESETS - 1);
    } else {
        const QString groups[NUM_SIZE_SETS] = {KEY_SLOT_WHOLE, KEY_SLOT_PARTIAL};
        for (int slot = 0; slot < NUM_SIZE_SETS; ++slot) {
            settings.beginGroup(groups[slot]);
            for (int i = 0; i < NUM_PRESETS; ++i) {
                const QString key = KEY_SIZE_PREFIX + QString::number(i + 1);
                const qreal size = settings.value(key, DEFAULT_SIZES[slot][i]).toReal();
                m_sizeSets[slot].sizes[i] = qBound(MIN_SIZE, size, MAX_SIZE);
            }
            m_sizeSets[slot].selected =
                qBound(0, settings.value(KEY_SELECTED_SIZE, 1).toInt(), NUM_PRESETS - 1);
            settings.endGroup();
        }
    }

    // Load eraser mode. Version 1 stored Lasso as 1, before Partial existed, so
    // an old "1" must not be reinterpreted as Partial.
    const int storedMode = settings.value(KEY_ERASER_MODE, 0).toInt();
    if (version < 2) {
        m_eraserModeIndex = (storedMode == 1)
            ? static_cast<int>(DocumentViewport::EraserMode::Lasso)
            : static_cast<int>(DocumentViewport::EraserMode::Normal);
    } else {
        m_eraserModeIndex = qBound(0, storedMode, NUM_MODES - 1);
    }

    settings.endGroup();

    // The buttons always carry a disc-based set, even when the saved mode is
    // lasso: currentSize() stays meaningful and switching back is instant.
    const int slot = activeSizeSlot();
    loadSizeSet(slot >= 0 ? slot : 0);
    applyModePresentation();
}

void EraserSubToolbar::saveToSettings()
{
    // The buttons are the source of truth for the visible mode, so fold them
    // into that mode's set before writing anything.
    flushButtonsToActiveSlot();

    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);

    const QString groups[NUM_SIZE_SETS] = {KEY_SLOT_WHOLE, KEY_SLOT_PARTIAL};
    for (int slot = 0; slot < NUM_SIZE_SETS; ++slot) {
        settings.beginGroup(groups[slot]);
        for (int i = 0; i < NUM_PRESETS; ++i) {
            const QString key = KEY_SIZE_PREFIX + QString::number(i + 1);
            settings.setValue(key, m_sizeSets[slot].sizes[i]);
        }
        settings.setValue(KEY_SELECTED_SIZE, m_sizeSets[slot].selected);
        settings.endGroup();
    }

    settings.setValue(KEY_ERASER_MODE, m_eraserModeIndex);
    settings.setValue(KEY_MODE_VERSION, 3);

    settings.endGroup();
}

void EraserSubToolbar::saveSelectionToSettings()
{
    // Write ONLY the active mode's selected-size index: cheap enough to call on
    // every preset click, and it leaves the inactive mode's presets untouched.
    const int slot = activeSizeSlot();
    if (slot < 0) {
        return;
    }

    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.beginGroup(slot == 0 ? KEY_SLOT_WHOLE : KEY_SLOT_PARTIAL);
    settings.setValue(KEY_SELECTED_SIZE, m_selectedSizeIndex);
    settings.endGroup();
    settings.endGroup();
}

void EraserSubToolbar::refreshFromSettings()
{
    loadFromSettings();
}

void EraserSubToolbar::emitCurrentValues()
{
    // Emit the currently selected preset value to sync with viewport
    if (m_selectedSizeIndex >= 0 && m_selectedSizeIndex < NUM_PRESETS) {
        emit eraserSizeChanged(m_sizeButtons[m_selectedSizeIndex]->thickness());
    }
    emit eraserModeChanged(m_eraserModeIndex);
}

int EraserSubToolbar::currentModeIndex() const
{
    return m_eraserModeIndex;
}

void EraserSubToolbar::setModeState(int mode)
{
    mode = qBound(0, mode, NUM_MODES - 1);
    if (mode != m_eraserModeIndex) {
        flushButtonsToActiveSlot();
        m_eraserModeIndex = mode;
        const int slot = activeSizeSlot();
        if (slot >= 0) {
            loadSizeSet(slot);
        }
    }
    applyModePresentation();
}

void EraserSubToolbar::pickMode(int mode)
{
    if (m_eraserModeIndex == mode) {
        return;
    }
    // Keep any edits made to the outgoing mode's presets before the
    // buttons are repurposed for the incoming mode.
    flushButtonsToActiveSlot();
    m_eraserModeIndex = mode;

    const int slot = activeSizeSlot();
    if (slot >= 0) {
        loadSizeSet(slot);
    }
    applyModePresentation();
    saveToSettings();

    emit eraserModeChanged(mode);
    // The disc needs a radius again as soon as a disc-based mode is
    // back; the viewport may have been left on a lasso cycle.
    if (slot >= 0) {
        emit eraserSizeChanged(currentSize());
    }
}

void EraserSubToolbar::updateModeButtonSelection()
{
    QWidgetAction* current = m_modeActions[m_eraserModeIndex];
    current->setChecked(true);
    m_modeButton->setText(current->text());
    m_modeButton->setIcon(QIcon(eraserModePicture(m_eraserModeIndex, m_darkMode)));
}

void EraserSubToolbar::setDarkMode(bool darkMode)
{
    SubToolbar::setDarkMode(darkMode);
    m_darkMode = darkMode;
    m_optionsButton->setDarkMode(darkMode);
    for (int i = 0; i < NUM_MODES; ++i) {
        static_cast<ModeRow*>(m_modeActions[i]->defaultWidget())
            ->setPicture(eraserModePicture(i, darkMode));
    }
    updateModeButtonSelection();
}

void EraserSubToolbar::setOrientation(Qt::Orientation orientation)
{
    // A vertical bar is 36 px wide: room for the picture only.
    const bool row = orientation == Qt::Horizontal;
    m_modeButton->setToolButtonStyle(row ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    m_modeButton->setIconSize(row ? QSize(33, 21) : QSize(26, 17));
    SubToolbar::setOrientation(orientation);
}

void EraserSubToolbar::applyModePresentation()
{
    updateModeButtonSelection();

    // Lasso removes everything inside the drawn region, so it has no size;
    // showing a size row there would offer a control that changes nothing.
    const bool showSizes = activeSizeSlot() >= 0;
    for (int i = 0; i < NUM_PRESETS; ++i) {
        if (m_sizeButtons[i]) {
            m_sizeButtons[i]->setVisible(showSizes);
        }
    }
    if (m_sizeSeparator) {
        m_sizeSeparator->setVisible(showSizes);
    }
}

qreal EraserSubToolbar::currentSize() const
{
    if (m_selectedSizeIndex >= 0 && m_selectedSizeIndex < NUM_PRESETS && m_sizeButtons[m_selectedSizeIndex]) {
        return m_sizeButtons[m_selectedSizeIndex]->thickness();
    }
    return DEFAULT_SIZES[0][1];  // Fallback to medium default
}

void EraserSubToolbar::restoreTabState(int tabIndex)
{
    if (!m_tabStates.contains(tabIndex) || !m_tabStates[tabIndex].initialized) {
        // No saved state for this tab - use current (global) values
        return;
    }
    
    const TabState& state = m_tabStates[tabIndex];
    
    // Both modes' presets travel with the tab, not just the visible one.
    for (int slot = 0; slot < NUM_SIZE_SETS; ++slot) {
        m_sizeSets[slot] = state.sets[slot];
    }

    const int slot = activeSizeSlot();
    loadSizeSet(slot >= 0 ? slot : 0);
    applyModePresentation();
}

void EraserSubToolbar::saveTabState(int tabIndex)
{
    flushButtonsToActiveSlot();

    TabState& state = m_tabStates[tabIndex];
    
    for (int slot = 0; slot < NUM_SIZE_SETS; ++slot) {
        state.sets[slot] = m_sizeSets[slot];
    }
    state.initialized = true;
}

void EraserSubToolbar::clearTabState(int tabIndex)
{
    m_tabStates.remove(tabIndex);
}

void EraserSubToolbar::cycleSize()
{
    // Lasso has no size to cycle through.
    if (activeSizeSlot() < 0) {
        return;
    }
    int start = qMax(0, m_selectedSizeIndex);
    onSizePresetClicked((start + 1) % NUM_PRESETS);
}

void EraserSubToolbar::onSizePresetClicked(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;

    // Always apply the size when clicked - the preset might show as "selected"
    // but the actual current size could be different (changed via other means)
    const bool indexChanged = (m_selectedSizeIndex != index);
    selectSizePreset(index);

    // Keep the mode's own set in step, so switching modes and back does not
    // resurrect the old selection.
    const int slot = activeSizeSlot();
    if (slot >= 0) {
        m_sizeSets[slot].selected = index;
    }

    // Emit size change
    emit eraserSizeChanged(m_sizeButtons[index]->thickness());

    // Persist the new selection so it survives an app restart. Guarded on
    // indexChanged so spam-clicking the same preset does not write QSettings
    // every click; the re-emit above still fires unconditionally to handle
    // the case where the actual current size drifted via another path.
    if (indexChanged) saveSelectionToSettings();
}

void EraserSubToolbar::onSizeEditRequested(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;
    
    // Open size edit dialog with the eraser's own range
    ThicknessEditDialog dialog(m_sizeButtons[index]->thickness(), MIN_SIZE, MAX_SIZE,
                               /*currentMinWidth=*/-1.0, this);
    dialog.setWindowTitle(tr("Edit Eraser Size"));
    
    if (dialog.exec() == QDialog::Accepted) {
        qreal newSize = dialog.thickness();
        m_sizeButtons[index]->setThickness(newSize);
        saveToSettings();  // Persist change (flushes the buttons into the set)
        
        // If this is the selected preset, emit change
        if (m_selectedSizeIndex == index) {
            emit eraserSizeChanged(newSize);
        }
    }
}

void EraserSubToolbar::selectSizePreset(int index)
{
    // Allow index = -1 for "no selection"
    if (index < -1 || index >= NUM_PRESETS) return;
    
    // Update selection state
    for (int i = 0; i < NUM_PRESETS; ++i) {
        m_sizeButtons[i]->setSelected(i == index);
    }
    
    m_selectedSizeIndex = index;
}
