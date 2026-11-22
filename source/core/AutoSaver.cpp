#include "AutoSaver.h"
#include "DocumentManager.h"

#include <QDebug>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>
#include <QTime>
#include <QTimer>

AutoSaver::AutoSaver(DocumentManager* manager, std::function<bool()> editInProgress,
                     QObject* parent)
    : QObject(parent)
    , m_manager(manager)
    , m_editInProgress(std::move(editInProgress))
    , m_timer(new QTimer(this))
{
    m_pool.setMaxThreadCount(1);  // one job at a time for the whole app
    m_clock.start();
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, &AutoSaver::jobFinished);
    // closeDocument() has already waited for the job and applied it.
    connect(m_manager, &DocumentManager::documentClosed, this, [this](Document* doc) {
        if (doc == m_jobDoc) {
            m_jobDoc = nullptr;
            m_job.reset();
        }
        m_tracks.remove(doc);
    });
    // Ctrl+S or Save As: the sign shows that save (and the new place).
    connect(m_manager, &DocumentManager::documentSaved, this, [this](Document* doc) {
        Track& track = m_tracks[doc];
        track.dirtySince = -1;
        track.retryAt = 0;
        setState(doc, Saved, savedText(doc));
    });
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, [this]() { tick(m_clock.elapsed()); });
    reloadSettings();
}

// The documents may already be gone (DocumentManager is deleted first), so
// nothing here touches them: ~Document waits for its own job, and the pool's
// destructor waits for the worker.
AutoSaver::~AutoSaver()
{
    Document::s_keepDirtyLoaded = false;
}

void AutoSaver::reloadSettings()
{
    QSettings settings("SpeedyNote", "App");
    configure(settings.value("autosave/enabled", true).toBool(),
              settings.value("autosave/idleSeconds", 5).toInt(),
              settings.value("autosave/maxMinutes", 2).toInt());
}

void AutoSaver::configure(bool enabled, int idleSeconds, int maxMinutes)
{
    m_enabled = enabled;
    m_idleMs = qBound(1, idleSeconds, 60) * 1000LL;
    m_maxMs = qBound(1, maxMinutes, 30) * 60000LL;
    // Autosave writes dirty pages on its worker; without it, eviction writes them.
    Document::s_keepDirtyLoaded = m_enabled;
    if (m_enabled) {
        m_timer->start();
    } else {
        m_timer->stop();
    }
    for (int i = 0; i < m_manager->documentCount(); ++i) {
        Document* doc = m_manager->documentAt(i);
        QString detail;
        const State current = state(doc, &detail);
        emit stateChanged(doc, current, detail);
    }
}

bool AutoSaver::canSave(Document* doc) const
{
    // A note that has a place, or a new note in its temporary bundle.
    return doc && !doc->bundlePath().isEmpty();
}

void AutoSaver::tick(qint64 nowMs)
{
    m_lastNow = nowMs;
    if (!m_enabled) {
        return;
    }
    Document* due = nullptr;
    for (int i = 0; i < m_manager->documentCount(); ++i) {
        Document* doc = m_manager->documentAt(i);
        Track& track = m_tracks[doc];
        if (!m_manager->hasUnsavedChanges(doc)) {
            track.dirtySince = -1;
            // Saved by hand (Ctrl+S) after an autosave failed.
            if (track.state == Failed && doc != m_jobDoc) {
                track.retryAt = 0;
                setState(doc, Saved, savedText(doc));
            }
            continue;
        }
        if (track.dirtySince < 0) {
            track.dirtySince = nowMs;
            track.lastChange = nowMs;
            track.serial = doc->editSerial();
        } else if (doc->editSerial() != track.serial) {
            track.serial = doc->editSerial();
            track.lastChange = nowMs;
        }
        if (due || m_job || nowMs < track.retryAt || !canSave(doc)) {
            continue;
        }
        const bool idle = nowMs - track.lastChange >= m_idleMs;
        const bool overdue = nowMs - track.dirtySince >= m_maxMs
                             && nowMs - track.lastChange >= kQuietMs;
        if (idle || overdue) {
            due = doc;
        }
    }
    // A pointer down in any pane delays every save: the pen never waits.
    if (due && !(m_editInProgress && m_editInProgress())) {
        start(due);
    }
}

