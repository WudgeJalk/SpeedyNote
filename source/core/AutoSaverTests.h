#pragma once

// ============================================================================
// AutoSaverTests - autosave and recovery (Steps R1 and R3 of docs/ROADMAP.md)
// ============================================================================
// Group: --test-autosave (desktop debug builds). Every note lives in a
// QTemporaryDir (DocumentManager::setUnsavedDirForTests); no user setting is
// read or written by the AutoSaver here (configure() instead of QSettings).
//  1. When: no save while a pointer is down; a save after the pause; at least
//     every maxMinutes while the user keeps writing (fake clock, tick()).
//  2. An edit made while a save is written stays unsaved and is saved next.
//  3. A failed save (fault hook) shows Failed, keeps the pages dirty, and is
//     tried again 30 s later.
//  4. A kill: a child process autosaves a new note, then is killed. The note
//     is offered back (leftoverTempBundles) with what was autosaved; a note
//     never changed is not offered.
//  5. Numbers for rule 1: GUI-thread time of a save (prepare) and the longest
//     event-loop gap while the worker writes.
// ============================================================================

#include "AutoSaver.h"
#include "Document.h"
#include "DocumentManager.h"
#include "DocumentTests.h"
#include "Page.h"
#include "../layers/VectorLayer.h"
#include "../ui/NavigationBar.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDir>
#include <QScopeGuard>
#include <QSettings>
#include <QLayout>
#include <QImage>
#include <QPainter>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>

