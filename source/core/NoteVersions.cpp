#include "NoteVersions.h"

#include "Document.h"
#include "../platform/BundleFile.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QThreadPool>
#include <QTime>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <functional>

namespace NoteVersions {
namespace {

const QDate kEpoch(1970, 1, 1);

QString tr(const char* text)
{
    return QCoreApplication::translate("NoteVersions", text);
}

bool isVersioned(const QString& rel)
{
    if (rel == QLatin1String("document.json")) return true;
    return (rel.startsWith(QLatin1String("pages/")) || rel.startsWith(QLatin1String("tiles/")))
        && rel.count(QLatin1Char('/')) == 1
        && rel.endsWith(QLatin1String(".json")) && !rel.endsWith(QLatin1String(".ocr.json"));
}

// versions/document, versions/pages/<uuid>, versions/tiles/<x,y>
QString folderFor(const QString& bundle, const QString& rel)
{
    return bundle + QStringLiteral("/versions/") + rel.chopped(5);
}

QString versionPath(const QString& folder, qint64 secs)
{
    return folder + QLatin1Char('/') + nameForTime(secs) + QStringLiteral(".json");
}

qint64 writtenAt(const QFileInfo& info)
{
    return info.lastModified().toSecsSinceEpoch();
}

qint64 nowOr(qint64 nowSecs)
{
    return nowSecs >= 0 ? nowSecs : QDateTime::currentSecsSinceEpoch();
}

// The version times of one folder, oldest first.
QVector<qint64> timesIn(const QString& folder)
{
    QVector<qint64> times;
    const QStringList names = QDir(folder).entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString& name : names) {
        const qint64 t = timeForName(name.chopped(5));
        if (t >= 0) times.append(t);
    }
    std::sort(times.begin(), times.end());
    return times;
}

void pruneFolder(const QString& folder, const Policy& policy, bool keepNewest, qint64 now)
{
    const QVector<qint64> times = timesIn(folder);
    const qint64 tooOld = now - qint64(qMax(1, policy.days)) * 86400;
    const int firstKept = times.size() - qMax(1, policy.count);
    for (int i = 0; i < times.size(); ++i) {
        if (keepNewest && i == times.size() - 1) continue;
        if (times[i] < tooOld || i < firstKept) {
            QFile::remove(versionPath(folder, times[i]));
        }
    }
}

// When the newest version of a folder was written. For a removed page that is
// the time of its removal: a removal always writes a last version.
qint64 lastWrittenIn(const QString& folder)
{
    qint64 latest = -1;
    for (const QFileInfo& file : QDir(folder).entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        latest = qMax(latest, writtenAt(file));
    }
    return latest;
}

bool copyInto(const QString& source, const QString& dest)
{
    return QDir().mkpath(QFileInfo(dest).absolutePath()) && QFile::copy(source, dest);
}

// A page id or tile coordinate read from a manifest becomes a file name.
bool isPlainName(const QString& name)
{
    return !name.isEmpty() && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'))
        && !name.contains(QLatin1String(".."));
}

}  // namespace

QString nameForTime(qint64 secs)
{
    const qint64 days = secs >= 0 ? secs / 86400 : (secs - 86399) / 86400;
    const int rest = int(secs - days * 86400);
    return kEpoch.addDays(days).toString(QStringLiteral("yyyyMMdd")) + QLatin1Char('-')
         + QTime(0, 0).addSecs(rest).toString(QStringLiteral("HHmmss"));
}

qint64 timeForName(const QString& baseName)
{
    if (baseName.size() != 15 || baseName.at(8) != QLatin1Char('-')) return -1;
    const QDate date = QDate::fromString(baseName.left(8), QStringLiteral("yyyyMMdd"));
    const QTime time = QTime::fromString(baseName.mid(9), QStringLiteral("HHmmss"));
    if (!date.isValid() || !time.isValid()) return -1;
    return kEpoch.daysTo(date) * 86400 + QTime(0, 0).secsTo(time);
}

Policy policyFromSettings()
{
    QSettings settings("SpeedyNote", "App");
    Policy policy;
    policy.enabled = settings.value("backup/enabled", true).toBool();
    policy.count = qBound(1, settings.value("backup/count", 10).toInt(), 100);
    policy.days = qBound(1, settings.value("backup/days", 30).toInt(), 365);
    policy.minutes = qBound(1, settings.value("backup/minutes", 15).toInt(), 240);
    return policy;
}

