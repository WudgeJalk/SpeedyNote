#pragma once

// ============================================================================
// SaveCrashTests - saves are interrupted many times; the note must always open
// ============================================================================
// Step R5 of docs/ROADMAP.md. Group: --test-savecrash (desktop debug builds).
//
// 1. Crash rounds: generation 0 is written into a temporary bundle. Then, 40
//    times, a child process (--save-crash-child <bundle> <seed> <firstGen>)
//    rewrites 3 of the 8 pages per generation (every 5th generation it also
//    adds one page and removes one) and saves in a loop; even generations use
//    saveBundle, odd ones prepareSave -> writeSaveJob on a thread -> finishSave.
//    It prints "G <g>" after each saved generation. The parent kills it after
//    a seeded 30-400 ms (QProcess::kill = TerminateProcess) and checks:
//      (a) the bundle loads; (b) every page in page_order loads;
//      (c) each page carries ONE generation tag, at most the last printed + 1;
//      (d) no page file named by the manifest is missing.
//    The next child continues from what is on disk.
// 2. Fault hook: every commit of a save is made to fail in turn
//    (Document::s_failCommitAt). The save must say so, keep the changes
//    unsaved and dirty, and leave each page old or new on disk. A clean save
//    then makes disk == memory.
// 3. An edgeless note: tiles, removed tiles, a failed save, a tile drawn
//    again where one was removed.
// Not covered: power loss (a killed process does not lose the OS cache).
// ============================================================================

#include "Document.h"
#include "Page.h"
#include "../layers/VectorLayer.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QProcess>
#include <QRandomGenerator>
#include <QSet>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent>
#include <QVector>
#include <algorithm>
#include <cstdio>

