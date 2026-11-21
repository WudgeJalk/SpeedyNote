#pragma once

// Tests of the library model (roadmap C1). Run: speedynote --test-librarytree
// Everything happens in temporary folders; no setting is written.

#include "LibraryTree.h"
#include "../ui/launcher/LibraryView.h"

#include <QContextMenuEvent>
#include <QDebug>
#include <QEventLoop>
#include <QGuiApplication>
#include <QListWidget>
#include <QMouseEvent>
#include <QStyleHints>
#include <QTimer>
#include <QMap>
#include <QSettings>
#include <QTreeWidget>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

namespace LibraryTreeTests {

inline bool check(bool condition, const char* message)
{
    if (!condition) qCritical() << "FAIL:" << message;
    return condition;
}

inline QString makeNote(const QString& dir, const QString& name, const QString& id = "id-1")
{
    const QString path = dir + "/" + name + ".snb";
    QDir().mkpath(path + "/pages");
    QFile f(path + "/document.json");
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(QJsonObject{{"name", name}, {"notebook_id", id}}).toJson());
    QFile marker(path + "/.snb_marker");
    if (marker.open(QIODevice::WriteOnly)) marker.write("x");
    return path;
}

inline QStringList names(const QStringList& paths)
{
    QStringList out;
    for (const QString& p : paths) out << LibraryTree::displayName(p);
    return out;
}

inline QStringList treeListing(const QString& root)
{
    QStringList out;
    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) out << QDir(root).relativeFilePath(it.next());
    out.sort();
    return out;
}

inline bool testOrder()
{
    QTemporaryDir tmp;
    const QString nb = tmp.path() + "/HEIG26";
    QDir().mkpath(nb + "/b section");
    QDir().mkpath(nb + "/A section");
    makeNote(nb, "zeta");
    makeNote(nb, "alpha");
    bool ok = true;
    // No order file: alphabetical, sections and notes apart.
    ok &= check(names(LibraryTree::sections(nb)) == QStringList({"A section", "b section"}), "alphabetical sections");
    ok &= check(names(LibraryTree::notes(nb)) == QStringList({"alpha", "zeta"}), "alphabetical notes");
    ok &= check(!QFile::exists(nb + "/" + LibraryTree::META_FILE), "listing writes nothing");

    // Stale and repeated entries drop out, unknown ones come last.
    LibraryTree::writeMeta(nb, QJsonObject{{"order", QJsonArray{"zeta.snb", "gone", "b section", "zeta.snb"}}});
    QDir().mkpath(nb + "/C copied by hand");
    ok &= check(LibraryTree::children(nb)
                    == QStringList({"zeta.snb", "b section", "A section", "alpha.snb", "C copied by hand"}),
                "stale dropped, unknown last");

    // Reorder.
    ok &= check(LibraryTree::placeBefore(nb, "alpha.snb", "zeta.snb"), "placeBefore saves");
    ok &= check(names(LibraryTree::notes(nb)) == QStringList({"alpha", "zeta"}), "note moved up");
    ok &= check(LibraryTree::placeBefore(nb, "b section"), "placeBefore to the end");
    ok &= check(LibraryTree::children(nb).last() == "b section", "section moved last");

    // Colour and marker keep the order.
    ok &= check(LibraryTree::setColor(nb + "/A section", QColor("#ff8800")), "set colour");
    ok &= check(LibraryTree::color(nb + "/A section") == QColor("#ff8800"), "colour read back");
    ok &= check(LibraryTree::markNotebook(nb), "mark notebook");
    ok &= check(LibraryTree::readMeta(nb).value("notebook").toBool(), "marker saved");
    ok &= check(LibraryTree::children(nb).last() == "b section", "marker keeps the order");
    LibraryTree::setColor(nb + "/A section", QColor());
    ok &= check(!LibraryTree::color(nb + "/A section").isValid(), "colour cleared");

    // A damaged order file is the same as none.
    QFile bad(nb + "/" + LibraryTree::META_FILE);
    if (bad.open(QIODevice::WriteOnly)) bad.write("{ broken");
    bad.close();
    ok &= check(LibraryTree::children(nb).first() == "A section", "damaged file lists alphabetically");
    return ok;
}

