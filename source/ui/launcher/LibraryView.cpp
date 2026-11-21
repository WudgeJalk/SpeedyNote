#include "LibraryView.h"
#include "Launcher.h"
#include "../ThemeColors.h"
#include "../MenuPopup.h"
#include "../../compat/qt_compat.h"
#include "../../core/Document.h"
#include "../../core/LibraryTree.h"
#include "../../core/NotebookLibrary.h"
#include "../../MainWindow.h"

#include <QApplication>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDir>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStyleHints>
#include <QTreeWidget>
#include <QVBoxLayout>

static const char* KEY_NOTEBOOKS = "library/notebooks";
static const char* KEY_NOTEBOOK = "library/lastNotebook";
static const char* KEY_SECTION = "library/lastSection";
static const char* KEY_PARENT = "library/lastParentDir";

static QIcon swatch(const QColor& color)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);  // no colour: still keeps the names aligned
    if (color.isValid()) {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(pm.rect()), 3, 3);
    }
    return QIcon(pm);
}

static QString pathOf(const QListWidgetItem* item)
{
    return item ? item->data(Qt::UserRole).toString() : QString();
}

static QString pathOf(const QTreeWidgetItem* item)
{
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

static QString parentOf(const QString& path)
{
    return QFileInfo(path).absolutePath();
}

// p with oldRoot swapped for newRoot when p is oldRoot or lies inside it.
static QString rebase(const QString& p, const QString& oldRoot, const QString& newRoot)
{
    if (p == oldRoot) return newRoot;
    return p.startsWith(oldRoot + "/") ? newRoot + p.mid(oldRoot.size()) : p;
}

static QString askName(QWidget* parent, const QString& title, const QString& current = QString())
{
    bool ok = false;
    const QString name = QInputDialog::getText(parent, title, QObject::tr("Name:"),
                                               QLineEdit::Normal, current, &ok);
    return ok ? name.trimmed() : QString();
}

LibraryView::LibraryView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("LibraryViewWidget");
    setAttribute(Qt::WA_StyledBackground);

    m_notebooks = new QListWidget(this);
    m_sections = new QTreeWidget(this);
    m_notes = new QListWidget(this);
    m_sections->setHeaderHidden(true);
    m_sections->setIndentation(16);
    m_notebooks->setDragDropMode(QAbstractItemView::InternalMove);
    m_sections->setDragDropMode(QAbstractItemView::DragDrop);
    m_notes->setDragDropMode(QAbstractItemView::DragDrop);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    struct Column { QAbstractItemView* view; QString title; QString tip; int stretch; };
    const Column columns[] = {
        {m_notebooks, tr("Notebooks"), tr("New notebook, add a notebook folder, open a packed notebook"), 2},
        {m_sections, tr("Sections"), tr("New section"), 3},
        {m_notes, tr("Notes"), tr("New note"), 4},
    };
    for (const Column& c : columns) {
        c.view->setObjectName("LibraryList");
        c.view->setSelectionMode(QAbstractItemView::SingleSelection);
        c.view->setDefaultDropAction(Qt::MoveAction);
        c.view->setContextMenuPolicy(Qt::CustomContextMenu);
        c.view->viewport()->installEventFilter(this);
        QAbstractItemView* view = c.view;
        connect(view, &QWidget::customContextMenuRequested, this, [this, view](const QPoint& pos) {
            const QModelIndex index = view->indexAt(pos);
            showMenu(view, index.data(Qt::UserRole).toString(), view->viewport()->mapToGlobal(pos));
        });

        auto* frame = new QFrame(this);
        frame->setObjectName("LibraryColumn");
        auto* column = new QVBoxLayout(frame);
        column->setContentsMargins(8, 8, 8, 8);
        column->setSpacing(6);
        auto* head = new QHBoxLayout;
        auto* title = new QLabel(c.title, frame);
        title->setObjectName("LibraryColumnTitle");
        auto* add = new QPushButton(QStringLiteral("+"), frame);
        add->setObjectName("LibraryAddButton");
        add->setFixedSize(36, 36);
        add->setToolTip(c.tip);
        connect(add, &QPushButton::clicked, this, [this, view, add]() {
            showMenu(view, QString(), add->mapToGlobal(QPoint(0, add->height())));
        });
        head->addWidget(title, 1);
        head->addWidget(add);
        column->addLayout(head);
        column->addWidget(c.view, 1);
        layout->addWidget(frame, c.stretch);
        if (c.view == m_notes) m_notesTitle = title;
    }

    connect(m_notebooks, &QListWidget::currentItemChanged, this, [this]() { fillSections(); });
    connect(m_sections, &QTreeWidget::currentItemChanged, this, [this]() { fillNotes(); });
    connect(m_notes, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!pathOf(item).isEmpty()) emit noteOpenRequested(pathOf(item));
    });

    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(QGuiApplication::styleHints()->mousePressAndHoldInterval());
    connect(&m_holdTimer, &QTimer::timeout, this, [this]() {
        if (!m_holdTarget) return;
        QContextMenuEvent menu(QContextMenuEvent::Mouse, m_holdPos, m_holdTarget->mapToGlobal(m_holdPos));
        QCoreApplication::sendEvent(m_holdTarget, &menu);
    });
}