namespace AutoSaverTests {

inline bool fail(const QString& what)
{
    std::printf("FAIL: %s\n", qPrintable(what));
    std::fflush(stdout);
    return false;
}

// Adds `count` strokes "<tag>-<n>" to page `index`, the way a pen stroke ends.
inline void draw(Document* doc, int index, const QString& tag, int count, int points = 20)
{
    VectorLayer* layer = doc->page(index)->layer(0);
    const int first = layer->strokes().size();
    for (int s = 0; s < count; ++s) {
        VectorStroke stroke;
        stroke.id = QStringLiteral("%1-%2").arg(tag).arg(first + s);
        stroke.color = Qt::black;
        stroke.baseThickness = 2.0;
        for (int p = 0; p < points; ++p) {
            stroke.points.append({QPointF(20 + p * 6, 20 + (first + s) % 900), 0.5});
        }
        stroke.updateBoundingBox();
        layer->addStroke(std::move(stroke));
    }
    doc->markPageDirty(index);
}

// Strokes of page `index` whose id starts with "<tag>-", read from the disk.
inline int strokesOnDisk(const QString& bundle, int index, const QString& tag)
{
    auto doc = Document::loadBundle(bundle);
    const Page* page = doc ? doc->page(index) : nullptr;
    if (!page) return -1;
    int n = 0;
    for (const VectorStroke& s : page->layer(0)->strokes()) {
        if (s.id.startsWith(tag + "-")) ++n;
    }
    return n;
}

inline bool waitForSave(AutoSaver& saver, int timeoutMs = 20000)
{
    QElapsedTimer waited;
    waited.start();
    while (saver.isSaving() && waited.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return !saver.isSaving();
}

// A DocumentManager whose new notes live in a temporary folder, and an
// AutoSaver driven by hand (its timer stopped).
struct Fixture {
    QTemporaryDir tmp;
    bool busy = false;
    std::unique_ptr<DocumentManager> manager;
    std::unique_ptr<AutoSaver> saver;
    AutoSaver::State lastState = AutoSaver::Off;
    QString lastDetail;

    Fixture(int idleSeconds = 5, int maxMinutes = 2)
    {
        DocumentManager::setUnsavedDirForTests(tmp.path() + "/unsaved");
        manager = std::make_unique<DocumentManager>();
        saver = std::make_unique<AutoSaver>(manager.get(), [this]() { return busy; });
        saver->configure(true, idleSeconds, maxMinutes);
        saver->findChild<QTimer*>()->stop();
        QObject::connect(saver.get(), &AutoSaver::stateChanged,
                         [this](Document*, AutoSaver::State state, const QString& detail) {
            lastState = state;
            lastDetail = detail;
        });
    }
    ~Fixture()
    {
        saver.reset();
        manager.reset();
        DocumentManager::setUnsavedDirForTests(QString());
    }
};

inline bool testWhen()
{
    std::printf("--- when a note is saved ---\n");
    Fixture f;
    Document* doc = f.manager->createDocument("When");
    if (!f.manager->isUsingTempBundle(doc) || doc->bundlePath().isEmpty())
        return fail("a new note has no temporary bundle");
    if (DocumentManager::tempNoteHasWork(doc))
        return fail("a note never changed counts as work");

    // A pointer is down: nothing is saved, however long it lasts.
    draw(doc, 0, "a", 3);
    f.busy = true;
    f.saver->tick(1000);
    f.saver->tick(30000);
    if (f.saver->isSaving() || !doc->modified) return fail("saved while a pointer was down");

    // Pointer up: saved at once (the pause is long over), into the temp bundle.
    f.busy = false;
    f.saver->tick(31000);
    if (!f.saver->isSaving()) return fail("no save after the pointer went up");
    if (f.lastState != AutoSaver::Saving) return fail("no Saving state");
    if (!waitForSave(*f.saver)) return fail("the save never finished");
    if (doc->modified || f.manager->hasUnsavedChanges(doc)) return fail("still unsaved after a save");
    if (f.lastState != AutoSaver::Saved) return fail("no Saved state");
    if (strokesOnDisk(doc->bundlePath(), 0, "a") != 3) return fail("the strokes are not on disk");
    if (!DocumentManager::tempNoteHasWork(doc)) return fail("a changed note does not count as work");

    // The pause: 4 s is too early, 5 s saves.
    draw(doc, 0, "b", 2);
    f.saver->tick(40000);
    f.saver->tick(44000);
    if (f.saver->isSaving()) return fail("saved before idleSeconds");
    f.saver->tick(45000);
    if (!f.saver->isSaving()) return fail("not saved after idleSeconds");
    if (!waitForSave(*f.saver)) return fail("the second save never finished");
    if (strokesOnDisk(doc->bundlePath(), 0, "b") != 2) return fail("the second strokes are not on disk");

    // Writing without a 5 s pause: saved once maxMinutes (here 1) is reached,
    // at the first quiet second.
    f.saver->configure(true, 5, 1);
    qint64 savedAt = -1;
    const qint64 t0 = 100000;
    for (qint64 t = t0; t <= t0 + 70000 && savedAt < 0; t += 1000) {
        if ((t / 1000) % 2 == 0) draw(doc, 0, "c", 1);  // a stroke every 2 s
        f.saver->tick(t);
        if (f.saver->isSaving()) savedAt = t - t0;
    }
    if (savedAt < 60000 || savedAt > 62000)
        return fail(QString("maxMinutes: saved after %1 ms, expected 60-62 s").arg(savedAt));
    if (!waitForSave(*f.saver)) return fail("the maxMinutes save never finished");
    std::printf("PASS: no save with a pointer down; after the pause; after maxMinutes (%lld ms)\n",
                static_cast<long long>(savedAt));
    return true;
}

inline bool testEditDuringSave()
{
    std::printf("--- an edit while a save is written ---\n");
    Fixture f;
    Document* doc = f.manager->createDocument("During");
    draw(doc, 0, "a", 3);
    f.saver->tick(1000);
    f.saver->tick(6000);
    if (!f.saver->isSaving()) return fail("no save started");
    // The job is out; this stroke lands before finishSave runs (same thread).
    draw(doc, 0, "b", 1);
    if (!waitForSave(*f.saver)) return fail("the save never finished");
    if (!doc->modified || !doc->isPageDirty(0) || !f.manager->hasUnsavedChanges(doc))
        return fail("the edit made during the save was marked saved");
    if (strokesOnDisk(doc->bundlePath(), 0, "a") != 3) return fail("the first strokes are not on disk");
    f.saver->tick(7000);
    f.saver->tick(12000);
    if (!f.saver->isSaving()) return fail("the edit made during the save was never saved");
    if (!waitForSave(*f.saver)) return fail("the second save never finished");
    if (doc->modified || strokesOnDisk(doc->bundlePath(), 0, "b") != 1)
        return fail("the edit made during the save is not on disk");

    // While the job holds page 0, eviction leaves it loaded (a failed save
    // must find it); a writer of the same file waits for the job.
    draw(doc, 0, "c", 1);
    f.saver->tick(13000);
    f.saver->tick(18000);
    if (!f.saver->isSaving()) return fail("no third save");
    doc->evictPage(0);
    if (!doc->isPageLoaded(0)) return fail("a page held by the running save was evicted");
    if (!doc->savePage(0)) return fail("savePage failed while a job ran");
    if (!waitForSave(*f.saver)) return fail("the third save never finished");
    if (strokesOnDisk(doc->bundlePath(), 0, "c") != 1) return fail("third strokes missing");
    doc->evictPage(0);
    if (doc->isPageLoaded(0)) return fail("the page was not evicted once the save was done");

    // Autosave on: a dirty page stays loaded for the worker (no write on the GUI thread).
    draw(doc, 0, "d", 1);
    doc->evictPage(0);
    if (!doc->isPageLoaded(0) || strokesOnDisk(doc->bundlePath(), 0, "d") != 0)
        return fail("a dirty page was written by eviction while autosave is on");
    f.saver->tick(19000);
    f.saver->tick(25000);
    if (!waitForSave(*f.saver) || strokesOnDisk(doc->bundlePath(), 0, "d") != 1)
        return fail("the dirty page kept loaded was not autosaved");
    std::printf("PASS: an edit during a save stays unsaved, then is saved\n");
    return true;
}

// A note whose folder was moved away is never rebuilt at the old place.
inline bool testMovedFolder()
{
    std::printf("--- the note's folder moved away ---\n");
    Fixture f;
    Document* doc = f.manager->createDocument("Moved");
    draw(doc, 0, "a", 1);
    const QString path = f.tmp.path() + "/Moved.snb";
    if (!doc->saveBundle(path)) return fail("could not save the note");  // not saveDocumentAs: no start-screen entry
    if (!QDir().rename(path, path + ".away")) return fail("could not move the folder");
    draw(doc, 0, "b", 1);
    QString why;
    if (f.saver->flush(doc, &why) || why.isEmpty()) return fail("flush wrote into a folder that is gone");
    if (QFileInfo::exists(path)) return fail("a half note was rebuilt at the old place");
    if (!f.manager->hasUnsavedChanges(doc)) return fail("the changes were marked saved");
    std::printf("PASS: no half note at the old place (\"%s\")\n", qPrintable(why));
    return true;
}

// A PDF opened directly gets a temp bundle: autosave and recovery cover it.
inline bool testPdf()
{
    std::printf("--- a PDF opened directly ---\n");
    // loadDocument() adds the PDF to the user's recent list: put it back after.
    const QVariant recent = QSettings("SpeedyNote", "App").value("RecentDocuments");
    const auto restoreRecent = qScopeGuard([recent]() {
        QSettings settings("SpeedyNote", "App");
        if (recent.isValid()) settings.setValue("RecentDocuments", recent);
        else settings.remove("RecentDocuments");
    });
    Fixture f;
    const QString pdf = f.tmp.path() + "/slides.pdf";
    if (!DocumentTests::writeFixtureFile(pdf, DocumentTests::makeMinimalPdf(3))) return fail("no PDF written");
    Document* doc = f.manager->loadDocument(pdf);
    if (!doc) return fail("the PDF did not open");
    if (doc->pageCount() != 3) {
        std::printf("SKIP: the PDF backend cannot read the synthetic PDF\n");
        return true;
    }
    if (!f.manager->isUsingTempBundle(doc) || doc->modified || DocumentManager::tempNoteHasWork(doc))
        return fail("a PDF just opened has no temp bundle, or counts as changed");
    draw(doc, 1, "pdf", 2);
    f.saver->tick(1000);
    f.saver->tick(6000);
    if (!waitForSave(*f.saver) || doc->modified) return fail("the annotations were not autosaved");
    if (!DocumentManager::tempNoteHasWork(doc) || strokesOnDisk(doc->bundlePath(), 1, "pdf") != 2)
        return fail("the annotations are not in the temp bundle");
    std::printf("PASS: annotations on a PDF are autosaved into its temp bundle\n");
    return true;
}

inline bool testFailure()
{
    std::printf("--- a failed save ---\n");
#ifdef SPEEDYNOTE_DEBUG
    Fixture f;
    Document* doc = f.manager->createDocument("Fault");
    draw(doc, 0, "a", 2);
    f.saver->tick(1000);
    if (!f.saver->flush(doc)) return fail("flush failed");
    draw(doc, 0, "b", 2);
    Document::s_failCommitAt = 0;
    f.saver->tick(2000);
    f.saver->tick(7000);
    if (!waitForSave(*f.saver)) return fail("the failing save never finished");
    Document::s_failCommitAt = -1;
    if (f.lastState != AutoSaver::Failed || f.lastDetail.isEmpty())
        return fail("no Failed state with a reason");
    if (!doc->modified || !doc->isPageDirty(0)) return fail("a failed save dropped the changes");
    if (strokesOnDisk(doc->bundlePath(), 0, "b") != 0 || strokesOnDisk(doc->bundlePath(), 0, "a") != 2)
        return fail("a failed save changed the disk");
    // Autosave is the retry, 30 s later, not before.
    f.saver->tick(20000);
    if (f.saver->isSaving()) return fail("retried before 30 s");
    f.saver->tick(37000);
    if (!f.saver->isSaving()) return fail("no retry after 30 s");
    if (!waitForSave(*f.saver)) return fail("the retry never finished");
    if (f.lastState != AutoSaver::Saved || doc->modified
        || strokesOnDisk(doc->bundlePath(), 0, "b") != 2) {
        return fail("the retry did not save");
    }
    std::printf("PASS: a failed save shows Failed, keeps the changes, retries after 30 s\n");
#else
    std::printf("SKIP: the fault hook exists in debug builds only\n");
#endif
    return true;
}

// The child of testKill: autosaves a new note, prints "S", waits to be killed.
inline int runChild(const QString& unsavedDir)
{
    DocumentManager::setUnsavedDirForTests(unsavedDir);
    DocumentManager manager;
    AutoSaver saver(&manager, []() { return false; });
    saver.configure(true, 1, 1);
    saver.findChild<QTimer*>()->stop();
    manager.createDocument("Never changed");
    Document* doc = manager.createDocument("Kept note");
    draw(doc, 0, "kept", 30);
    saver.tick(1000);
    saver.tick(2000);
    if (!waitForSave(saver) || doc->modified) return 2;
    draw(doc, 0, "late", 2);  // after the last autosave: at most idleSeconds of work
    std::printf("S\n");
    std::fflush(stdout);
    for (;;) QThread::msleep(50);
}

inline int runChildFromArguments()
{
    const QStringList args = QCoreApplication::arguments();
    const int at = args.indexOf(QStringLiteral("--autosave-crash-child"));
    if (at < 0 || at + 1 >= args.size()) return 5;
    return runChild(args[at + 1]);
}

inline bool testKill()
{
    std::printf("--- killed before any manual save ---\n");
    QTemporaryDir tmp;
    if (!tmp.isValid()) return fail("no temporary folder");
    const QString dir = tmp.path() + "/unsaved";

    QProcess child;
    child.setStandardErrorFile(QProcess::nullDevice());
    child.start(QCoreApplication::applicationFilePath(), {"--autosave-crash-child", dir});
    if (!child.waitForStarted(10000)) return fail("the child did not start");
    QByteArray out;
    QElapsedTimer waited;
    waited.start();
    while (!out.contains('S') && waited.elapsed() < 60000 && child.state() == QProcess::Running) {
        if (child.waitForReadyRead(200)) out += child.readAllStandardOutput();
    }
    if (!out.contains('S')) {
        child.kill();
        child.waitForFinished(5000);
        return fail(QString("the child did not autosave (exit %1)").arg(child.exitCode()));
    }
    child.kill();  // TerminateProcess: no closeEvent, no destructor
    child.waitForFinished(5000);

    DocumentManager::setUnsavedDirForTests(dir);
    QVector<DocumentManager::LeftoverNote> ours;
    for (const auto& note : DocumentManager::leftoverTempBundles()) {
        if (note.path.startsWith(dir)) ours.append(note);  // the system temp folder is scanned too
    }
    bool ok = true;
    if (ours.size() != 1 || ours[0].name != "Kept note") {
        ok = fail(QString("expected the one changed note to be offered, got %1").arg(ours.size()));
    } else {
        DocumentManager manager;
        Document* back = manager.adoptTempBundle(ours[0].path);
        if (!back || !manager.isUsingTempBundle(back) || !DocumentManager::tempNoteHasWork(back)) {
            ok = fail("the note does not open back as a note without a place");
        } else {
            int kept = 0, late = 0;
            for (const VectorStroke& s : back->page(0)->layer(0)->strokes()) {
                kept += s.id.startsWith("kept-");
                late += s.id.startsWith("late-");
            }
            if (kept != 30 || late != 0) {
                ok = fail(QString("recovered %1 of 30 autosaved strokes (late: %2)").arg(kept).arg(late));
            }
            manager.closeDocument(back);  // Delete: its temp bundle goes
            if (QFileInfo::exists(ours[0].path)) ok = fail("closing did not remove the temp bundle");
        }
    }
    DocumentManager::setUnsavedDirForTests(QString());
    if (ok) std::printf("PASS: after a kill the changed note is offered with its autosaved strokes\n");
    return ok;
}

inline bool testNumbers()
{
    std::printf("--- rule 1: time on the GUI thread ---\n");
    Fixture f;
    Document* doc = f.manager->createDocument("Heavy");
    while (doc->pageCount() < 10) doc->addPage();
    // A heavy page: 400 strokes of 80 points (32 000 points).
    draw(doc, 0, "heavy", 400, 80);
    if (!f.saver->flush(doc)) return fail("first save");

    auto measure = [&](const QString& what, auto edit) -> bool {
        edit();
        f.saver->tick(1000000);
        QElapsedTimer gui;
        gui.start();
        f.saver->tick(1010000);  // prepare runs inside this call
        const qint64 prepareMs = gui.elapsed();
        if (!f.saver->isSaving()) return fail(what + ": no save");
        // The event loop keeps turning while the worker writes.
        qint64 longestGap = 0, last = gui.elapsed();
        QElapsedTimer write;
        write.start();
        while (f.saver->isSaving() && write.elapsed() < 30000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            const qint64 now = gui.elapsed();
            longestGap = qMax(longestGap, now - last);
            last = now;
        }
        std::printf("  %s: GUI thread %lld ms, worker %lld ms, longest event-loop gap %lld ms\n",
                    qPrintable(what), static_cast<long long>(prepareMs),
                    static_cast<long long>(write.elapsed()), static_cast<long long>(longestGap));
        return !f.saver->isSaving();
    };
    bool ok = measure("1 light page dirty", [&]() { draw(doc, 1, "x", 5); });
    ok = measure("the heavy page dirty", [&]() { draw(doc, 0, "y", 1); }) && ok;
    ok = measure("10 pages dirty (one heavy)", [&]() {
        for (int i = 0; i < doc->pageCount(); ++i) draw(doc, i, "z", 20);
    }) && ok;
    if (ok) std::printf("PASS: numbers above (debug + AddressSanitizer build: slower than release)\n");
    return ok;
}

inline bool testEdgeless()
{
    std::printf("--- an endless note ---\n");
    Fixture f;
    Document* doc = f.manager->createEdgelessDocument("Endless");
    if (!f.manager->isUsingTempBundle(doc) || DocumentManager::tempNoteHasWork(doc))
        return fail("a new endless note has no temp bundle, or counts as work");
    VectorStroke stroke;
    stroke.id = QStringLiteral("e-0");
    stroke.color = Qt::black;
    stroke.baseThickness = 2.0;
    for (int p = 0; p < 20; ++p) stroke.points.append({QPointF(10 + p * 5, 10), 0.5});
    stroke.updateBoundingBox();
    doc->getOrCreateTile(0, 0)->layer(0)->addStroke(stroke);
    doc->markTileDirty({0, 0});
    doc->markModified();  // what the viewport's documentModified does
    f.saver->tick(1000);
    f.saver->tick(6000);
    if (!waitForSave(*f.saver) || doc->modified) return fail("the endless note was not autosaved");
    // While a save holds a tile, eviction keeps it (a failed save must find it).
    doc->getTile(0, 0)->layer(0)->addStroke(stroke);
    doc->markTileDirty({0, 0});
    doc->markModified();
    f.saver->tick(7000);
    f.saver->tick(12000);
    doc->evictTile({0, 0});
    if (doc->tileCount() != 1) return fail("a tile held by the running save was evicted");
    // An OCR result for that tile does not wait for the save (rule 1): the tile
    // is marked again, for the next save to write from memory.
    if (!doc->saveTileOcr({0, 0})) return fail("saveTileOcr failed while a save held the tile");
    if (!waitForSave(*f.saver)) return fail("the second endless save never finished");
    if (!doc->modified) return fail("the OCR result met during a save is not left for the next save");
    auto loaded = Document::loadBundle(doc->bundlePath());
    const Page* tile = loaded ? loaded->getTile(0, 0) : nullptr;
    if (!tile || tile->layer(0)->strokes().size() != 2) return fail("the tile is not on disk");
    if (!DocumentManager::tempNoteHasWork(doc)) return fail("an endless note with a tile is not work");
    std::printf("PASS: an endless note is autosaved into its temp bundle\n");
    return true;
}

// The sign in the NavigationBar, one row per state, saved as a picture to look at.
inline bool testSign()
{
    std::printf("--- the sign ---\n");
    NavigationBar bar;
    bar.updateTheme(false, QColor(0x31, 0x68, 0x82));
    bar.setFilename(QStringLiteral("Math-TIN"));
    bar.resize(760, bar.height());
    QImage sheet(760, bar.height() * 3, QImage::Format_ARGB32);
    sheet.fill(Qt::white);
    QPainter painter(&sheet);
    const AutoSaver::State states[] = {AutoSaver::Saved, AutoSaver::Saving, AutoSaver::Failed};
    for (int i = 0; i < 3; ++i) {
        bar.setSaveState(states[i], QStringLiteral("tip"));
        bar.layout()->activate();
        painter.drawPixmap(0, i * bar.height(), bar.grab());
    }
    painter.end();
    const QString file = QDir::tempPath() + "/sn_save_sign.png";
    sheet.save(file);
    QAbstractButton* sign = nullptr;
    for (QAbstractButton* button : bar.findChildren<QAbstractButton*>()) {
        if (button->toolTip() == QStringLiteral("tip")) sign = button;
    }
    if (!sign || sign->isHidden()) return fail("the sign is not shown with its tooltip");
    bar.setSaveState(AutoSaver::Off, QString());
    if (!sign->isHidden()) return fail("Off does not hide the sign");
    std::printf("PASS: the sign is hidden when Off; picture of Saved, Saving, Failed: %s\n",
                qPrintable(file));
    return true;
}

inline bool runAllTests()
{
    bool ok = testSign();
    ok = testWhen() && ok;
    ok = testEditDuringSave() && ok;
    ok = testFailure() && ok;
    ok = testEdgeless() && ok;
    ok = testMovedFolder() && ok;
    ok = testPdf() && ok;
    ok = testKill() && ok;
    ok = testNumbers() && ok;
    std::printf(ok ? "\nALL AUTOSAVE TESTS PASSED\n" : "\nAUTOSAVE TESTS FAILED\n");
    std::fflush(stdout);
    return ok;
}

}  // namespace AutoSaverTests