inline bool testCreateRenameMove()
{
    QTemporaryDir tmp;
    QString error;
    bool ok = true;
    const QString nb = LibraryTree::createNotebook(tmp.path(), "HEIG26", &error);
    ok &= check(!nb.isEmpty() && LibraryTree::readMeta(nb).value("notebook").toBool(), "notebook created and marked");
    ok &= check(LibraryTree::createNotebook(tmp.path(), "HEIG26", &error).isEmpty() && !error.isEmpty(),
                "notebook name clash refused");

    const QString math = LibraryTree::createSection(nb, "04_Math_TIN", &error);
    const QString info = LibraryTree::createSection(nb, "Info-Générale", &error);
    const QString cours = LibraryTree::createSection(math, "Cours", &error);
    const QString exam = LibraryTree::createSection(math, "Examen", &error);
    ok &= check(!cours.isEmpty() && !exam.isEmpty(), "sub-sections created");
    ok &= check(LibraryTree::children(nb) == QStringList({"04_Math_TIN", "Info-Générale"}), "new sections go last");
    ok &= check(LibraryTree::createSection(math, "Cours", &error).isEmpty(), "section name clash refused");
    ok &= check(LibraryTree::createSection(math, "a/b", &error).isEmpty(), "bad character refused");
    ok &= check(LibraryTree::createSection(math, "x.snb", &error).isEmpty(), ".snb name refused");

    const QString n1 = makeNote(cours, "outil pour hachure");
    makeNote(cours, "b");
    LibraryTree::placeBefore(cours, "outil pour hachure.snb", "b.snb");

    // Rename keeps the place, and the note's own name follows.
    const QString renamed = LibraryTree::rename(n1, "Hachures", &error);
    ok &= check(renamed == cours + "/Hachures.snb", "note renamed with .snb kept");
    ok &= check(LibraryTree::children(cours) == QStringList({"Hachures.snb", "b.snb"}), "rename keeps the place");
    QFile manifest(renamed + "/document.json");
    manifest.open(QIODevice::ReadOnly);
    ok &= check(QJsonDocument::fromJson(manifest.readAll()).object().value("name").toString() == "Hachures",
                "document.json name follows");
    manifest.close();
    ok &= check(LibraryTree::rename(renamed, "b", &error).isEmpty(), "rename clash refused");
    const QString math2 = LibraryTree::rename(math, "Math", &error);
    ok &= check(LibraryTree::children(nb) == QStringList({"Math", "Info-Générale"}), "section rename keeps the place");
    const QString cours2 = math2 + "/Cours";
    const QString exam2 = math2 + "/Examen";

    // Move between sections.
    const QString moved = LibraryTree::move(cours2 + "/Hachures.snb", exam2, QString(), &error);
    ok &= check(moved == exam2 + "/Hachures.snb" && QFile::exists(moved + "/document.json"), "note moved");
    ok &= check(LibraryTree::children(cours2) == QStringList({"b.snb"}), "gone from the old order");
    ok &= check(LibraryTree::children(exam2) == QStringList({"Hachures.snb"}), "in the new order");
    makeNote(cours2, "Hachures");
    ok &= check(LibraryTree::move(cours2 + "/Hachures.snb", exam2, QString(), &error).isEmpty()
                    && QFile::exists(cours2 + "/Hachures.snb"), "move clash refused, nothing lost");
    ok &= check(LibraryTree::move(math2, cours2, QString(), &error).isEmpty(), "move into itself refused");
    const QString infoMoved = LibraryTree::move(info, math2, "Cours", &error);
    ok &= check(LibraryTree::children(math2).first() == "Info-Générale", "section moved before a sibling");
    ok &= check(LibraryTree::children(nb) == QStringList({"Math"}), "section gone from the root order");
    ok &= check(LibraryTree::move(infoMoved, math2, QString(), &error) == infoMoved
                    && LibraryTree::children(math2).last() == "Info-Générale", "same parent only reorders");

    // Copy gets a new id and a free name.
    const QString copy = LibraryTree::copyNoteInto(cours2 + "/Hachures.snb", exam2, &error);
    ok &= check(copy == exam2 + "/Hachures (2).snb", "copy renamed on a clash");
    QFile copied(copy + "/document.json");
    copied.open(QIODevice::ReadOnly);
    const QJsonObject copyJson = QJsonDocument::fromJson(copied.readAll()).object();
    ok &= check(copyJson.value("notebook_id").toString() != "id-1" && copyJson.value("name") == "Hachures (2)",
                "copy has a new id and its name");
    ok &= check(QFile::exists(copy + "/.snb_marker"), "copy keeps hidden files");
    ok &= check(LibraryTree::notesUnder(nb).size() == 4, "notesUnder finds every note");
    return ok;
}