void LibraryView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    reload();  // the folders may have changed outside the app; this reads names only
}

QString LibraryView::notebookPath() const
{
    return pathOf(m_notebooks->currentItem());
}

QString LibraryView::sectionPath() const
{
    return pathOf(m_sections->currentItem());
}

void LibraryView::reload()
{
    QSettings settings("SpeedyNote", "App");
    const QString keep = notebookPath().isEmpty() ? settings.value(KEY_NOTEBOOK).toString() : notebookPath();
    {
        const QSignalBlocker block(m_notebooks);
        m_notebooks->clear();
        for (const QString& path : settings.value(KEY_NOTEBOOKS).toStringList()) {
            auto* item = new QListWidgetItem(QFileInfo(path).fileName(), m_notebooks);
            item->setData(Qt::UserRole, path);
            item->setToolTip(QDir::toNativeSeparators(path));
            const bool here = QFileInfo(path).isDir();
            item->setIcon(swatch(here ? LibraryTree::color(path) : QColor()));
            if (!here) {  // gone, renamed or not synced yet: shown greyed, never blocking
                item->setText(tr("%1 (missing)").arg(item->text()));
                item->setForeground(QColor(128, 128, 128));
                item->setToolTip(tr("Folder not found: %1\nRight click or long press: Locate...")
                                     .arg(QDir::toNativeSeparators(path)));
            }
            if (path == keep) m_notebooks->setCurrentItem(item);
        }
        if (!m_notebooks->currentItem() && m_notebooks->count() > 0) m_notebooks->setCurrentRow(0);
        if (m_notebooks->count() == 0) {
            auto* hint = new QListWidgetItem(tr("No notebook yet.\nTap + to make one or add a folder."), m_notebooks);
            hint->setFlags(Qt::NoItemFlags);
        }
    }
    fillSections();
}

void LibraryView::fillSections(const QString& select)
{
    QSettings settings("SpeedyNote", "App");
    const QString notebook = notebookPath();
    const QString want = select.isEmpty() ? settings.value(KEY_SECTION).toString() : select;
    {
        const QSignalBlocker block(m_sections);
        m_sections->clear();
        if (QFileInfo(notebook).isDir()) {
            // The notebook folder itself is the top item: its notes are the loose ones.
            auto* root = new QTreeWidgetItem(m_sections, {QFileInfo(notebook).fileName()});
            root->setData(0, Qt::UserRole, notebook);
            root->setIcon(0, swatch(LibraryTree::color(notebook)));
            root->setFlags(root->flags() & ~Qt::ItemIsDragEnabled);
            m_sections->setCurrentItem(root);
            addSectionItems(root, notebook, want);
            m_sections->expandAll();
        }
    }
    if (!notebook.isEmpty()) settings.setValue(KEY_NOTEBOOK, notebook);
    fillNotes();
}

