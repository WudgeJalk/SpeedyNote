#pragma once

// ============================================================================
// VersionsDialog - "Restore an Earlier Version..." (Step R4)
// ============================================================================
// Lists the times at which versions of the note were kept (newest first) and
// shows the current page as it was then. "Open as a Copy" builds a new note
// without a place from that time (NoteVersions::buildCopy); the note itself
// is never overwritten.
// ============================================================================

#include <QDialog>
#include <QVector>

class Document;
class QLabel;
class QListWidget;
class QPushButton;

class VersionsDialog : public QDialog {
    Q_OBJECT
public:
    VersionsDialog(Document* doc, int pageIndex, QWidget* parent = nullptr);

    /// The bundle built by "Open as a Copy"; empty until the dialog is accepted.
    QString copyPath() const { return m_copyPath; }

    /// Selects a time (row 0 = newest). Also used by tests.
    void selectRow(int row);

private:
    void showTime(int row);
    void openCopy();

    Document* m_doc = nullptr;
    QString m_bundle;
    QString m_pageFile;          ///< "pages/<uuid>.json"; empty for an endless note
    QVector<qint64> m_times;     ///< NoteVersions::versionTimes(), newest first
    QListWidget* m_list = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_warning = nullptr;
    QPushButton* m_open = nullptr;
    QString m_copyPath;
};