inline bool testMissingAndPack()
{
    QTemporaryDir tmp;
    QString error;
    bool ok = true;

    // A notebook folder that is gone: empty lists, clear errors, no crash.
    const QString gone = tmp.path() + "/gone";
    ok &= check(LibraryTree::children(gone).isEmpty() && LibraryTree::notesUnder(gone).isEmpty(), "missing folder lists empty");
    ok &= check(!LibraryTree::pack(gone, tmp.path() + "/x.snbpack", &error) && error.contains("missing"),
                "missing folder does not pack");

    const QString nb = LibraryTree::createNotebook(tmp.path(), "HEIG26", &error);
    const QString res = LibraryTree::createSection(nb, "Résumé", &error);
    LibraryTree::createSection(nb, "Brouillon", &error);  // stays empty
    LibraryTree::setColor(res, QColor("#3366cc"));
    makeNote(res, "été");
    makeNote(res, "a");
    LibraryTree::placeBefore(res, "été.snb", "a.snb");
    QFile big(res + "/été.snb/pages/p1.json");
    big.open(QIODevice::WriteOnly);
    big.write(QJsonDocument(QJsonObject{{"strokes", QString(20000, 'x')}}).toJson());
    big.close();

    const QString packFile = tmp.path() + "/out/HEIG26" + LibraryTree::PACK_SUFFIX;
    QDir().mkpath(tmp.path() + "/out");
    ok &= check(LibraryTree::pack(nb, packFile, &error), "pack");
    ok &= check(!QFile::exists(packFile + ".part"), "no part file left");
    ok &= check(!LibraryTree::pack(nb, nb + "/in.snbpack", &error), "pack inside itself refused");

    // Round trip: same files, order and colours. Unpacking next to the original renames.
    const QString back = LibraryTree::unpack(packFile, tmp.path(), &error);
    ok &= check(back == tmp.path() + "/HEIG26 (2)", "unpacked beside the original as (2)");
    ok &= check(treeListing(back) == treeListing(nb), "same files and empty folders");
    ok &= check(LibraryTree::children(back + "/Résumé") == QStringList({"été.snb", "a.snb"}), "order kept");
    ok &= check(LibraryTree::color(back + "/Résumé") == QColor("#3366cc"), "colour kept");
    ok &= check(QFileInfo(back + "/Résumé/été.snb/pages/p1.json").size()
                    == QFileInfo(res + "/été.snb/pages/p1.json").size(), "content kept");

    // A damaged pack fails and leaves nothing.
    QFile packed(packFile);
    packed.open(QIODevice::ReadOnly);
    const QByteArray bytes = packed.readAll();
    packed.close();
    const QString cut = tmp.path() + "/out/cut.snbpack";
    QFile cutFile(cut);
    cutFile.open(QIODevice::WriteOnly);
    cutFile.write(bytes.left(bytes.size() / 2));
    cutFile.close();
    const QString dest = tmp.path() + "/dest";
    QDir().mkpath(dest);
    ok &= check(LibraryTree::unpack(cut, dest, &error).isEmpty() && !error.isEmpty(), "cut pack refused");
    ok &= check(QDir(dest).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty(),
                "cut pack leaves nothing");
    QByteArray flipped = bytes;
    const int at = int(flipped.size() / 3);  // most likely inside an entry: the CRC must catch it
    flipped[at] = char(flipped[at] ^ 0x55);
    QFile flippedFile(cut);
    flippedFile.open(QIODevice::WriteOnly);
    flippedFile.write(flipped);
    flippedFile.close();
    const QString flippedResult = LibraryTree::unpack(cut, dest, &error);
    ok &= check(flippedResult.isEmpty() || treeListing(flippedResult) == treeListing(nb), "flipped byte never gives a silent half notebook");
    if (!flippedResult.isEmpty()) QDir(flippedResult).removeRecursively();
    ok &= check(LibraryTree::unpack(res + "/été.snb/document.json", dest, &error).isEmpty(), "not a pack refused");

    // A broken or half-synced folder does not pack, and leaves no file.
    const QString bad = tmp.path() + "/out/bad.snbpack";
    QFile::remove(nb + "/Résumé/a.snb/document.json");
    ok &= check(!LibraryTree::pack(nb, bad, &error) && error.contains("a") && !QFile::exists(bad),
                "note without document.json refused");
    makeNote(res, "a");
    QFile broken(res + "/été.snb/pages/p1.json");
    broken.open(QIODevice::WriteOnly | QIODevice::Truncate);
    broken.write("{\"strokes\": [1, 2");  // cut by a sync
    broken.close();
    ok &= check(!LibraryTree::pack(nb, bad, &error) && error.contains("p1.json") && !QFile::exists(bad),
                "broken JSON refused");
    return ok;
}