void AutoSaver::start(Document* doc)
{
    // A folder moved or deleted behind our back: writing would rebuild a
    // half note there. The note stays open with its changes instead.
    if (!QFileInfo::exists(doc->bundlePath() + "/document.json")) {
        applyResult(doc, false, tr("The note's folder was moved or deleted."));
        return;
    }
    setState(doc, Saving, tr("Saving..."));
    QString error;
    m_jobStartMs = m_clock.elapsed();
    m_job = doc->startBackgroundSave(&m_pool, &error);
    if (!m_job) {
        applyResult(doc, false, error);
        return;
    }
    m_jobDoc = doc;
    m_watcher.setFuture(doc->pendingSaveFuture());
}

void AutoSaver::jobFinished()
{
    if (!m_job || !m_jobDoc) {
        return;  // already applied by flush(), or the note was closed
    }
    Document* doc = m_jobDoc;
    const std::shared_ptr<Document::SaveJob> job = std::move(m_job);
    m_job.reset();
    m_jobDoc = nullptr;
    doc->waitForPendingSave();  // finishSave once; a no-op if a writer already did it
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "Autosave of" << doc->displayName() << (job->ok ? "done" : "failed") << "in"
             << (m_clock.elapsed() - m_jobStartMs) << "ms";
#endif
    applyResult(doc, job->ok, job->error);
}

bool AutoSaver::flush(Document* doc, QString* error)
{
    if (!doc) {
        return true;
    }
    if (doc == m_jobDoc) {
        jobFinished();  // waits for the worker and shows its result
    }
    doc->waitForPendingSave();
    if (!m_manager->hasUnsavedChanges(doc)) {
        return true;
    }
    QString why;
    bool ok = false;
    if (!canSave(doc)) {
        why = tr("The note has no place on disk yet.");
    } else {
        ok = doc->saveBundle(doc->bundlePath(), /*finalize=*/false, &why);
    }
    applyResult(doc, ok, why);
    if (error) {
        *error = why;
    }
    return ok;
}

void AutoSaver::applyResult(Document* doc, bool ok, const QString& error)
{
    Track& track = m_tracks[doc];
    track.dirtySince = -1;  // the next tick starts the clocks again if changes remain
    if (ok) {
        track.retryAt = 0;
        if (!doc->modified) {
            m_manager->clearModified(doc);  // also clears DocumentManager's flag
        }
        setState(doc, Saved, savedText(doc));
    } else {
        track.retryAt = m_lastNow + kRetryMs;
        setState(doc, Failed, tr("Not saved: %1")
                                  .arg(error.isEmpty() ? tr("the reason is unknown.") : error));
    }
}

void AutoSaver::setState(Document* doc, State state, const QString& detail)
{
    Track& track = m_tracks[doc];
    track.state = state;
    track.detail = detail;
    emit stateChanged(doc, state, detail);
}

QString AutoSaver::savedText(Document* doc) const
{
    if (m_manager->isUsingTempBundle(doc)) {
        return tr("Kept safe. Press Ctrl+S to choose a place.");
    }
    return tr("Saved %1").arg(QLocale().toString(QTime::currentTime(), QLocale::ShortFormat));
}

AutoSaver::State AutoSaver::state(Document* doc, QString* detail) const
{
    QString text;
    State result = Saved;
    if (!m_enabled) {
        result = Off;
        text = tr("Autosave is off.");
    } else if (!canSave(doc)) {
        result = Off;
        text = tr("Not saved yet. Press Ctrl+S to choose a place.");
    } else {
        const auto it = m_tracks.constFind(doc);
        if (it != m_tracks.constEnd() && !it->detail.isEmpty()) {
            result = it->state;
            text = it->detail;
        } else if (m_manager->isUsingTempBundle(doc)) {
            text = tr("Kept safe. Press Ctrl+S to choose a place.");
        } else {
            text = tr("Saved");
        }
    }
    if (detail) {
        *detail = text;
    }
    return result;
}