void LibraryView::addSectionItems(QTreeWidgetItem* parent, const QString& dir, const QString& select)
{
    for (const QString& path : LibraryTree::sections(dir)) {
        auto* item = new QTreeWidgetItem(parent, {LibraryTree::displayName(path)});
        item->setData(0, Qt::UserRole, path);
        item->setIcon(0, swatch(LibraryTree::color(path)));
        if (path == select) m_sections->setCurrentItem(item);
        addSectionItems(item, path, select);
    }
}

void LibraryView::fillNotes()
{
    const QString section = sectionPath();
    m_notes->clear();
    m_notesTitle->setText(section.isEmpty() ? tr("Notes") : LibraryTree::displayName(section));
    for (const QString& path : LibraryTree::notes(section)) {
        auto* item = new QListWidgetItem(LibraryTree::displayName(path), m_notes);
        item->setData(Qt::UserRole, path);
    }
    if (!section.isEmpty()) {
        QSettings("SpeedyNote", "App").setValue(KEY_SECTION, section);
        if (m_notes->count() == 0) {
            auto* hint = new QListWidgetItem(tr("No note here yet.\nTap + to make one."), m_notes);
            hint->setFlags(Qt::NoItemFlags);
        }
    }
}

void LibraryView::showMenu(QAbstractItemView* view, const QString& path, const QPoint& globalPos)
{
    m_holdTimer.stop();
    QMenu menu(this);
    ThemeColors::styleMenu(&menu, QApplication::palette().color(QPalette::Window).lightness() < 128);
    const QString notebook = notebookPath();
    const QString section = sectionPath();
    auto add = [&menu](const QString& text, auto&& slot, bool enabled = true) {
        QAction* action = menu.addAction(text);
        action->setEnabled(enabled);
        QObject::connect(action, &QAction::triggered, std::forward<decltype(slot)>(slot));
    };
    auto common = [&](const QString& p) {
        add(tr("Rename..."), [this, p]() { renameEntry(p); });
        if (!LibraryTree::isNote(p)) {
            add(tr("Set colour..."), [this, p]() { setEntryColor(p, false); });
            if (LibraryTree::color(p).isValid())
                add(tr("Clear colour"), [this, p]() { setEntryColor(p, true); });
        }
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
        add(tr("Show in file explorer"), [p]() { Launcher::showInFileManager(p); });
#endif
    };

    if (view == m_notebooks) {
        if (path.isEmpty()) {
            add(tr("New notebook..."), [this]() { newNotebook(); });
            add(tr("Add an existing notebook folder..."), [this]() { addNotebookFolder(); });
            add(tr("Open a packed notebook..."), [this]() { openPacked(); });
        } else if (!QFileInfo(path).isDir()) {
            add(tr("Locate..."), [this, path]() { locateNotebook(path); });
            add(tr("Remove from the list"), [this, path]() {
                QStringList list = QSettings("SpeedyNote", "App").value(KEY_NOTEBOOKS).toStringList();
                list.removeAll(path);
                setKnownNotebooks(list);
            });
        } else {
            add(tr("New section"), [this, path]() { newSection(path); });
            common(path);
            add(tr("Pack notebook..."), [this, path]() { packNotebook(path); });
            menu.addSeparator();
            add(tr("Remove from the list (keeps the folder)"), [this, path]() {
                QStringList list = QSettings("SpeedyNote", "App").value(KEY_NOTEBOOKS).toStringList();
                list.removeAll(path);
                setKnownNotebooks(list);
            });
            add(tr("Delete..."), [this, path]() { deleteEntry(path); });
        }
    } else if (view == m_sections) {
        const bool isRoot = !path.isEmpty() && path == notebook;
        if (path.isEmpty()) {
            add(tr("New section"), [this, notebook]() { newSection(notebook); }, !notebook.isEmpty());
            if (!section.isEmpty() && section != notebook)
                add(tr("New sub-section in \"%1\"").arg(LibraryTree::displayName(section)),
                    [this, section]() { newSection(section); });
        } else {
            add(isRoot ? tr("New section") : tr("New sub-section"), [this, path]() { newSection(path); });
            add(tr("New paged note"), [this, path]() { fillSections(path); newNote(false); });
            add(tr("New edgeless canvas"), [this, path]() { fillSections(path); newNote(true); });
            common(path);
            if (!isRoot) {
                menu.addSeparator();
                add(tr("Delete..."), [this, path]() { deleteEntry(path); });
            }
        }
    } else if (view == m_notes) {
        if (path.isEmpty()) {
            add(tr("New paged note"), [this]() { newNote(false); }, !section.isEmpty());
            add(tr("New edgeless canvas"), [this]() { newNote(true); }, !section.isEmpty());
            add(tr("Add an existing note here..."), [this]() { addExistingNote(); }, !section.isEmpty());
        } else {
            add(tr("Open"), [this, path]() { emit noteOpenRequested(path); });
            common(path);
            menu.addSeparator();
            add(tr("Delete..."), [this, path]() { deleteEntry(path); });
        }
    }
    execMenuAt(menu, globalPos);
}