void keepVersion(const QString& bundle, const QString& relPath, const Policy& policy,
                 bool force, qint64 nowSecs)
{
    if (!policy.enabled || !isVersioned(relPath)) return;
    const QFileInfo source(bundle + QLatin1Char('/') + relPath);
    if (!source.isFile()) return;

    const qint64 written = writtenAt(source);
    const QString folder = folderFor(bundle, relPath);
    const QVector<qint64> times = timesIn(folder);
    if (times.contains(written)) return;  // this content is already kept
    if (!force && !times.isEmpty() && written - times.last() < qint64(policy.minutes) * 60) return;

    QFile in(source.filePath());
    if (!QDir().mkpath(folder) || !in.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot keep a version of" << source.filePath();
        return;
    }
    const QByteArray data = in.readAll();
    in.close();

    // Not Document::commitFile: a backup must not use up its test fault hook,
    // and a failed backup never fails the save. Still atomic: no half version.
    BundleFile out(versionPath(folder, written));
    out.setDirectWriteFallback(false);
    bool ok = out.open(QIODevice::WriteOnly) && out.write(data) == data.size();
    if (ok) {
        ok = out.commit();
    } else if (out.isOpen()) {
        out.cancelWriting();
    }
    if (!ok) {
        qWarning() << "Cannot keep a version of" << source.filePath() << ":" << out.errorString();
        QFile::remove(versionPath(folder, written));  // a new name: only a half file (HarmonyOS writes in place)
        return;
    }
    pruneFolder(folder, policy, /*keepNewest=*/true, nowOr(nowSecs));
}

void pruneVersions(const QString& bundle, const Policy& policy, qint64 nowSecs)
{
    const qint64 now = nowOr(nowSecs);
    auto pruneOne = [&](const QString& rel) {
        const QString folder = folderFor(bundle, rel);
        // A removed page keeps its last version for `days` after its removal,
        // not after its content was written (a version is named by that).
        const bool alive = QFileInfo::exists(bundle + QLatin1Char('/') + rel);
        const bool keepNewest = alive || now - lastWrittenIn(folder) < qint64(qMax(1, policy.days)) * 86400;
        pruneFolder(folder, policy, keepNewest, now);
        QDir().rmdir(folder);  // only when it is empty
    };
    pruneOne(QStringLiteral("document.json"));
    for (const QString kind : {QStringLiteral("pages"), QStringLiteral("tiles")}) {
        const QStringList names = QDir(bundle + QStringLiteral("/versions/") + kind)
                                      .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& name : names) {
            pruneOne(kind + QLatin1Char('/') + name + QStringLiteral(".json"));
        }
    }
}

void pruneVersionsLater(const QString& bundle)
{
    if (bundle.isEmpty() || !QFileInfo::exists(bundle + QStringLiteral("/versions"))) return;
    const Policy policy = policyFromSettings();
    (void)QtConcurrent::run(QThreadPool::globalInstance(), [bundle, policy]() {
        pruneVersions(bundle, policy);
    });
}

QVector<qint64> versionTimes(const QString& bundle)
{
    QSet<qint64> minutes;
    QDirIterator it(bundle + QStringLiteral("/versions"), {QStringLiteral("*.json")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const qint64 t = timeForName(it.fileInfo().completeBaseName());
        if (t >= 0) minutes.insert(t - t % 60);
    }
    QVector<qint64> times;
    for (qint64 t : minutes) times.append(t);
    std::sort(times.begin(), times.end(), std::greater<qint64>());
    return times;
}

QString fileAt(const QString& bundle, const QString& relPath, qint64 at, bool* newer)
{
    if (newer) *newer = false;
    const QString folder = folderFor(bundle, relPath);
    QVector<QPair<qint64, QString>> candidates;
    for (qint64 t : timesIn(folder)) candidates.append({t, versionPath(folder, t)});
    const QFileInfo current(bundle + QLatin1Char('/') + relPath);
    if (current.isFile()) candidates.append({writtenAt(current), current.filePath()});
    if (candidates.isEmpty()) return {};
    std::sort(candidates.begin(), candidates.end());

    QString best;
    for (const auto& candidate : candidates) {
        if (candidate.first <= at) best = candidate.second;
    }
    if (best.isEmpty()) {
        if (newer) *newer = true;
        best = candidates.first().second;
    }
    return best;
}

Plan planAt(const QString& bundle, qint64 at)
{
    Plan plan;
    bool newer = false;
    plan.manifest = fileAt(bundle, QStringLiteral("document.json"), at, &newer);
    plan.someNewer = newer;
    QFile file(plan.manifest);
    if (plan.manifest.isEmpty() || !file.open(QIODevice::ReadOnly)) return plan;
    const QJsonObject manifest = QJsonDocument::fromJson(file.readAll()).object();
    plan.edgeless = manifest.value("mode").toString() == QLatin1String("edgeless");

    const QJsonArray ids = manifest.value(QLatin1String(plan.edgeless ? "tile_index" : "page_order")).toArray();
    const QString dir = plan.edgeless ? QStringLiteral("tiles/") : QStringLiteral("pages/");
    QSet<QString> listed;
    for (const QJsonValue& id : ids) {
        listed.insert(id.toString());
        if (!isPlainName(id.toString())) continue;
        const QString rel = dir + id.toString() + QStringLiteral(".json");
        const QString source = fileAt(bundle, rel, at, &newer);
        if (source.isEmpty()) continue;  // a PDF page drawn from the PDF alone
        plan.files.append({rel, source});
        plan.someNewer |= newer;
    }

    // A page removed after `at` is missing from a manifest saved after it,
    // and the manifests before it may be pruned already. Its last version was
    // written when it was removed; a version at or before `at` says it existed.
    const QStringList kept = QDir(bundle + QStringLiteral("/versions/") + dir)
                                 .entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& id : kept) {
        const QString rel = dir + id + QStringLiteral(".json");
        if (listed.contains(id) || !isPlainName(id) || QFileInfo::exists(bundle + QLatin1Char('/') + rel)
            || lastWrittenIn(folderFor(bundle, rel)) <= at) {
            continue;
        }
        const QString source = fileAt(bundle, rel, at, &newer);
        if (source.isEmpty() || newer) continue;  // made after `at`
        plan.files.append({rel, source});
        plan.removedIds.append(id);
    }
    return plan;
}

