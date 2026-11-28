#include "PanelFooter.h"
#include "LibraryPanel.h"

#include "../ThemeColors.h"
#include "../launcher/LibraryView.h"
#include "../widgets/ActionBarButton.h"
#include "../widgets/LibraryPlacePicker.h"
#include "../../compat/qt_compat.h"
#include "../../core/LibraryTree.h"

#include <QApplication>
#include <QColorDialog>
#include <QDir>
#include <QDropEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QStyleHints>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QPixmap>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

static const int PathRole = Qt::UserRole;

LibraryPanelTree::LibraryPanelTree(QWidget* parent)
    : OutlinePanelTreeWidget(parent)
{
    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(QGuiApplication::styleHints()->mousePressAndHoldInterval());
    connect(&m_holdTimer, &QTimer::timeout, this, [this]() {
        m_menuByHold = true;
        emit customContextMenuRequested(m_holdPos);
    });
}

void LibraryPanelTree::mousePressEvent(QMouseEvent* event)
{
    m_menuByHold = false;
    if (event->button() == Qt::LeftButton) {
        m_holdPos = event->pos();
        m_holdTimer.start();
    }
    OutlinePanelTreeWidget::mousePressEvent(event);
}

void LibraryPanelTree::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->pos() - m_holdPos).manhattanLength() > QApplication::startDragDistance()) {
        m_holdTimer.stop();   // a scroll or a drag, not a hold
    }
    OutlinePanelTreeWidget::mouseMoveEvent(event);
}

void LibraryPanelTree::mouseReleaseEvent(QMouseEvent* event)
{
    m_holdTimer.stop();
    if (m_menuByHold) {
        // The menu took this press: no tap. The base class forgets the press
        // on a release far from any row.
        m_menuByHold = false;
        QMouseEvent away(event->type(), QPointF(-1000, -1000), event->globalPosition(),
                         event->button(), event->buttons(), event->modifiers(), event->pointingDevice());
        OutlinePanelTreeWidget::mouseReleaseEvent(&away);
        return;
    }
    OutlinePanelTreeWidget::mouseReleaseEvent(event);
}

void LibraryPanelTree::dropEvent(QDropEvent* event)
{
    QTreeWidgetItem* dragged = currentItem();
    QTreeWidgetItem* onto = itemAt(SN_DRAG_POS(event).toPoint());
    // We move the files ourselves: the tree must not move or delete its rows.
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    if (!dragged || !onto || dragged == onto) return;
    emit entryDropped(dragged->data(0, PathRole).toString(), onto->data(0, PathRole).toString());
}

LibraryPanel::LibraryPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tree = new LibraryPanelTree(this);
    m_tree->setHeaderHidden(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setExpandsOnDoubleClick(false);  // one tap is enough, see itemClicked below
    m_tree->setTextElideMode(Qt::ElideRight);
    m_tree->setDragDropMode(QAbstractItemView::DragDrop);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setDropIndicatorShown(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    // Rows a finger can hit (the user, 2026-11-23).
    m_tree->setIndentation(22);
    m_tree->setStyleSheet(QStringLiteral("QTreeView::item { min-height: 38px; }"));
    layout->addWidget(m_tree, 1);

    // The panel's options, at its end (as in the Layers and Pages panels).
    m_footer = new QWidget(this);
    auto* buttons = new QHBoxLayout(m_footer);
    buttons->setContentsMargins(PanelFooter::Margin, PanelFooter::Margin, PanelFooter::Margin, PanelFooter::Margin);
    buttons->setSpacing(PanelFooter::Spacing);
    auto addButton = [&](const char* icon, const QString& tip, auto&& slot) {
        auto* button = new ActionBarButton(this);
        button->setIconName(QLatin1String(icon));
        button->setToolTip(tip);
        connect(button, &ActionBarButton::clicked, this, std::forward<decltype(slot)>(slot));
        buttons->addWidget(button);
        return button;
    };
    m_newNoteButton = addButton("addtab", tr("New note in the chosen section"),
                                [this]() { emit newNoteRequested(currentFolder()); });
    m_newSectionButton = addButton("folder", tr("New section in the chosen notebook or section"),
                                   [this]() { newSection(); });
    m_renameButton = addButton("edit", tr("Rename"), [this]() { renameCurrent(); });
    m_deleteButton = addButton("trash", tr("Delete (to the recycle bin)"), [this]() { deleteCurrent(); });
    buttons->addStretch();
    layout->addWidget(m_footer);
    setDarkMode(QApplication::palette().color(QPalette::Window).lightness() < 128);

    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        const QString path = item->data(0, PathRole).toString();
        if (path.isEmpty()) return;  // the "no notebook" lines
        if (LibraryTree::isNote(path)) {
            emit noteActivated(path);
        } else {
            item->setExpanded(!item->isExpanded());
        }
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        const QString path = item->data(0, PathRole).toString();
        m_open.insert(path);
        // Read the folder now, in place of the stand-in child.
        if (item->childCount() == 1 && item->child(0)->data(0, PathRole).toString().isEmpty()) {
            delete item->takeChild(0);
            fill(item, path);
        }
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
        m_open.remove(item->data(0, PathRole).toString());
    });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this]() { updateButtons(); });
    connect(m_tree, &QWidget::customContextMenuRequested, this, &LibraryPanel::showMenu);
    // Later: the tree is still inside its drop event here.
    connect(m_tree, &LibraryPanelTree::entryDropped, this, [this](const QString& path, const QString& onto) {
        QTimer::singleShot(0, this, [this, path, onto]() { moveEntry(path, onto); });
    });
    updateButtons();
}

