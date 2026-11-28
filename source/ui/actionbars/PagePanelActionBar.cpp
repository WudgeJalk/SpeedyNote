#include "../sidebars/PanelFooter.h"
#include "PagePanelActionBar.h"
#include "../widgets/ActionBarButton.h"
#include "../widgets/PageWheelPicker.h"
#include "../widgets/UndoDeleteButton.h"

#include <QFrame>
#include <QHBoxLayout>

PagePanelActionBar::PagePanelActionBar(QWidget* parent)
    : ActionBar(parent)
{
    setupUI();
    setupConnections();
    updateButtonStates();
}

void PagePanelActionBar::setupUI()
{
    // === Lock Button ===
    m_lockButton = new ActionBarButton(this);
    m_lockButton->setIconName("lock");
    m_lockButton->setToolTip(tr("Lock/Unlock Page Panel Action Bar"));
    m_lockButton->setCheckable(true);
    addButton(m_lockButton);
    
    // === Navigation Section ===
    
    // Search button (PDF text search, Ctrl+F)
    m_searchButton = new ActionBarButton(this);
    m_searchButton->setIconName("zoom");
    m_searchButton->setToolTip(tr("Find in Document (Ctrl+F)"));
    addButton(m_searchButton);
    
    // Page Up button
    m_pageUpButton = new ActionBarButton(this);
    m_pageUpButton->setIconName("up_arrow");
    m_pageUpButton->setToolTip(tr("Previous Page (Page Up)"));
    addButton(m_pageUpButton);
    
    // Page wheel picker
    m_wheelPicker = new PageWheelPicker(this);
    m_wheelPicker->setToolTip(
        tr("Scroll or drag to change pages\nDouble-click or double-tap to jump to a page"));
    addButton(m_wheelPicker);
    
    // Page Down button
    m_pageDownButton = new ActionBarButton(this);
    m_pageDownButton->setIconName("down_arrow");
    m_pageDownButton->setToolTip(tr("Next Page (Page Down)"));
    addButton(m_pageDownButton);
    
    // Layout toggle button (1-column / Auto) - right below page down
    m_layoutToggleButton = new ActionBarButton(this);
    m_layoutToggleButton->setText("1");  // Default: 1-column mode
    m_layoutToggleButton->setToolTip(tr("Toggle Column Layout (Ctrl+2)\n1 = Single Column\nA = Auto 1/2 Columns"));
    addButton(m_layoutToggleButton);
    
    // Separator between navigation and management
    addSeparator();
    
    // === Page Management Section ===
    
    // Select (multi-select mode) toggle
    m_selectButton = new ActionBarButton(this);
    m_selectButton->setIconName("select");
    m_selectButton->setToolTip(tr("Select Multiple Pages"));
    m_selectButton->setCheckable(true);
    addButton(m_selectButton);
    
    // Add Page button
    m_addPageButton = new ActionBarButton(this);
    m_addPageButton->setIconName("addtab");
    m_addPageButton->setToolTip(tr("Add Page at End"));
    addButton(m_addPageButton);
    
    // Insert Page button
    m_insertPageButton = new ActionBarButton(this);
    m_insertPageButton->setIconName("import");
    m_insertPageButton->setToolTip(tr("Insert Page After Current"));
    addButton(m_insertPageButton);

    m_addPdfPagesButton = new ActionBarButton(this);
    m_addPdfPagesButton->setIconName("pdf");
    m_addPdfPagesButton->setToolTip(tr("Add Pages from PDF..."));
    addButton(m_addPdfPagesButton);
    
    // Delete Page button (with undo support)
    m_deleteButton = new UndoDeleteButton(this);
    m_deleteButton->setToolTip(tr("Delete Current Page"));
    addButton(m_deleteButton);
}

void PagePanelActionBar::setupConnections()
{
    // Lock toggle
    connect(m_lockButton, &ActionBarButton::clicked, this, [this]() {
        m_locked = !m_locked;
        m_lockButton->setChecked(m_locked);
        emit lockChanged(m_locked);
    });
    
    // Search signal
    connect(m_searchButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::searchClicked);
    
    // Navigation signals
    connect(m_pageUpButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::pageUpClicked);
    
    connect(m_pageDownButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::pageDownClicked);
    
    connect(m_layoutToggleButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::layoutToggleClicked);
    
    // Select mode toggle
    connect(m_selectButton, &ActionBarButton::clicked, this, [this]() {
        m_selectMode = !m_selectMode;
        m_selectButton->setChecked(m_selectMode);
        emit selectModeToggled(m_selectMode);
    });
    
    connect(m_wheelPicker, &PageWheelPicker::currentPageChanged,
            this, &PagePanelActionBar::onWheelPageChanged);
    connect(m_wheelPicker, &PageWheelPicker::jumpToPageRequested,
            this, &PagePanelActionBar::jumpToPageRequested);
    
    // Page management signals
    connect(m_addPageButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::addPageClicked);
    
    connect(m_insertPageButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::insertPageClicked);

    connect(m_addPdfPagesButton, &ActionBarButton::clicked,
            this, &PagePanelActionBar::addPdfPagesClicked);
    
    // Delete button signals (3-way: request, confirm, undo)
    // Use direct signal-to-signal connections to avoid trivial wrapper slots
    connect(m_deleteButton, &UndoDeleteButton::deleteRequested,
            this, &PagePanelActionBar::deletePageClicked);
    
    connect(m_deleteButton, &UndoDeleteButton::deleteConfirmed,
            this, &PagePanelActionBar::deleteConfirmed);
    
    connect(m_deleteButton, &UndoDeleteButton::undoRequested,
            this, &PagePanelActionBar::undoDeleteClicked);
}

