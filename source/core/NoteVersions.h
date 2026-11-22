#pragma once

// ============================================================================
// NoteVersions - earlier versions of the pages of a note (Step R4)
// ============================================================================
// Before a save replaces or removes a page, a tile or document.json, the old
// file is copied into the note's own folder:
//   <bundle>/versions/pages/<uuid>/<yyyyMMdd-HHmmss>.json
//   <bundle>/versions/tiles/<x,y>/<yyyyMMdd-HHmmss>.json
//   <bundle>/versions/document/<yyyyMMdd-HHmmss>.json
// The name is the time the old file was written (UTC), so it says when that
// content was the note. A new version is kept when the page has none yet, or
// when its newest one is at least `minutes` older.
//
// Disk use is bounded per page: at most `count` versions, none older than
// `days` (the newest one stays while the page exists). So the versions of a
// note take at most `count` times the size of the pages that changed; a page
// never changed has none. Old app versions ignore the folder.
// ============================================================================

#include <QString>
#include <QStringList>
#include <QVector>

namespace NoteVersions {

struct Policy {
    bool enabled = true;
    int count = 10;    ///< Versions kept per page
    int days = 30;     ///< Older versions are dropped (not the newest of a live page)
    int minutes = 15;  ///< At most one new version per page in this time
};

/// backup/enabled, backup/count, backup/days, backup/minutes.
Policy policyFromSettings();

/// Call before <bundle>/<relPath> is replaced or removed. Copies the old file
/// into versions/ when the policy asks for it (always when `force`, used before
/// a removal). Only document.json and page/tile files are versioned; any other
/// path is ignored. Any thread. A failure is only logged: it never fails a save.
void keepVersion(const QString& bundle, const QString& relPath, const Policy& policy,
                 bool force = false, qint64 nowSecs = -1);

/// Drops the versions over `count` or older than `days` in every folder. The
/// newest one stays while its page exists; folders of removed pages go by age.
void pruneVersions(const QString& bundle, const Policy& policy, qint64 nowSecs = -1);
/// pruneVersions() with the current settings, on a worker thread.
void pruneVersionsLater(const QString& bundle);

/// The times at which versions were kept, to the minute, newest first
/// (seconds since 1970, UTC).
QVector<qint64> versionTimes(const QString& bundle);

/// The file <relPath> as it was at `at`: the newest of its versions and its
/// current file that is not newer than `at`. When all are newer: the oldest
/// one, and *newer is set. Empty when there is none.
QString fileAt(const QString& bundle, const QString& relPath, qint64 at, bool* newer = nullptr);

/// What a copy of the note at `at` is made of.
struct Plan {
    QString manifest;                              ///< document.json to use
    QVector<QPair<QString, QString>> files;        ///< relative path -> source file
    QStringList removedIds;                        ///< Pages/tiles in `files` that no manifest at `at` lists
    bool edgeless = false;
    bool someNewer = false;                        ///< A file had no copy at or before `at`
};
Plan planAt(const QString& bundle, qint64 at);

/// Builds a new note in `destBundle` (which must not exist) from planAt(at):
/// that manifest with a new notebook id and `name`, those pages or tiles, and
/// every other file of the note (pictures, bundled PDFs). Never writes into
/// `bundle`. On failure nothing is left in `destBundle`.
bool buildCopy(const QString& bundle, qint64 at, const QString& destBundle,
               const QString& name, QString* error = nullptr);

/// "yyyyMMdd-HHmmss" of a time in seconds since 1970 (UTC), and back (-1 if invalid).
QString nameForTime(qint64 secs);
qint64 timeForName(const QString& baseName);

}  // namespace NoteVersions