void LibraryPanel::setDarkMode(bool dark)
{
    PanelFooter::apply(m_footer, dark);
    for (ActionBarButton* button : {m_newNoteButton, m_newSectionButton, m_renameButton, m_deleteButton}) {
        button->setDarkMode(dark);
    }
}

void LibraryPanel::showEvent(QShowEvent* event)
{
    reload();  // folders may have changed on the Library page or on disk
    QWidget::showEvent(event);
}

void LibraryPanel::reload()
{
    const QString chosen = currentPath();
    const QSet<QString> open = m_open;  // clear() and the fill below change m_open
    m_tree->clear();
    m_open = open;
    const QStringList notebooks = QSettings("SpeedyNote", "App").value("library/notebooks").toStringList();
    for (const QString& path : notebooks) {
        if (QFileInfo(path).isDir()) addEntry(nullptr, path);
    }
    if (m_tree->topLevelItemCount() == 0) {
        // Two short lines: one long line was cut off in a narrow panel.
        for (const QString& line : {tr("No notebook yet."), tr("Make one: Home > Library.")}) {
            auto* none = new QTreeWidgetItem(m_tree, {line});
            none->setFlags(Qt::NoItemFlags);
        }
    }
    if (!chosen.isEmpty()) {
        const auto found = m_tree->findItems(LibraryTree::displayName(chosen), Qt::MatchExactly | Qt::MatchRecursive);
        for (QTreeWidgetItem* item : found) {
            if (item->data(0, PathRole).toString() == chosen) m_tree->setCurrentItem(item);
        }
    }
    updateButtons();
}

void LibraryPanel::fill(QTreeWidgetItem* parent, const QString& dir)
{
    for (const QString& path : LibraryTree::sections(dir)) addEntry(parent, path);
    for (const QString& path : LibraryTree::notes(dir)) addEntry(parent, path);
}

QTreeWidgetItem* LibraryPanel::addEntry(QTreeWidgetItem* parent, const QString& path)
{
    auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    item->setText(0, LibraryTree::displayName(path));
    item->setToolTip(0, LibraryTree::displayName(path));
    item->setData(0, PathRole, path);
    // A notebook stays where it is; a note takes no children.
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
    if (parent) flags |= Qt::ItemIsDragEnabled;
    item->setFlags(flags);
    if (LibraryTree::isNote(path)) return item;

    QFont bold = item->font(0);
    bold.setBold(true);
    item->setFont(0, bold);
    const QColor color = LibraryTree::color(path);
    if (color.isValid()) {
        QPixmap swatch(12, 12);
        swatch.fill(color);
        item->setIcon(0, swatch);
    }
    if (!LibraryTree::children(path).isEmpty()) {
        new QTreeWidgetItem(item);  // stand-in: the folder is read when it opens
        if (m_open.contains(path)) item->setExpanded(true);
    }
    return item;
}

QString LibraryPanel::currentPath() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    return item ? item->data(0, PathRole).toString() : QString();
}