bool buildCopy(const QString& bundle, qint64 at, const QString& destBundle,
               const QString& name, QString* error)
{
    if (destBundle.isEmpty() || QFileInfo::exists(destBundle)) {
        if (error) *error = tr("The folder for the copy already exists.");
        return false;
    }
    auto fail = [&](const QString& why) {
        QDir(destBundle).removeRecursively();
        if (error) *error = why;
        return false;
    };

    const Plan plan = planAt(bundle, at);
    QFile manifestFile(plan.manifest);
    if (plan.manifest.isEmpty() || !manifestFile.open(QIODevice::ReadOnly)) {
        return fail(tr("No saved version of this note was found."));
    }
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    if (manifest.isEmpty()) {
        return fail(tr("The saved version of this note cannot be read."));
    }
    const QString copyError = tr("The copy could not be written. The disk may be full.");

    // Every other file of the note: pictures, markdown notes, bundled PDFs.
    // Plain copies: document.json, written last, is what makes the folder a note.
    const QDir from(bundle);
    QDirIterator it(bundle, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = from.relativeFilePath(path);
        const QString top = rel.section(QLatin1Char('/'), 0, 0);
        if (top == QLatin1String("pages") || top == QLatin1String("tiles")
            || top == QLatin1String("versions") || rel.startsWith(QLatin1String("document.json"))) {
            continue;
        }
        if (!copyInto(path, destBundle + QLatin1Char('/') + rel)) return fail(copyError);
    }
    for (const auto& file : plan.files) {
        if (!copyInto(file.second, destBundle + QLatin1Char('/') + file.first)) return fail(copyError);
    }
    QDir().mkpath(destBundle + (plan.edgeless ? QStringLiteral("/tiles") : QStringLiteral("/pages")));

    // Pages removed since: back at the end (their place is not known), with their size.
    if (!plan.removedIds.isEmpty()) {
        const QString key = plan.edgeless ? QStringLiteral("tile_index") : QStringLiteral("page_order");
        QJsonArray ids = manifest.value(key).toArray();
        QJsonObject sizes = manifest.value(QStringLiteral("page_metadata")).toObject();
        for (const QString& id : plan.removedIds) {
            ids.append(id);
            if (plan.edgeless || !manifest.contains(QStringLiteral("page_metadata"))) continue;
            QFile pageFile(destBundle + QStringLiteral("/pages/") + id + QStringLiteral(".json"));
            const QJsonObject page = pageFile.open(QIODevice::ReadOnly)
                ? QJsonDocument::fromJson(pageFile.readAll()).object() : QJsonObject();
            sizes[id] = QJsonObject{{"width", page.value("width").toDouble(816)},
                                    {"height", page.value("height").toDouble(1056)}};
        }
        manifest[key] = ids;
        if (manifest.contains(QStringLiteral("page_metadata"))) manifest["page_metadata"] = sizes;
    }

    manifest["notebook_id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    manifest["name"] = name;
    QString writeError;
    if (!Document::commitFile(destBundle + QStringLiteral("/document.json"),
                              QJsonDocument(manifest).toJson(QJsonDocument::Indented), &writeError)) {
        return fail(writeError);
    }
    return true;
}

}  // namespace NoteVersions
