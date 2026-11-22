#pragma once

// ============================================================================
// NoteVersionsTests - earlier versions of pages (Step R4). Group: --test-versions
// ============================================================================
// 1. Spacing and count: a version only every `minutes`, at most `count`.
// 2. Age: versions older than `days` go; the newest stays while the page
//    exists; the folder of a removed page goes.
// 3. fileAt picks the newest file not newer than the time, else the oldest.
// 4. A real note: the save writes versions (a removed page too), a copy at an
//    earlier time has the old pages and a new id, versions/ is never copied
//    or packed. The dialog is saved as a picture in the temp folder.
// Files get chosen modification times (QFileDevice::setFileTime) so nothing
// waits on the clock. No setting of the user is read or written.
// ============================================================================

#include "NoteVersions.h"
#include "Document.h"
#include "Page.h"
#include "../layers/VectorLayer.h"
#include "../sharing/NotebookExporter.h"
#include "../sharing/NotebookImporter.h"
#include "../ui/dialogs/VersionsDialog.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <cstdio>

namespace NoteVersionsTests {

inline bool check(bool ok, const QString& what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    std::fflush(stdout);
    return ok;
}

inline bool setWritten(const QString& path, qint64 secs)
{
    QFile file(path);
    return file.open(QIODevice::ReadWrite)
        && file.setFileTime(QDateTime::fromSecsSinceEpoch(secs), QFileDevice::FileModificationTime);
}

inline void writeFile(const QString& path, const QByteArray& data, qint64 secs)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) file.write(data);
    }
    setWritten(path, secs);
}

inline QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

inline int versionCount(const QString& folder)
{
    return QDir(folder).entryList({QStringLiteral("*.json")}, QDir::Files).size();
}

// A minute boundary in the past, younger than the default 30 days: a save
// prunes against the real clock.
inline qint64 baseTime()
{
    const qint64 t = QDateTime::currentSecsSinceEpoch() - 2 * 86400;
    return t - t % 60;
}

inline bool testSpacingAndCount()
{
    std::printf("--- spacing and count ---\n");
    QTemporaryDir tmp;
    const QString bundle = tmp.path() + "/n.snb";
    const QString page = bundle + "/pages/p.json";
    const QString folder = bundle + "/versions/pages/p";
    const qint64 t0 = baseTime();
    NoteVersions::Policy policy;
    policy.count = 3;
    policy.minutes = 15;
    bool ok = true;

    ok &= check(NoteVersions::timeForName(NoteVersions::nameForTime(t0)) == t0,
                "a version name gives back its time");
    writeFile(page, "A", t0);
    NoteVersions::keepVersion(bundle, "pages/p.json", policy, false, t0);
    ok &= check(versionCount(folder) == 1, "the first change always keeps a version");
    writeFile(page, "B", t0 + 5 * 60);
    NoteVersions::keepVersion(bundle, "pages/p.json", policy, false, t0);
    ok &= check(versionCount(folder) == 1, "no new version 5 min after the last one");
    writeFile(page, "C", t0 + 20 * 60);
    NoteVersions::keepVersion(bundle, "pages/p.json", policy, false, t0);
    NoteVersions::keepVersion(bundle, "pages/p.json", policy, false, t0);
    ok &= check(versionCount(folder) == 2, "a new version 20 min later, kept once");
    writeFile(page, "D", t0 + 25 * 60);
    NoteVersions::keepVersion(bundle, "pages/p.json", policy, /*force=*/true, t0);
    ok &= check(versionCount(folder) == 3, "a removal always keeps a version");
    writeFile(page, "E", t0 + 60 * 60);
    NoteVersions::keepVersion(bundle, "pages/p.json", policy, false, t0);
    ok &= check(versionCount(folder) == 3, "at most `count` versions");
    ok &= check(!QFile::exists(folder + "/" + NoteVersions::nameForTime(t0) + ".json"),
                "the oldest version went first");
    ok &= check(readFile(folder + "/" + NoteVersions::nameForTime(t0 + 3600) + ".json") == "E",
                "a version holds the old file's bytes");

    writeFile(bundle + "/pages/p.ocr.json", "{}", t0);
    writeFile(bundle + "/assets/images/x.png", "x", t0);
    NoteVersions::keepVersion(bundle, "pages/p.ocr.json", policy, true, t0);
    NoteVersions::keepVersion(bundle, "assets/images/x.png", policy, true, t0);
    ok &= check(!QFileInfo::exists(bundle + "/versions/pages/p.ocr")
                    && !QFileInfo::exists(bundle + "/versions/assets"),
                "OCR files and pictures get no versions");
    NoteVersions::Policy off = policy;
    off.enabled = false;
    writeFile(page, "F", t0 + 7200);
    NoteVersions::keepVersion(bundle, "pages/p.json", off, true, t0);
    ok &= check(versionCount(folder) == 3, "switched off: no version");
    return ok;
}

