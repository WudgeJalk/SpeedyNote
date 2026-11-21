#pragma once

// ============================================================================
// LibraryTree - the library on disk (roadmap C1), no widgets
// ============================================================================
// A notebook is a plain folder, a section (or sub-section) is a sub-folder at
// any depth, a note is a .snb bundle. Each notebook or section folder may hold
// one small file, META_FILE, with:
//   "order":    the names of its children (sections and notes, as on disk)
//   "color":    the section colour ("#rrggbb"), optional
//   "notebook": true at the notebook root
// The file is optional: without it a folder lists alphabetically. Entries of
// the order that are gone are dropped, folders not in it come last. Listing
// reads only folder names and this file, never a note.
// ============================================================================

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QStringList>

class LibraryTree {
public:
    static const QString META_FILE;    ///< ".speedynote-section.json"
    static const QString PACK_SUFFIX;  ///< ".snbpack": a whole notebook in one file

    static bool isNote(const QString& path);
    static QString displayName(const QString& path);  ///< folder name without ".snb"

    /// Child folder names of dir, in the saved order (see above).
    static QStringList children(const QString& dir);
    static QStringList sections(const QString& dir);  ///< full paths, in order
    static QStringList notes(const QString& dir);     ///< full paths, in order
    /// path itself if it is a note, else every note below it.
    static QStringList notesUnder(const QString& path);

    static QJsonObject readMeta(const QString& dir);  ///< empty if missing or damaged
    static bool writeMeta(const QString& dir, const QJsonObject& meta);
    static QColor color(const QString& dir);          ///< invalid if none
    static bool setColor(const QString& dir, const QColor& color);  ///< invalid clears it
    static bool markNotebook(const QString& dir);

    /// Moves name just before beforeName in dir's order (last if beforeName is empty
    /// or not there) and saves the whole order.
    static bool placeBefore(const QString& dir, const QString& name,
                            const QString& beforeName = QString());

    /// Empty if the name can be a folder name, else the reason.
    static QString checkName(const QString& name);
    /// base + suffix, or "base (2)" + suffix ... if that name is taken in dir.
    static QString uniqueName(const QString& dir, const QString& base,
                              const QString& suffix = QString());

    // Each returns the new path, or an empty string and sets *error.
    static QString createNotebook(const QString& parentDir, const QString& name, QString* error);
    static QString createSection(const QString& parentDir, const QString& name, QString* error);
    /// newName without ".snb"; a note keeps its suffix and its document.json name follows.
    static QString rename(const QString& path, const QString& newName, QString* error);
    /// Into newParent, just before beforeName. Refuses a name clash and a move into itself.
    static QString move(const QString& path, const QString& newParent,
                        const QString& beforeName, QString* error);
    /// Copies a note into dir (renamed " (2)" on a clash) with a new notebook id.
    static QString copyNoteInto(const QString& notePath, const QString& dir, QString* error);
    /// To the system recycle bin. False (and *error) where Qt cannot do it.
    static bool trash(const QString& path, QString* error);

    /// One file with the whole notebook. Fails, leaving no file, on a damaged or
    /// half-synced folder (missing document.json, unreadable or broken JSON).
    static bool pack(const QString& notebookDir, const QString& packFile, QString* error);
    /// Unpacks into a new folder in parentDir; returns it, or empty and *error.
    static QString unpack(const QString& packFile, const QString& parentDir, QString* error);

private:
    static bool copyFolder(const QString& from, const QString& to);
    static void dropFromOrder(const QString& dir, const QString& name);
};
