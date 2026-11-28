#pragma once

// ============================================================================
// LibraryPanel - the library in the left side panel (the user, 2026-11-23)
// ============================================================================
// One tree: notebook > section > sub-section > note, the same folders and the
// same order as the Library page of the start screen (LibraryTree, setting
// "library/notebooks"). A tap on a note opens it; a tap on a folder opens or
// closes it. A folder is read only when it opens.
//
// At the end of the panel, like the other side panels: new note, new section,
// rename, delete. The same actions and "Set colour..." are in the menu of an
// entry (right click). Drag a note or a section onto a section to move it
// there, onto a note to put it just before that note (mouse or pen; a finger
// scrolls). The work itself is LibraryView's, so an open note closes first and
// the start screen follows.
// ============================================================================

#include "OutlinePanelTreeWidget.h"

#include <QPoint>
#include <QSet>
#include <QTimer>
#include <QWidget>

class ActionBarButton;

/// The tree; it only reports a drop, LibraryPanel moves the files. A press
/// held still (finger, pen or mouse) asks for the menu, as a right click does.
class LibraryPanelTree : public OutlinePanelTreeWidget {
    Q_OBJECT
public:
    explicit LibraryPanelTree(QWidget* parent = nullptr);
signals:
    void entryDropped(const QString& path, const QString& ontoPath);
protected:
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
private:
    QTimer m_holdTimer;
    QPoint m_holdPos;
    bool m_menuByHold = false;   ///< the hold opened the menu: its release is not a tap
};

class LibraryPanel : public QWidget {
    Q_OBJECT

public:
    explicit LibraryPanel(QWidget* parent = nullptr);

    /// Reads the notebooks again; the folders that were open stay open.
    void reload();
    void setDarkMode(bool dark);

signals:
    void noteActivated(const QString& path);
    /// folder: where the New note window starts. Call reload() after it.
    void newNoteRequested(const QString& folder);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void fill(QTreeWidgetItem* parent, const QString& dir);
    QTreeWidgetItem* addEntry(QTreeWidgetItem* parent, const QString& path);
    QString currentPath() const;
    QString currentFolder() const;   ///< the chosen folder, or the folder of the chosen note
    void updateButtons();
    void newSection();
    void renameCurrent();
    void deleteCurrent();
    void colourCurrent();
    void moveEntry(const QString& path, const QString& ontoPath);
    void moveCurrentTo(const QPoint& globalPos);   ///< "Move to...": the way without a drag
    void showMenu(const QPoint& pos);

    LibraryPanelTree* m_tree = nullptr;
    ActionBarButton* m_newNoteButton = nullptr;
    ActionBarButton* m_newSectionButton = nullptr;
    ActionBarButton* m_renameButton = nullptr;
    ActionBarButton* m_deleteButton = nullptr;
    QSet<QString> m_open;  ///< paths of the folders that are open
};