namespace SaveCrashTests {

constexpr int kPages = 8;
// Light pages: a debug build runs with AddressSanitizer, and a generation must
// be able to finish inside the 30-400 ms window for the later paths to run.
constexpr int kStrokesPerPage = 40;

inline bool fail(const QString& what)
{
    std::printf("FAIL: %s\n", qPrintable(what));
    std::fflush(stdout);
    return false;
}

// Replaces the strokes of a page by strokes whose ids start with "<tag>-".
inline void fillLayer(VectorLayer* layer, const QString& tag, QRandomGenerator& rng)
{
    layer->clear();
    for (int s = 0; s < kStrokesPerPage; ++s) {
        VectorStroke stroke;
        stroke.id = QStringLiteral("%1-%2").arg(tag).arg(s);
        stroke.color = Qt::black;
        stroke.baseThickness = 2.0;
        const qreal y = 20 + rng.bounded(700);
        for (int p = 0; p < 20; ++p) {
            stroke.points.append({QPointF(20 + p * 24, y + rng.bounded(8)), 0.5});
        }
        stroke.updateBoundingBox();
        layer->addStroke(std::move(stroke));
    }
}

// The one tag of a layer, or an empty string when it is empty or mixed.
inline QString layerTag(const VectorLayer* layer)
{
    QSet<QString> tags;
    for (const VectorStroke& s : layer->strokes()) tags.insert(s.id.section('-', 0, 0));
    if (tags.size() != 1 || layer->strokes().size() != kStrokesPerPage) return {};
    return *tags.begin();
}

// The save used by the child and the fault test: sync or split, by `split`.
inline bool saveOnce(Document* doc, const QString& bundle, bool split, QThreadPool* pool,
                     QString* error = nullptr)
{
    if (!split) return doc->saveBundle(bundle, false, error);
    const std::shared_ptr<Document::SaveJob> job = doc->prepareSave(error);
    if (!job) return false;
    const bool ok = QtConcurrent::run(pool, [job, error]() {
        return Document::writeSaveJob(*job, error);
    }).result();
    doc->finishSave(*job, ok);
    return ok;
}

// page uuid -> tag, read from a fresh load of the bundle (empty tag = bad page).
inline QMap<QString, QString> tagsOnDisk(const QString& bundle, QStringList* order = nullptr)
{
    QMap<QString, QString> tags;
    auto doc = Document::loadBundle(bundle);
    if (!doc) return tags;
    for (int i = 0; i < doc->pageCount(); ++i) {
        const Page* page = doc->page(i);
        if (!page) continue;
        tags[page->uuid] = layerTag(page->layer(0));
        if (order) order->append(page->uuid);
    }
    return tags;
}

inline QMap<QString, QString> tagsInMemory(Document* doc, QStringList* order = nullptr)
{
    QMap<QString, QString> tags;
    for (int i = 0; i < doc->pageCount(); ++i) {
        const Page* page = doc->page(i);
        tags[page->uuid] = layerTag(page->layer(0));
        if (order) order->append(page->uuid);
    }
    return tags;
}

// Child: saves generation after generation until it is killed.
inline int runChild(const QString& bundle, quint32 seed, int firstGen)
{
    auto doc = Document::loadBundle(bundle);
    if (!doc || doc->pageCount() != kPages) return 2;
    QRandomGenerator rng(seed);
    QThreadPool pool;
    pool.setMaxThreadCount(1);
    std::printf("R\n");
    std::fflush(stdout);

    for (int g = firstGen;; ++g) {
        const QString tag = QStringLiteral("g%1").arg(g);
        if (g % 5 == 0) {
            fillLayer(doc->addPage()->layer(0), tag, rng);
            doc->removePage(rng.bounded(doc->pageCount() - 1));  // never the new page
        }
        QSet<int> picked;
        while (picked.size() < 3) picked.insert(rng.bounded(doc->pageCount()));
        for (int i : picked) {
            Page* page = doc->page(i);
            if (!page) return 3;
            fillLayer(page->layer(0), tag, rng);
            doc->markPageDirty(i);
        }
        QElapsedTimer saveTime;
        saveTime.start();
        if (!saveOnce(doc.get(), bundle, g % 2 == 1, &pool)) return 4;
        std::printf("G %d %lld\n", g, static_cast<long long>(saveTime.elapsed()));
        std::fflush(stdout);
    }
}

// Reads "--save-crash-child <bundle> <seed> <firstGen>" (Unicode-safe).
inline int runChildFromArguments()
{
    const QStringList args = QCoreApplication::arguments();
    const int at = args.indexOf(QStringLiteral("--save-crash-child"));
    if (at < 0 || at + 3 >= args.size()) return 5;
    return runChild(args[at + 1], args[at + 2].toUInt(), args[at + 3].toInt());
}

// Empty when the bundle is sound, else what is wrong.
inline QString checkBundle(const QString& bundle, int maxGen)
{
    QFile manifest(bundle + "/document.json");
    if (!manifest.open(QIODevice::ReadOnly)) return "(a) no document.json";
    const QJsonArray order =
        QJsonDocument::fromJson(manifest.readAll()).object().value("page_order").toArray();
    for (const QJsonValue& v : order) {
        if (!QFile::exists(bundle + "/pages/" + v.toString() + ".json"))
            return "(d) missing page file " + v.toString();
    }

    auto doc = Document::loadBundle(bundle);
    if (!doc) return "(a) the bundle does not load";
    if (doc->pageCount() != kPages) return QString("page count %1").arg(doc->pageCount());
    for (int i = 0; i < doc->pageCount(); ++i) {
        const Page* page = doc->page(i);
        if (!page) return QString("(b) page %1 does not load").arg(i);
        const QString tag = layerTag(page->layer(0));
        if (tag.isEmpty()) return QString("(c) page %1 is not one whole generation").arg(i);
        if (tag.mid(1).toInt() > maxGen)
            return QString("(c) page %1 has %2, newer than g%3").arg(i).arg(tag).arg(maxGen);
    }
    return {};
}

inline bool testCrashRounds()
{
    std::printf("--- crash rounds ---\n");
    QTemporaryDir tmp;
    if (!tmp.isValid()) return fail("no temporary folder");
    const QString bundle = tmp.path() + "/crash.snb";
    {
        QRandomGenerator rng(1);
        auto doc = Document::createNew("Crash test");
        while (doc->pageCount() < kPages) doc->addPage();
        for (int i = 0; i < kPages; ++i) fillLayer(doc->page(i)->layer(0), "g0", rng);
        if (!doc->saveBundle(bundle)) return fail("generation 0 was not saved");
    }

    int lastPrinted = 0;
    int killsInsideSaves = 0;
    QVector<int> saveMs;
    QRandomGenerator rng(20261122);
    for (int round = 0; round < 40; ++round) {
        QProcess child;
        child.setStandardErrorFile(QProcess::nullDevice());
        child.start(QCoreApplication::applicationFilePath(),
                    {"--save-crash-child", bundle, QString::number(1000 + round),
                     QString::number(lastPrinted + 1)});
        if (!child.waitForStarted(10000)) return fail("the child did not start");

        // Wait until the child has loaded the bundle ("R"; Windows ends the line
        // with \r\n), then let it save a while.
        QByteArray out;
        QElapsedTimer waited;
        waited.start();
        while (!out.contains('R') && waited.elapsed() < 30000
               && child.state() == QProcess::Running) {
            if (child.waitForReadyRead(200)) out += child.readAllStandardOutput();
        }
        if (!out.contains('R')) {
            child.kill();
            child.waitForFinished(5000);
            return fail(QString("round %1: the child could not load the bundle (exit %2)")
                            .arg(round).arg(child.exitCode()));
        }
        if (child.waitForFinished(30 + rng.bounded(371))) {
            return fail(QString("round %1: the child stopped by itself, exit %2 (a save failed)")
                            .arg(round).arg(child.exitCode()));
        }
        child.kill();
        child.waitForFinished(5000);
        out += child.readAllStandardOutput();

        int printedThisRound = 0;
        for (const QByteArray& line : out.split('\n')) {
            const QList<QByteArray> parts = line.trimmed().split(' ');
            if (parts.size() == 3 && parts[0] == "G") {
                lastPrinted = qMax(lastPrinted, parts[1].toInt());
                saveMs.append(parts[2].toInt());
                ++printedThisRound;
            }
        }
        if (printedThisRound > 0) ++killsInsideSaves;

        const QString problem = checkBundle(bundle, lastPrinted + 1);
        if (!problem.isEmpty()) return fail(QString("round %1: %2").arg(round).arg(problem));
    }
    std::sort(saveMs.begin(), saveMs.end());
    std::printf("PASS: 40 kills, the note always opened whole (generations saved: %d, "
                "rounds that saved at least once before the kill: %d, median save: %d ms)\n",
                lastPrinted, killsInsideSaves, saveMs.isEmpty() ? -1 : saveMs[saveMs.size() / 2]);
    if (lastPrinted < 10) {
        return fail("fewer than 10 generations were saved: the kills did not reach the later "
                    "paths (split save, added and removed pages)");
    }
    return true;
}

inline bool testFaultHook()
{
    std::printf("--- fault hook ---\n");
    QTemporaryDir tmp;
    if (!tmp.isValid()) return fail("no temporary folder");
    const QString bundle = tmp.path() + "/fault.snb";
    QRandomGenerator rng(7);
    QThreadPool pool;
    pool.setMaxThreadCount(1);

    auto doc = Document::createNew("Fault test");
    while (doc->pageCount() < kPages) doc->addPage();
    for (int i = 0; i < kPages; ++i) fillLayer(doc->page(i)->layer(0), "s", rng);
    if (!doc->saveBundle(bundle)) return fail("first save");

    for (bool split : {false, true}) {
        bool succeeded = false;
        for (int n = 0; n < 60 && !succeeded; ++n) {
            QStringList orderBefore;
            const QMap<QString, QString> before = tagsOnDisk(bundle, &orderBefore);

            const QString tag = QStringLiteral("%1%2").arg(split ? "a" : "b").arg(n);
            fillLayer(doc->addPage()->layer(0), tag, rng);
            doc->removePage(rng.bounded(doc->pageCount() - 1));
            QSet<int> picked;
            while (picked.size() < 3) picked.insert(rng.bounded(doc->pageCount()));
            for (int i : picked) {
                fillLayer(doc->page(i)->layer(0), tag, rng);
                doc->markPageDirty(i);
            }
            const QMap<QString, QString> memory = tagsInMemory(doc.get());

            Document::s_failCommitAt = n;
            QString error;
            const bool ok = saveOnce(doc.get(), bundle, split, &pool, &error);
            Document::s_failCommitAt = -1;
            const QString where = QString("%1 save, commit %2: ").arg(split ? "split" : "plain").arg(n);

            if (ok) {
                if (n == 0) return fail(where + "the fault hook did nothing");
                succeeded = true;
                break;
            }
            if (error.isEmpty()) return fail(where + "no reason given");
            if (!doc->modified) return fail(where + "the note no longer has unsaved changes");
            for (int i : picked) {
                if (!doc->isPageDirty(i)) return fail(where + QString("page %1 is no longer dirty").arg(i));
            }
            QStringList orderAfter;
            const QMap<QString, QString> after = tagsOnDisk(bundle, &orderAfter);
            if (orderAfter != orderBefore) return fail(where + "the page list on disk changed");
            for (auto it = after.begin(); it != after.end(); ++it) {
                if (it.value().isEmpty()
                    || (it.value() != before.value(it.key()) && it.value() != memory.value(it.key()))) {
                    return fail(where + "page " + it.key() + " is neither old nor new: " + it.value());
                }
            }
        }
        if (!succeeded) return fail("a save never got past the fault hook");

        QStringList diskOrder, memoryOrder;
        if (tagsOnDisk(bundle, &diskOrder) != tagsInMemory(doc.get(), &memoryOrder)
            || diskOrder != memoryOrder) {
            return fail(QString("%1 save: after a clean save, disk differs from memory")
                            .arg(split ? "split" : "plain"));
        }
        if (doc->modified) return fail("a clean save left unsaved changes");
    }
    std::printf("PASS: every failed commit kept the note unsaved and each page old or new\n");
    return true;
}

inline bool testEdgeless()
{
    std::printf("--- edgeless ---\n");
    QTemporaryDir tmp;
    if (!tmp.isValid()) return fail("no temporary folder");
    const QString bundle = tmp.path() + "/edge.snb";
    QRandomGenerator rng(3);

    auto doc = Document::createNew("Edge test", Document::Mode::Edgeless);
    fillLayer(doc->getOrCreateTile(0, 0)->layer(0), "t1", rng);
    fillLayer(doc->getOrCreateTile(1, 0)->layer(0), "t1", rng);
    if (!doc->saveBundle(bundle)) return fail("edgeless first save");

    auto tagAt = [](const QString& path, int tx, int ty) {
        auto loaded = Document::loadBundle(path);
        const Page* tile = loaded ? loaded->getTile(tx, ty) : nullptr;
        return tile ? layerTag(tile->layer(0)) : QString();
    };
    if (tagAt(bundle, 0, 0) != "t1" || tagAt(bundle, 1, 0) != "t1")
        return fail("edgeless tiles did not round-trip");

    // A failed save keeps the old tiles on disk and the change unsaved.
    fillLayer(doc->getTile(0, 0)->layer(0), "t2", rng);
    doc->markTileDirty({0, 0});
    doc->getTile(1, 0)->layer(0)->clear();
    doc->removeTileIfEmpty(1, 0);
    Document::s_failCommitAt = 0;
    const bool failed = !doc->saveBundle(bundle);
    Document::s_failCommitAt = -1;
    if (!failed || !doc->isTileDirty({0, 0}) || !doc->modified)
        return fail("edgeless failed save did not keep the change unsaved");
    if (tagAt(bundle, 0, 0) != "t1" || tagAt(bundle, 1, 0) != "t1")
        return fail("edgeless failed save changed the disk");

    // Clean save: the change lands, the removed tile leaves the disk.
    if (!doc->saveBundle(bundle) || tagAt(bundle, 0, 0) != "t2"
        || QFile::exists(bundle + "/tiles/1,0.json")) {
        return fail("edgeless clean save");
    }

    // A tile removed and drawn again before the save must be kept.
    doc->getTile(0, 0)->layer(0)->clear();
    doc->removeTileIfEmpty(0, 0);
    fillLayer(doc->getOrCreateTile(0, 0)->layer(0), "t3", rng);
    if (!doc->saveBundle(bundle) || tagAt(bundle, 0, 0) != "t3")
        return fail("a tile drawn again where one was removed was lost");

    std::printf("PASS: edgeless tiles saved, removed and kept as expected\n");
    return true;
}

inline QByteArray readAll(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

inline bool writeAll(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

// A tile that fails to load stays in the note, and is never written over.
inline bool testUnreadableTile()
{
    std::printf("--- an unreadable tile ---\n");
    QTemporaryDir tmp;
    const QString bundle = tmp.path() + "/edge.snb";
    QRandomGenerator rng(5);
    {
        auto doc = Document::createNew("Unreadable", Document::Mode::Edgeless);
        fillLayer(doc->getOrCreateTile(0, 0)->layer(0), "t1", rng);
        fillLayer(doc->getOrCreateTile(1, 0)->layer(0), "t1", rng);
        if (!doc->saveBundle(bundle)) return fail("first save");
    }
    const QString broken = bundle + "/tiles/1,0.json";
    if (!writeAll(broken, "{\"cut short")) return fail("could not damage the tile");

    auto doc = Document::loadBundle(bundle);
    if (!doc || doc->getTile(1, 0)) return fail("the damaged tile loaded");
    fillLayer(doc->getTile(0, 0)->layer(0), "t2", rng);
    doc->markTileDirty({0, 0});
    if (!doc->saveBundle(bundle)) return fail("save after the read failure");
    const QJsonArray index = QJsonDocument::fromJson(readAll(bundle + "/document.json")).object()
                                 .value("tile_index").toArray();
    if (!index.contains(QStringLiteral("1,0")) || readAll(broken) != "{\"cut short")
        return fail("a read failure removed the tile from the note");

    // Drawing there: the old file is set aside, not written over.
    fillLayer(doc->getOrCreateTile(1, 0)->layer(0), "t3", rng);
    doc->markTileDirty({1, 0});
    if (!doc->saveBundle(bundle)) return fail("save of the new tile");
    const QStringList aside = QDir(bundle + "/tiles").entryList({QStringLiteral("1,0.json.unreadable-*")}, QDir::Files);
    if (aside.size() != 1 || readAll(bundle + "/tiles/" + aside.first()) != "{\"cut short")
        return fail("the unreadable tile was not kept aside");
    std::printf("PASS: an unreadable tile stays named, and is set aside before a new one\n");
    return true;
}

// Save As carries the markdown notes; a broken document.json opens from its version.
inline bool testNotesAndBrokenManifest()
{
    std::printf("--- markdown notes on Save As, a broken manifest ---\n");
    QTemporaryDir tmp;
    const QString a = tmp.path() + "/a.snb";
    const QString b = tmp.path() + "/b.snb";
    QRandomGenerator rng(6);
    auto doc = Document::createNew("Notes");
    fillLayer(doc->page(0)->layer(0), "n1", rng);
    if (!doc->saveBundle(a)) return fail("first save");
    QDir().mkpath(a + "/assets/notes");
    if (!writeAll(a + "/assets/notes/n1.md", "---\ntitle: \"x\"\n---\n\nhello")) return fail("no markdown note");
    if (!doc->saveBundle(b) || !readAll(b + "/assets/notes/n1.md").endsWith("hello"))
        return fail("Save As left the markdown notes behind");

    const QByteArray manifest = readAll(b + "/document.json");
    QDir().mkpath(b + "/versions/document");
    if (!writeAll(b + "/versions/document/20260101-000000.json", manifest)
        || !writeAll(b + "/document.json", manifest.left(manifest.size() / 2))) {
        return fail("could not damage the manifest");
    }
    auto back = Document::loadBundle(b);
    if (!back || back->pageCount() != 1 || layerTag(back->page(0)->layer(0)) != "n1")
        return fail("a broken document.json did not open from its earlier version");
    std::printf("PASS: markdown notes follow Save As; a broken manifest opens from its version\n");
    return true;
}

inline bool runAllTests()
{
    QElapsedTimer clock;
    clock.start();
    auto timed = [&clock](bool result) {
        std::printf("    (%lld ms)\n", static_cast<long long>(clock.restart()));
        return result;
    };
    bool ok = timed(testEdgeless());
    ok = timed(testUnreadableTile()) && ok;
    ok = timed(testNotesAndBrokenManifest()) && ok;
    ok = timed(testFaultHook()) && ok;
    ok = timed(testCrashRounds()) && ok;
    std::printf(ok ? "\nALL SAVE CRASH TESTS PASSED\n" : "\nSAVE CRASH TESTS FAILED\n");
    std::fflush(stdout);
    return ok;
}

}  // namespace SaveCrashTests
