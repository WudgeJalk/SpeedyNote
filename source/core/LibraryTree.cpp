#include "LibraryTree.h"
#include "../sharing/NotebookExporter.h"
#include "../sharing/NotebookImporter.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QSaveFile>
#include <QUuid>

const QString LibraryTree::META_FILE = QStringLiteral(".speedynote-section.json");
const QString LibraryTree::PACK_SUFFIX = QStringLiteral(".snbpack");

static const char* PACK_FORMAT = "speedynote-notebook";

static QString fail(QString* error, const QString& why)
{
    if (error) *error = why;
    return QString();
}

static QString parentOf(const QString& path)
{
    return QFileInfo(QDir::cleanPath(path)).absolutePath();
}

static QJsonObject readJson(const QString& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return QJsonObject();
    return QJsonDocument::fromJson(f.readAll()).object();
}

static bool writeJson(const QString& file, const QJsonObject& obj)
{
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return f.commit();
}

static QStringList toStringList(const QJsonValue& value)
{
    QStringList list;
    for (const QJsonValue& v : value.toArray()) list << v.toString();
    return list;
}

bool LibraryTree::isNote(const QString& path)
{
    return QFileInfo(QDir::cleanPath(path)).fileName().endsWith(".snb", Qt::CaseInsensitive);
}

QString LibraryTree::displayName(const QString& path)
{
    QString name = QFileInfo(QDir::cleanPath(path)).fileName();
    if (name.endsWith(".snb", Qt::CaseInsensitive)) name.chop(4);
    return name;
}

QStringList LibraryTree::children(const QString& dir)
{
    if (dir.isEmpty()) return {};  // QDir("") would list the working folder
    QStringList onDisk = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                             QDir::Name | QDir::IgnoreCase | QDir::LocaleAware);
    QStringList ordered;
    for (const QString& name : toStringList(readMeta(dir).value("order"))) {
        if (onDisk.removeOne(name)) ordered << name;  // gone or repeated entries drop out
    }
    return ordered + onDisk;
}

QStringList LibraryTree::sections(const QString& dir)
{
    QStringList paths;
    for (const QString& name : children(dir))
        if (!isNote(name)) paths << dir + "/" + name;
    return paths;
}

QStringList LibraryTree::notes(const QString& dir)
{
    QStringList paths;
    for (const QString& name : children(dir))
        if (isNote(name)) paths << dir + "/" + name;
    return paths;
}

QStringList LibraryTree::notesUnder(const QString& path)
{
    if (isNote(path)) return {path};
    if (path.isEmpty()) return {};
    QStringList found;
    QDirIterator it(path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString dir = it.next();
        if (isNote(dir)) found << dir;
    }
    return found;
}

QJsonObject LibraryTree::readMeta(const QString& dir)
{
    return readJson(dir + "/" + META_FILE);
}

bool LibraryTree::writeMeta(const QString& dir, const QJsonObject& meta)
{
    return writeJson(dir + "/" + META_FILE, meta);
}

QColor LibraryTree::color(const QString& dir)
{
    return QColor(readMeta(dir).value("color").toString());
}

bool LibraryTree::setColor(const QString& dir, const QColor& color)
{
    QJsonObject meta = readMeta(dir);
    if (color.isValid()) meta["color"] = color.name();
    else meta.remove("color");
    return writeMeta(dir, meta);
}

bool LibraryTree::markNotebook(const QString& dir)
{
    QJsonObject meta = readMeta(dir);
    if (meta.value("notebook").toBool()) return true;
    meta["notebook"] = true;
    return writeMeta(dir, meta);
}

bool LibraryTree::placeBefore(const QString& dir, const QString& name, const QString& beforeName)
{
    if (name == beforeName) return true;  // dropped on itself
    QStringList order = children(dir);
    order.removeAll(name);
    const int at = beforeName.isEmpty() ? -1 : int(order.indexOf(beforeName));
    order.insert(at < 0 ? int(order.size()) : at, name);
    QJsonObject meta = readMeta(dir);
    meta["order"] = QJsonArray::fromStringList(order);
    return writeMeta(dir, meta);
}

void LibraryTree::dropFromOrder(const QString& dir, const QString& name)
{
    QJsonObject meta = readMeta(dir);
    if (!meta.contains("order")) return;  // never create the file in a folder that has none
    QStringList order = toStringList(meta.value("order"));
    if (order.removeAll(name) == 0) return;
    meta["order"] = QJsonArray::fromStringList(order);
    writeMeta(dir, meta);
}