// ---------------------------------------------------------------------------
// Long press and drops on the three columns
// ---------------------------------------------------------------------------

bool LibraryView::eventFilter(QObject* watched, QEvent* event)
{
    auto* viewport = qobject_cast<QWidget*>(watched);
    auto* view = viewport ? qobject_cast<QAbstractItemView*>(viewport->parentWidget()) : nullptr;
    if (!view) return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            m_holdTarget = viewport;
            m_holdPos = SN_MOUSE_POS(mouse).toPoint();
            m_holdTimer.start();
        }
        break;
    }
    case QEvent::MouseMove:
        if ((SN_MOUSE_POS(static_cast<QMouseEvent*>(event)).toPoint() - m_holdPos).manhattanLength()
            > QApplication::startDragDistance())
            m_holdTimer.stop();
        break;
    case QEvent::MouseButtonRelease:
    case QEvent::DragEnter:
        m_holdTimer.stop();
        break;
    case QEvent::Drop:
        return handleDrop(view, static_cast<QDropEvent*>(event));
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

bool LibraryView::handleDrop(QAbstractItemView* view, QDropEvent* event)
{
    auto* source = qobject_cast<QAbstractItemView*>(event->source());
    const QPoint pos = SN_DRAG_POS(event).toPoint();
    const QString dragged = source ? source->currentIndex().data(Qt::UserRole).toString() : QString();
    // The view never sees this drop: end its drag state so the drop line goes away.
    QDragLeaveEvent leave;
    QCoreApplication::sendEvent(view->viewport(), &leave);
    if (dragged.isEmpty() || (source != view && !(view == m_sections && source == m_notes))) {
        event->ignore();
        return true;
    }
    // We move the files ourselves: the source view must not delete its row.
    event->setDropAction(Qt::CopyAction);
    event->accept();

    // Before which item of a flat list the drop lands (empty: at the end).
    auto beforeIn = [pos](QListWidget* list) {
        QListWidgetItem* item = list->itemAt(pos);
        if (!item) return -1;
        const int row = list->row(item);
        return pos.y() < list->visualItemRect(item).center().y() ? row : row + 1;
    };
    auto nameAt = [](QListWidget* list, int row) {
        return row >= 0 && row < list->count() ? QFileInfo(pathOf(list->item(row))).fileName() : QString();
    };

    // The work runs after the drag: no dialog may open inside the system drag loop.
    if (view == m_notebooks) {
        const int from = m_notebooks->currentRow();
        const int to = beforeIn(m_notebooks);
        QTimer::singleShot(0, this, [this, from, to]() {
            QStringList list = QSettings("SpeedyNote", "App").value(KEY_NOTEBOOKS).toStringList();
            if (from < 0 || from >= list.size()) return;
            const QString moved = list.takeAt(from);
            const int at = to < 0 ? int(list.size()) : (to > from ? to - 1 : to);
            list.insert(qBound(0, at, int(list.size())), moved);
            setKnownNotebooks(list);
        });
    } else if (view == m_notes) {
        const QString before = nameAt(m_notes, beforeIn(m_notes));
        const QString section = sectionPath();
        QTimer::singleShot(0, this, [=]() { moveEntry(dragged, section, before); });
    } else {
        QTreeWidgetItem* root = m_sections->topLevelItem(0);
        QTreeWidgetItem* target = m_sections->itemAt(pos);
        if (!target) target = root;
        if (!target) return true;
        QString dir = pathOf(target);
        QString before;
        if (source == m_sections && target != root) {
            // Same zones as Qt's drop line: near an edge = beside the section, else into it.
            const QRect r = m_sections->visualItemRect(target);
            const int margin = qBound(2, int(r.height() / 5.5), 12);
            QTreeWidgetItem* parent = target->parent();
            if (pos.y() - r.top() < margin) {
                dir = pathOf(parent);
                before = QFileInfo(pathOf(target)).fileName();
            } else if (r.bottom() - pos.y() < margin) {
                dir = pathOf(parent);
                QTreeWidgetItem* next = parent->child(parent->indexOfChild(target) + 1);
                before = next ? QFileInfo(pathOf(next)).fileName() : QString();
            }
        }
        QTimer::singleShot(0, this, [=]() { moveEntry(dragged, dir, before); });
    }
    return true;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void LibraryView::setKnownNotebooks(const QStringList& paths)
{
    QSettings("SpeedyNote", "App").setValue(KEY_NOTEBOOKS, paths);
    reload();
}

void LibraryView::addKnownNotebook(const QString& path)
{
    const QString clean = QDir::cleanPath(path);
    QSettings settings("SpeedyNote", "App");
    QStringList list = settings.value(KEY_NOTEBOOKS).toStringList();
    if (!list.contains(clean)) list << clean;
    settings.setValue(KEY_NOTEBOOK, clean);
    m_notebooks->setCurrentItem(nullptr);  // so reload() picks the new one
    setKnownNotebooks(list);
}

static QString lastParentDir()
{
    const QString dir = QSettings("SpeedyNote", "App").value(KEY_PARENT).toString();
    return QFileInfo(dir).isDir() ? dir : QDir::homePath();
}

void LibraryView::newNotebook()
{
    const QString name = askName(this, tr("New notebook"));
    if (name.isEmpty()) return;
    const QString parent = QFileDialog::getExistingDirectory(
        this, tr("Where should the notebook \"%1\" go?").arg(name), lastParentDir());
    if (parent.isEmpty()) return;
    QSettings("SpeedyNote", "App").setValue(KEY_PARENT, parent);
    QString error;
    const QString path = LibraryTree::createNotebook(parent, name, &error);
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("New notebook"), error);
        return;
    }
    addKnownNotebook(path);
}

