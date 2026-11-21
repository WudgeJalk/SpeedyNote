#ifndef LIBRARYVIEW_H
#define LIBRARYVIEW_H

#include <QPointer>
#include <QTimer>
#include <QWidget>

class QAbstractItemView;
class QDropEvent;
class QLabel;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * @brief The "Library" page of the launcher (roadmap C1), like OneNote:
 * notebooks | sections of the chosen notebook (a tree) | notes of the chosen section.
 *
 * The disk side lives in LibraryTree; this page only shows it and asks. Every
 * action is in the "+" buttons and in the menu of an item, opened by a right
 * click or a long press (finger, pen or mouse). Drag and drop orders a column,
 * and moves a note or a section into another section.
 *
 * Settings ("SpeedyNote"/"App"): library/notebooks (the known notebook folders),
 * library/lastNotebook, library/lastSection, library/lastParentDir.
 */
class LibraryView : public QWidget {
    Q_OBJECT

public:
    explicit LibraryView(QWidget* parent = nullptr);

    /// Reads the known notebooks and the chosen one again (folder names only).
    void reload();

signals:
    void noteOpenRequested(const QString& bundlePath);
    void newNoteRequested(const QString& bundlePath, bool edgeless);

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QString notebookPath() const;
    QString sectionPath() const;
    void fillSections(const QString& select = QString());
    void addSectionItems(QTreeWidgetItem* parent, const QString& dir, const QString& select);
    void fillNotes();
    void showMenu(QAbstractItemView* view, const QString& path, const QPoint& globalPos);
    bool handleDrop(QAbstractItemView* view, QDropEvent* event);

    void addKnownNotebook(const QString& path);
    void setKnownNotebooks(const QStringList& paths);
    void newNotebook();
    void addNotebookFolder();
    void openPacked();
    void packNotebook(const QString& path);
    void locateNotebook(const QString& path);
    void newSection(const QString& parentDir);
    void newNote(bool edgeless);
    void addExistingNote();
    void renameEntry(const QString& path);
    void setEntryColor(const QString& path, bool clear);
    void deleteEntry(const QString& path);
    void moveEntry(const QString& path, const QString& dir, const QString& beforeName);
    bool releaseNotes(const QString& path, QStringList* notes);
    void followNotes(const QString& oldRoot, const QString& newRoot, const QStringList& notes);

    QListWidget* m_notebooks = nullptr;
    QTreeWidget* m_sections = nullptr;
    QListWidget* m_notes = nullptr;
    QLabel* m_notesTitle = nullptr;

    QTimer m_holdTimer;  ///< long press = right click
    QPointer<QWidget> m_holdTarget;
    QPoint m_holdPos;
};

#endif // LIBRARYVIEW_H