QString LibraryTree::checkName(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return QObject::tr("The name is empty.");
    if (n == "." || n == "..") return QObject::tr("This name is not allowed.");
    for (const QChar c : n) {
        if (c.unicode() < 32 || QStringLiteral("\\/:*?\"<>|").contains(c))
            return QObject::tr("A name cannot contain \\ / : * ? \" < > |");
    }
    if (n.endsWith('.')) return QObject::tr("A name cannot end with a dot.");
    if (n.endsWith(".snb", Qt::CaseInsensitive)) return QObject::tr("A name cannot end with .snb.");
    return QString();
}

QString LibraryTree::uniqueName(const QString& dir, const QString& base, const QString& suffix)
{
    QString name = base + suffix;
    for (int i = 2; QFileInfo::exists(dir + "/" + name); ++i)
        name = QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(suffix);
    return name;
}

static QString makeFolder(const QString& parentDir, const QString& name, QString* error)
{
    const QString n = name.trimmed();
    const QString why = LibraryTree::checkName(n);
    if (!why.isEmpty()) return fail(error, why);
    const QString path = QDir::cleanPath(parentDir) + "/" + n;
    if (QFileInfo::exists(path)) return fail(error, QObject::tr("\"%1\" already exists here.").arg(n));
    if (!QDir().mkdir(path)) return fail(error, QObject::tr("Could not create the folder:\n%1").arg(path));
    return path;
}

QString LibraryTree::createNotebook(const QString& parentDir, const QString& name, QString* error)
{
    const QString path = makeFolder(parentDir, name, error);
    if (!path.isEmpty()) markNotebook(path);
    return path;
}

QString LibraryTree::createSection(const QString& parentDir, const QString& name, QString* error)
{
    const QString path = makeFolder(parentDir, name, error);
    if (!path.isEmpty()) placeBefore(parentOf(path), QFileInfo(path).fileName());
    return path;
}

// Keeps document.json in step with the folder: the app shows the name it holds.
static void editManifest(const QString& bundle, const QJsonObject& changes)
{
    const QString file = bundle + "/document.json";
    QJsonObject manifest = readJson(file);
    if (manifest.isEmpty()) return;
    for (auto it = changes.begin(); it != changes.end(); ++it) manifest[it.key()] = it.value();
    writeJson(file, manifest);
}

QString LibraryTree::rename(const QString& path, const QString& newName, QString* error)
{
    const QString src = QDir::cleanPath(path);
    const QString n = newName.trimmed();
    const QString why = checkName(n);
    if (!why.isEmpty()) return fail(error, why);

    const bool note = isNote(src);
    const QString dir = parentOf(src);
    const QString oldName = QFileInfo(src).fileName();
    const QString newFolder = note ? n + ".snb" : n;
    if (newFolder == oldName) return src;
    const QString target = dir + "/" + newFolder;
    // A change of case only finds the same folder on Windows and macOS.
    if (QFileInfo::exists(target) && oldName.compare(newFolder, Qt::CaseInsensitive) != 0)
        return fail(error, QObject::tr("\"%1\" already exists here.").arg(n));

    QJsonObject meta = readMeta(dir);
    if (!QDir().rename(src, target))
        return fail(error, QObject::tr("Could not rename \"%1\". A file inside may be open in another program.")
                               .arg(displayName(src)));
    if (meta.contains("order")) {  // keep its place in the order
        QStringList order = toStringList(meta.value("order"));
        const int i = int(order.indexOf(oldName));
        if (i >= 0) {
            order[i] = newFolder;
            meta["order"] = QJsonArray::fromStringList(order);
            writeMeta(dir, meta);
        }
    }
    if (note) editManifest(target, {{"name", n}});
    return target;
}