void LibraryView::addNotebookFolder()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("Add an existing notebook folder"),
                                                           lastParentDir());
    if (path.isEmpty()) return;
    if (LibraryTree::isNote(path)) {
        QMessageBox::information(this, tr("Add a notebook folder"),
            tr("This folder is a note, not a notebook. Open a section and use "
               "\"Add an existing note here\" instead."));
        return;
    }
    QSettings("SpeedyNote", "App").setValue(KEY_PARENT, parentOf(path));
    LibraryTree::markNotebook(path);
    addKnownNotebook(path);
}

void LibraryView::openPacked()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Open a packed notebook"), lastParentDir(),
        tr("Packed notebook (*%1)").arg(LibraryTree::PACK_SUFFIX));
    if (file.isEmpty()) return;
    const QString parent = QFileDialog::getExistingDirectory(this, tr("Unpack the notebook into..."),
                                                             lastParentDir());
    if (parent.isEmpty()) return;
    QSettings("SpeedyNote", "App").setValue(KEY_PARENT, parent);
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QString path = LibraryTree::unpack(file, parent, &error);
    QApplication::restoreOverrideCursor();
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("Open a packed notebook"), error);
        return;
    }
    addKnownNotebook(path);
}

void LibraryView::packNotebook(const QString& path)
{
    QString file = QFileDialog::getSaveFileName(this, tr("Pack notebook"),
        lastParentDir() + "/" + QFileInfo(path).fileName() + LibraryTree::PACK_SUFFIX,
        tr("Packed notebook (*%1)").arg(LibraryTree::PACK_SUFFIX));
    if (file.isEmpty()) return;
    if (!file.endsWith(LibraryTree::PACK_SUFFIX, Qt::CaseInsensitive)) file += LibraryTree::PACK_SUFFIX;
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = LibraryTree::pack(path, file, &error);
    QApplication::restoreOverrideCursor();
    if (ok) {
        QMessageBox::information(this, tr("Pack notebook"),
            tr("The notebook is packed in:\n%1\n\nNotes open in the editor are packed as last saved.")
                .arg(QDir::toNativeSeparators(file)));
    } else {
        QMessageBox::warning(this, tr("Pack notebook"), tr("Nothing was packed.\n\n%1").arg(error));
    }
}