void PagePanelActionBar::setCurrentPage(int page)
{
    if (m_currentPage != page) {
        m_currentPage = page;
        
        // Update wheel picker without triggering signals
        m_wheelPicker->blockSignals(true);
        m_wheelPicker->setCurrentPage(page);
        m_wheelPicker->blockSignals(false);
        
        updateButtonStates();
    }
}

void PagePanelActionBar::setPageCount(int count)
{
    if (m_pageCount != count && count > 0) {
        m_pageCount = count;
        
        // Update wheel picker
        m_wheelPicker->setPageCount(count);
        
        // Clamp current page if necessary
        if (m_currentPage >= m_pageCount) {
            m_currentPage = m_pageCount - 1;
            m_wheelPicker->blockSignals(true);
            m_wheelPicker->setCurrentPage(m_currentPage);
            m_wheelPicker->blockSignals(false);
        }
        
        updateButtonStates();
    }
}

void PagePanelActionBar::setAutoLayoutEnabled(bool enabled)
{
    if (m_autoLayoutEnabled != enabled) {
        m_autoLayoutEnabled = enabled;
        // Update button text: "A" for auto, "1" for single column
        m_layoutToggleButton->setText(enabled ? "A" : "1");
    }
}

void PagePanelActionBar::updateButtonStates()
{
    // Page Up: disabled on first page
    m_pageUpButton->setEnabled(m_currentPage > 0);
    
    // Page Down: disabled on last page
    m_pageDownButton->setEnabled(m_currentPage < m_pageCount - 1);
    
    // Delete: disabled when only 1 page remains
    m_deleteButton->setEnabled(m_pageCount > 1);
    
    // Note: Add/Insert and Layout toggle are always enabled, no state updates needed
}

void PagePanelActionBar::setInPanel()
{
    m_inPanel = true;
    m_lockButton->hide();  // it kept the floating bar on screen; here the bar is part of the panel
    while (QLayoutItem* item = m_layout->takeAt(0)) delete item;  // the widgets stay our children
    for (QFrame* line : findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (line->frameShape() == QFrame::HLine || line->frameShape() == QFrame::VLine) line->hide();
    }
    m_layout->setContentsMargins(PanelFooter::Margin, PanelFooter::Margin, PanelFooter::Margin, PanelFooter::Margin);
    m_layout->setSpacing(PanelFooter::Spacing);
    auto addRow = [this](std::initializer_list<QWidget*> widgets) {
        auto* row = new QHBoxLayout();
        row->setSpacing(PanelFooter::Spacing);
        for (QWidget* widget : widgets) row->addWidget(widget, 0, Qt::AlignVCenter);
        row->addStretch();
        m_layout->addLayout(row);
    };
    addRow({m_searchButton, m_pageUpButton, m_wheelPicker, m_pageDownButton, m_layoutToggleButton});
    addRow({m_selectButton, m_addPageButton, m_insertPageButton, m_addPdfPagesButton, m_deleteButton});
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setupStyle();
}

void PagePanelActionBar::setupStyle()
{
    if (!m_inPanel) {
        ActionBar::setupStyle();
        return;
    }
    setGraphicsEffect(nullptr);
    PanelFooter::apply(this, isDarkMode());
}

bool PagePanelActionBar::isLocked() const
{
    return m_locked;
}

void PagePanelActionBar::setDarkMode(bool darkMode)
{
    // Call base class implementation
    ActionBar::setDarkMode(darkMode);
    
    // Propagate to all child widgets
    if (m_lockButton) {
        m_lockButton->setDarkMode(darkMode);
    }
    if (m_searchButton) {
        m_searchButton->setDarkMode(darkMode);
    }
    if (m_pageUpButton) {
        m_pageUpButton->setDarkMode(darkMode);
    }
    if (m_pageDownButton) {
        m_pageDownButton->setDarkMode(darkMode);
    }
    if (m_wheelPicker) {
        m_wheelPicker->setDarkMode(darkMode);
    }
    if (m_selectButton) {
        m_selectButton->setDarkMode(darkMode);
    }
    if (m_addPageButton) {
        m_addPageButton->setDarkMode(darkMode);
    }
    if (m_insertPageButton) {
        m_insertPageButton->setDarkMode(darkMode);
    }
    if (m_addPdfPagesButton) {
        m_addPdfPagesButton->setDarkMode(darkMode);
    }
    if (m_deleteButton) {
        m_deleteButton->setDarkMode(darkMode);
    }
    if (m_layoutToggleButton) {
        m_layoutToggleButton->setDarkMode(darkMode);
    }
}

void PagePanelActionBar::setSelectModeChecked(bool checked)
{
    if (m_selectMode == checked) {
        return;
    }
    m_selectMode = checked;
    if (m_selectButton) {
        m_selectButton->setChecked(checked);
    }
}

void PagePanelActionBar::resetDeleteButton()
{
    if (m_deleteButton) {
        m_deleteButton->reset();
    }
}

void PagePanelActionBar::confirmDelete()
{
    if (m_deleteButton) {
        m_deleteButton->confirmDelete();
    }
}

// ============================================================================
// Private Slots
// ============================================================================

void PagePanelActionBar::onWheelPageChanged(int page)
{
    if (page != m_currentPage) {
        m_currentPage = page;
        updateButtonStates();
        emit pageSelected(page);
    }
}

