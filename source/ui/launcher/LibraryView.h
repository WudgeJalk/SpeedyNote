#ifndef LIBRARYVIEW_H
#define LIBRARYVIEW_H

#include <QIcon>
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
 * Notebooks show their cover like books (Step N3, setting library/showCovers);
 * notes show their cover, else the cached picture of page 1 (Step N4). A note
 * without a picture gets one on an idle tick, one note at a time, only while
 * this page is visible.
 *
 * Settings ("SpeedyNote"/"App"): library/notebooks (the known notebook folders),
 * library/lastNotebook, library/lastSection, library/lastParentDir,
 * library/showCovers (true).
 */
class LibraryView : public QWidget {
    Q_OBJECT

public:
    explicit LibraryView(QWidget* parent = nullptr);

    /// Reads the known notebooks and the chosen one again (folder names only).
    void reload();

    /// Chooses this notebook and this section (for a notebook card of Chronologie).
    void showNotebook(const QString& notebook, const QString& section);

    /// A note of a notebook in the favourites (the Favoris page shows it). A note
    /// that was never opened enters the start screen's list first.
    static bool isFavourite(const QString& notePath);
    static void setFavourite(const QString& notePath, bool on);

    /// The actions of this page's menus, for the Library side panel too. Each
    /// asks, closes a note that is open first, and says what failed. They
    /// return the new path (true for delete), or empty (false): nothing done.
    static QString newSectionWithPrompt(QWidget* parent, const QString& parentDir);
    static QString renameWithPrompt(QWidget* parent, const QString& path);
    static bool deleteWithPrompt(QWidget* parent, const QString& path);

    /// Moves a note or a section into dir, just before beforeName (also used by
    /// "Move to a Notebook..." of the start screen, Step N2). Notes in it that
    /// are open close first (the editor asks to save); the start screen, the
    /// stars and the remembered choices follow. Returns the new path, or empty
    /// when cancelled or refused (a box says why).
    static QString moveNoteInto(QWidget* parent, const QString& path, const QString& dir,
                                const QString& beforeName = QString());

signals:
    void noteOpenRequested(const QString& bundlePath);
    /// folder: the section the New note window starts in.
    void newNoteRequested(const QString& folder, bool edgeless);

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
    void editCover(const QString& path);
    QIcon notebookIcon(const QString& path) const;
    QIcon noteIcon(const QString& path);  ///< queues a missing picture
    void makeNextThumbnail();

    QListWidget* m_notebooks = nullptr;
    QTreeWidget* m_sections = nullptr;
    QListWidget* m_notes = nullptr;
    QLabel* m_notesTitle = nullptr;

    QTimer m_holdTimer;  ///< long press = right click
    QPointer<QWidget> m_holdTarget;
    QPoint m_holdPos;

    QStringList m_thumbQueue;  ///< notes of the column without a picture yet
    QTimer m_thumbTimer;
};

#endif // LIBRARYVIEW_H