inline bool testAge()
{
    std::printf("--- age ---\n");
    QTemporaryDir tmp;
    const QString bundle = tmp.path() + "/n.snb";
    const qint64 t0 = baseTime();
    NoteVersions::Policy policy;
    policy.minutes = 1;
    for (const char* name : {"p", "q"}) {
        const QString rel = QStringLiteral("pages/%1.json").arg(QLatin1String(name));
        for (int i = 0; i < 4; ++i) {
            writeFile(bundle + "/" + rel, QByteArray::number(i), t0 + i * 3600);
            NoteVersions::keepVersion(bundle, rel, policy, false, t0);
        }
    }
    writeFile(bundle + "/document.json", "{}", t0);
    NoteVersions::keepVersion(bundle, "document.json", policy, false, t0);
    QFile::remove(bundle + "/pages/q.json");  // q was removed from the note

    bool ok = true;
    NoteVersions::Policy keep10days = policy;
    keep10days.days = 10;
    NoteVersions::pruneVersions(bundle, keep10days, t0 + 5 * 86400);
    ok &= check(versionCount(bundle + "/versions/pages/p") == 4
                    && versionCount(bundle + "/versions/pages/q") == 4,
                "versions younger than `days` stay");
    NoteVersions::pruneVersions(bundle, keep10days, t0 + 30 * 86400);
    ok &= check(versionCount(bundle + "/versions/pages/p") == 1, "an old page keeps only its newest version");
    ok &= check(readFile(bundle + "/versions/pages/p/" + NoteVersions::nameForTime(t0 + 3 * 3600) + ".json") == "3",
                "and it is the newest");
    ok &= check(!QFileInfo::exists(bundle + "/versions/pages/q"), "a removed page's versions go by age");
    ok &= check(versionCount(bundle + "/versions/document") == 1, "the manifest keeps its newest version");
    return ok;
}

// A page last written 40 days ago and removed today keeps its last version for
// `days` from today, not from 40 days ago.
inline bool testRemovedLongAgo()
{
    std::printf("--- a page removed long after it was written ---\n");
    QTemporaryDir tmp;
    const QString bundle = tmp.path() + "/n.snb";
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    NoteVersions::Policy policy;   // 30 days
    writeFile(bundle + "/pages/old.json", "lecture", now - 40 * 86400);
    NoteVersions::keepVersion(bundle, "pages/old.json", policy, /*force=*/true);   // what a removal does
    QFile::remove(bundle + "/pages/old.json");
    NoteVersions::pruneVersions(bundle, policy, now + 60);
    bool ok = check(versionCount(bundle + "/versions/pages/old") == 1,
                    "the next open keeps the removed page's last version");
    NoteVersions::pruneVersions(bundle, policy, now + 31 * 86400);
    ok &= check(!QFileInfo::exists(bundle + "/versions/pages/old"), "31 days after the removal it goes");
    return ok;
}

inline bool testFileAt()
{
    std::printf("--- fileAt ---\n");
    QTemporaryDir tmp;
    const QString bundle = tmp.path() + "/n.snb";
    const QString rel = "pages/r.json";
    const qint64 t1 = baseTime();
    const qint64 t2 = t1 + 1800;
    const qint64 t3 = t2 + 1800;
    NoteVersions::Policy policy;
    writeFile(bundle + "/" + rel, "R1", t1);
    NoteVersions::keepVersion(bundle, rel, policy, false, t1);
    writeFile(bundle + "/" + rel, "R2", t2);
    NoteVersions::keepVersion(bundle, rel, policy, false, t1);
    writeFile(bundle + "/" + rel, "R3", t3);

    bool ok = true;
    bool newer = false;
    ok &= check(readFile(NoteVersions::fileAt(bundle, rel, t1 - 60, &newer)) == "R1" && newer,
                "before every copy: the oldest, flagged newer");
    ok &= check(readFile(NoteVersions::fileAt(bundle, rel, t1 + 10, &newer)) == "R1" && !newer,
                "between two versions: the older one");
    ok &= check(readFile(NoteVersions::fileAt(bundle, rel, t2 + 10)) == "R2", "at the second version");
    ok &= check(readFile(NoteVersions::fileAt(bundle, rel, t3 + 10)) == "R3", "after the last save: the current file");
    ok &= check(NoteVersions::fileAt(bundle, "pages/none.json", t3).isEmpty(), "no file: empty");
    return ok;
}