// A long press (finger, pen or mouse) opens the same menu as a right click.
// The menu event is caught here, so no menu opens.
inline bool checkLongPress(QTreeWidget* tree)
{
    struct MenuCatcher : QObject {
        int count = 0;
        QPoint pos;
        bool eventFilter(QObject*, QEvent* event) override {
            if (event->type() != QEvent::ContextMenu) return false;
            ++count;
            pos = static_cast<QContextMenuEvent*>(event)->pos();
            return true;
        }
    } catcher;
    QWidget* viewport = tree->viewport();
    viewport->installEventFilter(&catcher);
    const QPoint at = tree->visualItemRect(tree->currentItem()).center();
    auto send = [&](QEvent::Type type, Qt::MouseButtons buttons) {
        QMouseEvent event(type, QPointF(at), QPointF(viewport->mapToGlobal(at)), Qt::LeftButton, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &event);
    };
    auto wait = [](int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    };
    const int hold = QGuiApplication::styleHints()->mousePressAndHoldInterval();
    bool ok = true;
    send(QEvent::MouseButtonPress, Qt::LeftButton);
    wait(hold / 3);
    send(QEvent::MouseButtonRelease, Qt::NoButton);
    wait(hold);
    ok &= check(catcher.count == 0, "a short press opens no menu");
    send(QEvent::MouseButtonPress, Qt::LeftButton);
    wait(hold + 300);
    ok &= check(catcher.count == 1 && catcher.pos == at, "a long press opens the menu where it is");
    send(QEvent::MouseButtonRelease, Qt::NoButton);
    viewport->removeEventFilter(&catcher);
    return ok;
}

// Builds the Library page on a temporary library, in light and dark, and saves
// %TEMP%/sn_library_light.png and sn_library_dark.png to look at. The page's
// settings are put back as they were.
inline bool testLibraryViewPicture()
{
    QTemporaryDir tmp;
    QString error;
    const QString nb = LibraryTree::createNotebook(tmp.path(), "HEIG26", &error);
    LibraryTree::createSection(nb, "Info-Générale", &error);
    LibraryTree::createSection(nb, "Brouillon", &error);
    const QString math = LibraryTree::createSection(nb, "04_Math_TIN", &error);
    LibraryTree::setColor(math, QColor("#e57373"));
    const QString cours = LibraryTree::createSection(math, "Cours", &error);
    LibraryTree::setColor(cours, QColor("#64b5f6"));
    for (const char* sub : {"Examen", "Résumé", "Exercices", "Corrigés"}) LibraryTree::createSection(math, sub, &error);
    for (const char* note : {"outil pour hachure", "Intégrales", "Séries de Fourier"}) makeNote(cours, note);
    makeNote(nb, "loose note");

    QSettings settings("SpeedyNote", "App");
    const QStringList keys = {"library/notebooks", "library/lastNotebook", "library/lastSection"};
    QMap<QString, QVariant> saved;
    for (const QString& key : keys)
        if (settings.contains(key)) saved[key] = settings.value(key);
    settings.setValue(keys[0], QStringList{nb, tmp.path() + "/Physique (moved)"});
    settings.setValue(keys[1], nb);
    settings.setValue(keys[2], cours);

    bool ok = true;
    for (const bool dark : {false, true}) {
        LibraryView view;
        QFile qss(dark ? ":/resources/styles/launcher_dark.qss" : ":/resources/styles/launcher.qss");
        if (qss.open(QIODevice::ReadOnly)) view.setStyleSheet(QString::fromUtf8(qss.readAll()));
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(1100, 560);
        view.show();  // reloads
        const QList<QListWidget*> lists = view.findChildren<QListWidget*>();
        QTreeWidget* tree = view.findChild<QTreeWidget*>();
        ok &= check(lists.size() == 2 && tree, "three columns");
        if (lists.size() == 2 && tree) {
            ok &= check(lists[0]->count() == 2 && lists[0]->item(1)->text().contains("missing"),
                        "missing notebook shown greyed");
            ok &= check(tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == 3,
                        "notebook root with its three sections");
            ok &= check(tree->currentItem() && tree->currentItem()->text(0) == "Cours", "last section chosen again");
            ok &= check(lists[1]->count() == 3, "notes of the section listed");
        }
        ok &= check(view.grab().save(QDir::tempPath() + (dark ? "/sn_library_dark.png" : "/sn_library_light.png")),
                    "picture saved");
        if (!dark && tree && tree->currentItem()) ok &= checkLongPress(tree);
    }

    for (const QString& key : keys) {
        if (saved.contains(key)) settings.setValue(key, saved[key]);
        else settings.remove(key);
    }
    return ok;
}

inline bool runAllTests()
{
    qDebug() << "=== LibraryTree tests ===";
    bool ok = true;
    ok &= testOrder();
    ok &= testCreateRenameMove();
    ok &= testMissingAndPack();
    ok &= testLibraryViewPicture();
    qDebug() << (ok ? "All LibraryTree tests PASSED" : "Some LibraryTree tests FAILED");
    return ok;
}

} // namespace LibraryTreeTests