QString LibraryPanel::currentFolder() const
{
    const QString path = currentPath();
    return LibraryTree::isNote(path) ? QFileInfo(path).path() : path;
}

void LibraryPanel::updateButtons()
{
    const QString path = currentPath();
    m_newNoteButton->setEnabled(!path.isEmpty());
    m_newSectionButton->setEnabled(!path.isEmpty());
    m_renameButton->setEnabled(!path.isEmpty());
    m_deleteButton->setEnabled(!path.isEmpty());
}

void LibraryPanel::newSection()
{
    const QString folder = currentFolder();
    const QString made = LibraryView::newSectionWithPrompt(this, folder);
    if (made.isEmpty()) return;
    m_open.insert(folder);  // show the new section
    reload();
}

void LibraryPanel::renameCurrent()
{
    const QString path = currentPath();
    if (path.isEmpty()) return;
    const QString renamed = LibraryView::renameWithPrompt(this, path);
    if (renamed.isEmpty()) return;
    if (m_open.remove(path)) m_open.insert(renamed);
    reload();
}

void LibraryPanel::deleteCurrent()
{
    const QString path = currentPath();
    if (!path.isEmpty() && LibraryView::deleteWithPrompt(this, path)) reload();
}

void LibraryPanel::colourCurrent()
{
    const QString path = currentPath();
    if (path.isEmpty() || LibraryTree::isNote(path)) return;
    const QColor current = LibraryTree::color(path);
    const QColor color = QColorDialog::getColor(current.isValid() ? current : QColor("#4a90d9"), this,
                                                tr("Colour of \"%1\"").arg(LibraryTree::displayName(path)));
    if (!color.isValid()) return;
    LibraryTree::setColor(path, color);
    reload();
}

void LibraryPanel::moveEntry(const QString& path, const QString& ontoPath)
{
    if (path.isEmpty() || ontoPath.isEmpty()) return;
    // Onto a note: into its section, just before it. Onto a folder: into it, last.
    const bool ontoNote = LibraryTree::isNote(ontoPath);
    const QString dir = ontoNote ? QFileInfo(ontoPath).path() : ontoPath;
    const QString before = ontoNote ? QFileInfo(ontoPath).fileName() : QString();
    if (LibraryView::moveNoteInto(this, path, dir, before).isEmpty()) return;
    m_open.insert(dir);  // show where it went
    reload();
}

void LibraryPanel::moveCurrentTo(const QPoint& globalPos)
{
    const QString path = currentPath();
    if (path.isEmpty()) return;
    const QString here = QFileInfo(path).path();
    const QString folder = LibraryPlacePicker::choose(this, globalPos, here);
    if (folder.isEmpty() || QDir::cleanPath(folder) == QDir::cleanPath(here)) return;
    if (LibraryView::moveNoteInto(this, path, folder).isEmpty()) return;
    m_open.insert(QDir::fromNativeSeparators(folder));
    reload();
}

void LibraryPanel::showMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item || item->data(0, PathRole).toString().isEmpty()) return;
    m_tree->setCurrentItem(item);
    QMenu menu(this);
    ThemeColors::styleMenu(&menu, QApplication::palette().color(QPalette::Window).lightness() < 128);
    menu.addAction(tr("New note"), this, [this]() { emit newNoteRequested(currentFolder()); });
    menu.addAction(tr("New section"), this, [this]() { newSection(); });
    menu.addSeparator();
    const QString path = currentPath();
    const QPoint globalPos = m_tree->viewport()->mapToGlobal(pos);
    if (LibraryTree::isNote(path)) {
        const bool favourite = LibraryView::isFavourite(path);
        menu.addAction(favourite ? tr("Remove from the favourites") : tr("Add to the favourites"), this,
                       [path, favourite]() { LibraryView::setFavourite(path, !favourite); });
    }
    if (item->parent()) {   // a notebook stays where it is
        menu.addAction(tr("Move to..."), this, [this, globalPos]() { moveCurrentTo(globalPos); });
    }
    menu.addAction(tr("Rename..."), this, [this]() { renameCurrent(); });
    if (!LibraryTree::isNote(currentPath())) {
        menu.addAction(tr("Set colour..."), this, [this]() { colourCurrent(); });
    }
    menu.addAction(tr("Delete..."), this, [this]() { deleteCurrent(); });
    menu.exec(globalPos);
}