inline void fillPage(Page* page, const QString& tag, int strokes, int points = 30)
{
    VectorLayer* layer = page->layer(0);
    layer->clear();
    for (int s = 0; s < strokes; ++s) {
        VectorStroke stroke;
        stroke.id = QStringLiteral("%1-%2").arg(tag).arg(s);
        stroke.color = tag == QLatin1String("new") ? QColor(200, 40, 40) : QColor(30, 30, 30);
        stroke.baseThickness = 4.0;
        for (int p = 0; p < points; ++p) {
            stroke.points.append({QPointF(80 + p * 20, 120 + s * 90 + (p % 2) * 25), 0.6});
        }
        stroke.updateBoundingBox();
        layer->addStroke(std::move(stroke));
    }
}

inline bool saveWith(Document* doc, const NoteVersions::Policy& policy)
{
    const std::shared_ptr<Document::SaveJob> job = doc->prepareSave();
    if (!job) return false;
    job->versions = policy;  // the test never depends on the user's settings
    const bool ok = Document::writeSaveJob(*job);
    doc->finishSave(*job, ok);
    return ok;
}

inline bool testNote()
{
    std::printf("--- a real note ---\n");
    QTemporaryDir tmp;
    const QString folder = tmp.path() + "/Notebook";
    const QString bundle = folder + "/n.snb";
    const qint64 t0 = baseTime();
    const qint64 t1 = t0 + 3600;
    NoteVersions::Policy policy;
    bool ok = true;

    auto doc = Document::createNew("Versions test");
    while (doc->pageCount() < 3) doc->addPage();
    for (int i = 0; i < 3; ++i) fillPage(doc->page(i), "old", 2);
    if (!check(doc->saveBundle(bundle), "first save")) return false;
    const QString p1 = doc->pageUuidAt(1);
    const QString p2 = doc->pageUuidAt(2);
    for (int i = 0; i < 3; ++i) setWritten(bundle + "/pages/" + doc->pageUuidAt(i) + ".json", t0);
    setWritten(bundle + "/document.json", t0);

    fillPage(doc->page(1), "new", 5);
    doc->markPageDirty(1);
    ok &= check(saveWith(doc.get(), policy), "save of a changed page");
    ok &= check(versionCount(bundle + "/versions/pages/" + p1) == 1
                    && versionCount(bundle + "/versions/document") == 1,
                "the save kept the old page and the old manifest");
    ok &= check(!QFileInfo::exists(bundle + "/versions/pages/" + doc->pageUuidAt(0)),
                "an unchanged page has no versions");
    setWritten(bundle + "/pages/" + p1 + ".json", t1);
    setWritten(bundle + "/document.json", t1);

    doc->removePage(2);
    ok &= check(saveWith(doc.get(), policy), "save after a page was removed");
    ok &= check(versionCount(bundle + "/versions/pages/" + p2) == 1, "the removed page was kept");
    ok &= check(versionCount(bundle + "/versions/document") == 2, "a manifest one hour newer was kept");
    const QVector<qint64> times = NoteVersions::versionTimes(bundle);
    ok &= check(times == QVector<qint64>({t1, t0}), "two times, newest first");

    // A copy at t0: the note as it was first saved.
    const QString copyA = tmp.path() + "/a.snb";
    ok &= check(NoteVersions::buildCopy(bundle, t0 + 59, copyA, "Copy A"), "a copy at the first time");
    auto a = Document::loadBundle(copyA);
    ok &= check(a && a->pageCount() == 3, "the copy loads, with the 3 pages of that time");
    if (a && a->pageCount() == 3) {
        ok &= check(a->id != doc->id && a->name == "Copy A", "the copy has a new id and its name");
        ok &= check(a->page(1)->layer(0)->strokes().size() == 2, "page 2 is the old one");
        ok &= check(a->page(2)->layer(0)->strokes().size() == 2, "the removed page is back");
    }
    ok &= check(!QFileInfo::exists(copyA + "/versions"), "versions are not copied");
    ok &= check(!NoteVersions::planAt(bundle, t0 + 59).someNewer, "nothing newer at that time");

    // A copy at t1: the changed page, the removed page still there.
    const QString copyB = tmp.path() + "/b.snb";
    NoteVersions::buildCopy(bundle, t1 + 59, copyB, "Copy B");
    auto b = Document::loadBundle(copyB);
    ok &= check(b && b->pageCount() == 3 && b->page(1)->layer(0)->strokes().size() == 5
                    && b->page(2)->layer(0)->strokes().size() == 2,
                "a copy at the second time has the new page 2 and the old page 3");
    ok &= check(NoteVersions::planAt(bundle, t0 - 3600).someNewer, "before every version: flagged newer");
    ok &= check(!NoteVersions::buildCopy(bundle, t0 + 59, copyB, "x") && QFileInfo::exists(copyB + "/document.json"),
                "a copy never goes into a folder that exists");

    // Packing a notebook leaves the versions out.
    const QString pack = tmp.path() + "/nb.snbpack";
    const auto packed = NotebookExporter::exportFolderPackage(folder, pack,
                                                              QJsonObject{{"format", "versions-test"}});
    const QString unpackDir = tmp.path() + "/unpacked";
    QDir().mkpath(unpackDir);
    const auto unpacked = NotebookImporter::importFolderPackage(pack, unpackDir, "versions-test");
    ok &= check(packed.success && unpacked.success
                    && QFileInfo::exists(unpacked.extractedSnbPath + "/n.snb/document.json")
                    && !QFileInfo::exists(unpacked.extractedSnbPath + "/n.snb/versions"),
                "a pack has the note without its versions");

    // The dialog, as a picture: %TEMP%/sn_versions_dialog.png
    {
        auto opened = Document::loadBundle(bundle);
        VersionsDialog dialog(opened.get(), 1);
        dialog.selectRow(1);
        dialog.resize(dialog.sizeHint());
        const QString png = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                          + "/sn_versions_dialog.png";
        ok &= check(dialog.grab().save(png), "dialog picture saved to " + png);
    }

    // A size example for the docs: one heavy page and its versions.
    {
        auto heavy = Document::createNew("Size");
        fillPage(heavy->page(0), "old", 400, 80);
        const QString sizeBundle = tmp.path() + "/size.snb";
        heavy->saveBundle(sizeBundle);
        const qint64 bytes = QFileInfo(sizeBundle + "/pages/" + heavy->pageUuidAt(0) + ".json").size();
        std::printf("INFO: a page of 400 strokes x 80 points = %lld KB; 10 versions = %lld KB\n",
                    static_cast<long long>(bytes / 1024), static_cast<long long>(bytes * 10 / 1024));
    }

    // Hours of editing later, every manifest from before the removal is pruned:
    // a copy at t0 still has the removed page (its last version says it lived then).
    for (const QString& name : QDir(bundle + "/versions/document").entryList(QDir::Files)) {
        QFile::remove(bundle + "/versions/document/" + name);
    }
    const QString copyC = tmp.path() + "/c.snb";
    ok &= check(NoteVersions::buildCopy(bundle, t0 + 59, copyC, "Copy C"), "a copy once the old manifests are gone");
    auto c = Document::loadBundle(copyC);
    ok &= check(c && c->pageCount() == 3 && c->page(2)->layer(0)->strokes().size() == 2,
                "the removed page is in it all the same");

    // Opening the note much later: the removed page goes, the others keep one.
    NoteVersions::pruneVersions(bundle, policy, t0 + 400 * 86400);
    ok &= check(versionCount(bundle + "/versions/pages/" + p1) == 1
                    && !QFileInfo::exists(bundle + "/versions/pages/" + p2),
                "after a year: one version per page, none for the removed one");
    return ok;
}