void LibraryView::locateNotebook(const QString& path)
{
    const QString start = QFileInfo(parentOf(path)).isDir() ? parentOf(path) : lastParentDir();
    const QString found = QFileDialog::getExistingDirectory(
        this, tr("Where is the notebook \"%1\" now?").arg(QFileInfo(path).fileName()), start);
    if (found.isEmpty()) return;
    QSettings settings("SpeedyNote", "App");
    QStringList list = settings.value(KEY_NOTEBOOKS).toStringList();
    const int at = int(list.indexOf(path));
    if (at >= 0) list[at] = QDir::cleanPath(found);
    settings.setValue(KEY_NOTEBOOK, QDir::cleanPath(found));
    m_notebooks->setCurrentItem(nullptr);
    setKnownNotebooks(list);
}

void LibraryView::newSection(const QString& parentDir)
{
    if (parentDir.isEmpty()) return;
    const QString name = askName(this, tr("New section"));
    if (name.isEmpty()) return;
    QString error;
    const QString path = LibraryTree::createSection(parentDir, name, &error);
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("New section"), error);
        return;
    }
    fillSections(path);
}

void LibraryView::newNote(bool edgeless)
{
    const QString section = sectionPath();
    if (section.isEmpty()) return;
    QString base = LibraryTree::uniqueName(section, edgeless ? tr("New canvas") : tr("New note"), ".snb");
    base.chop(4);
    const QString name = askName(this, edgeless ? tr("New edgeless canvas") : tr("New paged note"), base);
    if (name.isEmpty()) return;
    QString error = LibraryTree::checkName(name);
    if (error.isEmpty() && QFileInfo::exists(section + "/" + name + ".snb"))
        error = tr("\"%1\" already exists here.").arg(name);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("New note"), error);
        return;
    }
    LibraryTree::placeBefore(section, name + ".snb");
    emit newNoteRequested(section + "/" + name + ".snb", edgeless);  // the editor saves it there
    fillNotes();
}

void LibraryView::addExistingNote()
{
    const QString section = sectionPath();
    QSettings settings("SpeedyNote", "App");
    const QString note = QFileDialog::getExistingDirectory(this, tr("Choose a note (.snb folder)"),
        settings.value("FileDialogs/lastOpenDirectory", QDir::homePath()).toString());
    if (note.isEmpty() || section.isEmpty()) return;
    settings.setValue("FileDialogs/lastOpenDirectory", parentOf(note));
    if (!QFile::exists(note + "/document.json")) {
        QMessageBox::warning(this, tr("Add an existing note"),
            tr("This folder is not a SpeedyNote note (no document.json):\n%1").arg(QDir::toNativeSeparators(note)));
        return;
    }
    QMessageBox box(QMessageBox::Question, tr("Add an existing note"),
                    tr("Move \"%1\" into \"%2\", or copy it there?")
                        .arg(LibraryTree::displayName(note), LibraryTree::displayName(section)),
                    QMessageBox::Cancel, this);
    QPushButton* moveButton = box.addButton(tr("Move"), QMessageBox::AcceptRole);
    QPushButton* copyButton = box.addButton(tr("Copy"), QMessageBox::AcceptRole);
    box.exec();
    if (box.clickedButton() == moveButton) {
        moveEntry(note, section, QString());
    } else if (box.clickedButton() == copyButton) {
        QString error;
        if (LibraryTree::copyNoteInto(note, section, &error).isEmpty())
            QMessageBox::warning(this, tr("Add an existing note"), error);
        fillNotes();
    }
}

