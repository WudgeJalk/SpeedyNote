#pragma once

// ============================================================================
// AutoSaver - open notes save themselves (Step R1 of docs/ROADMAP.md)
// ============================================================================
// A 1 s timer only reads flags. A note with unsaved changes is saved when no
// pointer is down in any viewport and either
//   (a) nothing changed for autosave/idleSeconds, or
//   (b) it has been unsaved for autosave/maxMinutes and quiet for 1 s.
// Rule 1 (no stutter while writing): the GUI thread only serializes the dirty
// pages (Document::prepareSave). Bytes, I/O and fsync run on one worker
// thread, one job at a time for the whole app. The worker holds value copies
// only; the Document is touched on the GUI thread alone (prepare and finish).
// A note without a place is saved into its temporary bundle (DocumentManager).
// ============================================================================

#include "Document.h"

#include <QObject>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QThreadPool>
#include <functional>
#include <memory>

class DocumentManager;
class QTimer;

class AutoSaver : public QObject
{
    Q_OBJECT

public:
    enum State { Saved, Saving, Failed, Off };
    Q_ENUM(State)

    /// @param editInProgress True while a pointer is down or a text is being typed.
    AutoSaver(DocumentManager* manager, std::function<bool()> editInProgress,
              QObject* parent = nullptr);
    ~AutoSaver() override;

    /// One check (the timer calls it every second). Public for tests (fake clock).
    void tick(qint64 nowMs);

    /// Waits for a running save, then saves @p doc now if it has changes.
    /// Synchronous. @return true when nothing is left unsaved.
    bool flush(Document* doc, QString* error = nullptr);

    /// Reads autosave/enabled, autosave/idleSeconds, autosave/maxMinutes.
    void reloadSettings();
    void configure(bool enabled, int idleSeconds, int maxMinutes);
    bool isEnabled() const { return m_enabled; }

    /// Current state of @p doc and the text for its sign.
    State state(Document* doc, QString* detail = nullptr) const;
    bool isSaving() const { return m_job != nullptr; }

signals:
    void stateChanged(Document* doc, AutoSaver::State state, const QString& detail);

private:
    struct Track {
        quint64 serial = 0;
        qint64 lastChange = 0;
        qint64 dirtySince = -1;  ///< -1: no unsaved change seen
        qint64 retryAt = 0;      ///< After a failure, no new try before this
        State state = Saved;
        QString detail;
    };

    bool canSave(Document* doc) const;
    void start(Document* doc);
    void jobFinished();
    void applyResult(Document* doc, bool ok, const QString& error);
    void setState(Document* doc, State state, const QString& detail);
    QString savedText(Document* doc) const;

    DocumentManager* m_manager;
    std::function<bool()> m_editInProgress;
    QTimer* m_timer;
    QElapsedTimer m_clock;
    QThreadPool m_pool;
    QFutureWatcher<void> m_watcher;
    QHash<Document*, Track> m_tracks;
    Document* m_jobDoc = nullptr;
    std::shared_ptr<Document::SaveJob> m_job;
    qint64 m_jobStartMs = 0;  ///< Real clock, for the debug log
    qint64 m_lastNow = 0;     ///< Clock of the last tick (fake in tests)

    bool m_enabled = true;
    qint64 m_idleMs = 5000;
    qint64 m_maxMs = 2 * 60000;
    static constexpr qint64 kQuietMs = 1000;   ///< (b) waits for a 1 s pause, so it never lands mid-word
    static constexpr qint64 kRetryMs = 30000;  ///< Wait after a failed save (autosave is the retry)
};