bool LibraryTree::copyFolder(const QString& from, const QString& to)
{
    if (QFileInfo::exists(to) || !QDir().mkpath(to)) return false;  // never mix into an existing folder
    bool ok = true;
    QDirIterator it(from, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (ok && it.hasNext()) {
        const QString source = it.next();
        const QString dest = to + source.mid(from.size());
        ok = it.fileInfo().isDir() ? QDir().mkpath(dest) : QFile::copy(source, dest);
    }
    if (!ok) QDir(to).removeRecursively();
    return ok;
}

QString LibraryTree::move(const QString& path, const QString& newParent,
                          const QString& beforeName, QString* error)
{
    const QString src = QDir::cleanPath(path);
    const QString dst = QDir::cleanPath(newParent);
    const QString name = QFileInfo(src).fileName();
    const QString oldParent = parentOf(src);
    if (!QFileInfo(src).isDir()) return fail(error, QObject::tr("This folder is gone:\n%1").arg(src));
    if (!QFileInfo(dst).isDir()) return fail(error, QObject::tr("This folder is gone:\n%1").arg(dst));
    if (oldParent == dst) {
        placeBefore(dst, name, beforeName);
        return src;
    }
    if (dst == src || dst.startsWith(src + "/"))
        return fail(error, QObject::tr("A section cannot go inside itself."));
    const QString target = dst + "/" + name;
    if (QFileInfo::exists(target))
        return fail(error, QObject::tr("\"%1\" already exists in \"%2\".")
                               .arg(displayName(src), displayName(dst)));
    if (!QDir().rename(src, target)) {
        // Another drive: copy, and delete the original only once the copy is whole.
        if (!copyFolder(src, target))
            return fail(error, QObject::tr("Could not move \"%1\". A file inside may be open in another program.")
                                   .arg(displayName(src)));
        QDir(src).removeRecursively();
    }
    dropFromOrder(oldParent, name);
    placeBefore(dst, name, beforeName);
    return target;
}

QString LibraryTree::copyNoteInto(const QString& notePath, const QString& dir, QString* error)
{
    const QString src = QDir::cleanPath(notePath);
    if (!QFile::exists(src + "/document.json"))
        return fail(error, QObject::tr("This folder is not a SpeedyNote note (no document.json):\n%1").arg(src));
    const QString name = uniqueName(dir, displayName(src), ".snb");
    const QString target = QDir::cleanPath(dir) + "/" + name;
    if (!copyFolder(src, target))
        return fail(error, QObject::tr("Could not copy \"%1\".").arg(displayName(src)));
    // A new id: two notes with one id would open as the same tab.
    editManifest(target, {{"notebook_id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                          {"name", displayName(target)}});
    placeBefore(dir, name);
    return target;
}

bool LibraryTree::trash(const QString& path, QString* error)
{
    const QString clean = QDir::cleanPath(path);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    if (QFile::moveToTrash(clean)) {
        dropFromOrder(parentOf(clean), QFileInfo(clean).fileName());
        return true;
    }
#endif
    fail(error, QObject::tr("Could not move \"%1\" to the recycle bin.").arg(displayName(clean)));
    return false;
}

bool LibraryTree::pack(const QString& notebookDir, const QString& packFile, QString* error)
{
    const QString root = QDir::cleanPath(notebookDir);
    if (!QFileInfo(root).isDir())
        return fail(error, QObject::tr("The notebook folder is missing:\n%1").arg(root)), false;
    if (QDir::cleanPath(QFileInfo(packFile).absoluteFilePath()).startsWith(root + "/"))
        return fail(error, QObject::tr("Save the pack outside the notebook folder.")), false;

    // Check everything before writing: a half-synced folder must not give a half pack.
    for (const QString& note : notesUnder(root)) {
        if (!QFile::exists(note + "/document.json"))
            return fail(error, QObject::tr("The note \"%1\" has no document.json. The folder may be "
                                           "damaged or not fully synced:\n%2")
                                   .arg(displayName(note), note)), false;
    }
    QDirIterator it(root, {QStringLiteral("*.json")}, QDir::Files | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        QFile f(file);
        QJsonParseError parse;
        parse.error = QJsonParseError::GarbageAtEnd;
        if (f.open(QIODevice::ReadOnly)) QJsonDocument::fromJson(f.readAll(), &parse);
        if (parse.error != QJsonParseError::NoError)
            return fail(error, QObject::tr("This file is damaged or not fully synced:\n%1").arg(file)), false;
    }

    const auto result = NotebookExporter::exportFolderPackage(
        root, packFile, QJsonObject{{"format", PACK_FORMAT}, {"version", 1}});
    if (!result.success) return fail(error, result.errorMessage), false;
    return true;
}

QString LibraryTree::unpack(const QString& packFile, const QString& parentDir, QString* error)
{
    const auto result = NotebookImporter::importFolderPackage(packFile, QDir::cleanPath(parentDir),
                                                              PACK_FORMAT);
    if (!result.success) return fail(error, result.errorMessage);
    markNotebook(result.extractedSnbPath);
    return result.extractedSnbPath;
}