// Notes open in the editor hold their path: close them first (the editor asks
// to save). False if the user cancelled.
bool LibraryView::releaseNotes(const QString& path, QStringList* notes)
{
    *notes = LibraryTree::notesUnder(path);
    MainWindow* mainWindow = MainWindow::findExistingMainWindow();
    if (!mainWindow || mainWindow->tabCount() == 0) return true;
    for (const QString& note : *notes) {
        const QString id = Document::peekBundleId(note);
        if (!id.isEmpty() && !mainWindow->closeDocumentById(id)) return false;
    }
    return true;
}

// The recent list, the stars and the remembered choices follow a moved folder.
void LibraryView::followNotes(const QString& oldRoot, const QString& newRoot, const QStringList& notes)
{
    for (const QString& note : notes)
        NotebookLibrary::instance()->updateBundlePath(note, rebase(note, oldRoot, newRoot));
    QSettings settings("SpeedyNote", "App");
    QStringList list = settings.value(KEY_NOTEBOOKS).toStringList();
    for (QString& p : list) p = rebase(p, oldRoot, newRoot);
    settings.setValue(KEY_NOTEBOOKS, list);
    settings.setValue(KEY_NOTEBOOK, rebase(settings.value(KEY_NOTEBOOK).toString(), oldRoot, newRoot));
    settings.setValue(KEY_SECTION, rebase(settings.value(KEY_SECTION).toString(), oldRoot, newRoot));
}

void LibraryView::renameEntry(const QString& path)
{
    const QString name = askName(this, tr("Rename"), LibraryTree::displayName(path));
    if (name.isEmpty() || name == LibraryTree::displayName(path)) return;
    QStringList notes;
    if (!releaseNotes(path, &notes)) return;
    QString error;
    const QString renamed = LibraryTree::rename(path, name, &error);
    if (renamed.isEmpty()) {
        QMessageBox::warning(this, tr("Rename"), error);
        return;
    }
    followNotes(path, renamed, notes);
    m_notebooks->setCurrentItem(nullptr);
    reload();
}

void LibraryView::setEntryColor(const QString& path, bool clear)
{
    QColor color;
    if (!clear) {
        const QColor current = LibraryTree::color(path);
        color = QColorDialog::getColor(current.isValid() ? current : QColor("#4a90d9"), this,
                                       tr("Colour of \"%1\"").arg(LibraryTree::displayName(path)));
        if (!color.isValid()) return;  // cancelled
    }
    LibraryTree::setColor(path, color);
    reload();
}

void LibraryView::deleteEntry(const QString& path)
{
    const QString name = LibraryTree::displayName(path);
    const QString question = LibraryTree::isNote(path)
        ? tr("Move the note \"%1\" to the recycle bin?").arg(name)
        : tr("Move \"%1\" and everything in it to the recycle bin?").arg(name);
    if (QMessageBox::question(this, tr("Delete"), question, QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;
    QStringList notes;
    if (!releaseNotes(path, &notes)) return;
    QString error;
    if (!LibraryTree::trash(path, &error)) {
        if (QMessageBox::warning(this, tr("Delete"),
                tr("%1\n\nDelete it for good instead? This cannot be undone.").arg(error),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        if (!QDir(path).removeRecursively())
            QMessageBox::warning(this, tr("Delete"), tr("Some files could not be deleted."));
    }
    NotebookLibrary* library = NotebookLibrary::instance();
    for (const QString& note : notes) {
        library->invalidateThumbnail(note);  // before removeFromRecent, which knows its id
        library->removeFromRecent(note);
    }
    QStringList list = QSettings("SpeedyNote", "App").value(KEY_NOTEBOOKS).toStringList();
    list.removeAll(path);
    setKnownNotebooks(list);
}

void LibraryView::moveEntry(const QString& path, const QString& dir, const QString& beforeName)
{
    if (path.isEmpty() || dir.isEmpty()) return;
    QStringList notes;
    if (parentOf(path) != dir && !releaseNotes(path, &notes)) return;
    QString error;
    const QString moved = LibraryTree::move(path, dir, beforeName, &error);
    if (moved.isEmpty()) {
        QMessageBox::warning(this, tr("Move"), error);
        return;
    }
    if (moved != path) followNotes(path, moved, notes);
    fillSections(rebase(sectionPath(), path, moved));
}