inline bool testEndlessNote()
{
    std::printf("--- an endless note ---\n");
    QTemporaryDir tmp;
    const QString bundle = tmp.path() + "/e.snb";
    const qint64 t0 = baseTime();
    auto doc = Document::createNew("Endless", Document::Mode::Edgeless);
    fillPage(doc->getOrCreateTile(0, 0), "old", 2);
    doc->markTileDirty({0, 0});
    if (!check(doc->saveBundle(bundle), "first save")) return false;
    setWritten(bundle + "/tiles/0,0.json", t0);
    setWritten(bundle + "/document.json", t0);

    fillPage(doc->getTile(0, 0), "new", 5);
    doc->markTileDirty({0, 0});
    bool ok = check(saveWith(doc.get(), NoteVersions::Policy()), "save of a changed tile");
    ok &= check(versionCount(bundle + "/versions/tiles/0,0") == 1, "the old tile was kept");

    const QString copy = tmp.path() + "/copy.snb";
    ok &= check(NoteVersions::buildCopy(bundle, t0 + 59, copy, "Copy"), "a copy at the first time");
    auto back = Document::loadBundle(copy);
    ok &= check(back && back->isEdgeless() && back->loadTileFromDisk({0, 0})
                    && back->getTile(0, 0)->layer(0)->strokes().size() == 2,
                "the copy is endless and has the old tile");
    return ok;
}

inline bool runAllTests()
{
    bool ok = true;
    ok &= testSpacingAndCount();
    ok &= testAge();
    ok &= testRemovedLongAgo();
    ok &= testFileAt();
    ok &= testNote();
    ok &= testEndlessNote();
    std::printf("\n%s\n", ok ? "All version tests passed." : "SOME VERSION TESTS FAILED.");
    return ok;
}

}  // namespace NoteVersionsTests
