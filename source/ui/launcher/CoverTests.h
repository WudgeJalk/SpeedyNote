#pragma once

// ============================================================================
// CoverTests - covers, "Move to a notebook", note pictures (Steps N2-N4).
// Group: --test-covers
// ============================================================================
// 1. CoverPainter: every design draws, the picture fills its cover, a big
//    picture is copied smaller. A sheet of all designs in %TEMP%.
// 2. A note's cover: chosen in the New note window, its picture copied into
//    the note, saved in document.json, kept by the orphan cleanup, read by the
//    start screen; a note without one writes no key. A card picture in %TEMP%.
// 3. A notebook's cover: saved in its small file with the other keys kept,
//    its picture copied into the folder, removed again.
// 4. Move a loose note into a section (the helper of both pages); a note never
//    opened gets its picture (id from its manifest); the Library page makes a
//    missing one on its own. Pictures of the Library page, the cover window and
//    the New note window in %TEMP%.
// Temporary folders only; the user's settings and start screen are put back.
// ============================================================================

#include "LibraryView.h"
#include "NotebookCardDelegate.h"
#include "../ThumbnailRenderer.h"
#include "../dialogs/NewNoteDialog.h"
#include "../widgets/CoverPicker.h"
#include "../../core/CoverPainter.h"
#include "../../core/Document.h"
#include "../../core/DocumentManager.h"
#include "../../core/LibraryTree.h"
#include "../../core/NotebookLibrary.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QListWidget>
#include <QPainter>
#include <QSettings>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QTemporaryDir>
#include <cstdio>

namespace CoverTests {

inline bool check(bool ok, const QString& what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    std::fflush(stdout);
    return ok;
}

/// Puts the user's settings back, whatever the test wrote.
struct SettingsGuard {
    QStringList keys{"library/notebooks", "library/lastNotebook", "library/lastSection", "library/showCovers",
                     "newNote/cover", "RecentDocuments"};
    QHash<QString, QVariant> saved;
    SettingsGuard()
    {
        QSettings s("SpeedyNote", "App");
        for (const QString& k : keys) if (s.contains(k)) saved.insert(k, s.value(k));
    }
    ~SettingsGuard()
    {
        QSettings s("SpeedyNote", "App");
        for (const QString& k : keys) {
            if (saved.contains(k)) s.setValue(k, saved.value(k));
            else s.remove(k);
        }
    }
};

inline QImage drawCover(const QJsonObject& cover, const QString& title, const QString& baseDir,
                        const QSize& size = QSize(150, 200))
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter p(&image);
    CoverPainter::paint(p, QRectF(QPointF(0, 0), QSizeF(size)), cover, title, baseDir);
    return image;
}

inline bool closeTo(const QColor& a, const QColor& b, int tolerance = 12)
{
    return qAbs(a.red() - b.red()) <= tolerance && qAbs(a.green() - b.green()) <= tolerance
        && qAbs(a.blue() - b.blue()) <= tolerance;
}

inline QString makePhoto(const QString& path, const QSize& size, const QColor& color)
{
    QImage photo(size, QImage::Format_RGB32);
    photo.fill(color);
    {
        QPainter p(&photo);
        p.setBrush(color.lighter(140));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(QPointF(0, 0), QSizeF(size)).adjusted(size.width() * 0.3, size.height() * 0.05,
                                                                   -size.width() * 0.3, -size.height() * 0.55));
    }
    photo.save(path);
    return path;
}

inline bool testPainter(const QString& dir)
{
    bool ok = true;
    const QString photo = makePhoto(dir + QStringLiteral("/photo.png"), QSize(400, 300), QColor("#c04040"));
    const QColor blue("#4a6fa5");
    ok &= check(closeTo(drawCover({{"design", "plain"}, {"color", blue.name()}}, QString(), QString()).pixelColor(75, 100), blue, 2),
                QStringLiteral("plain: the cover is its colour"));
    ok &= check(closeTo(drawCover({}, QString(), QString()).pixelColor(75, 100), blue, 2),
                QStringLiteral("no keys: the default blue"));
    for (const QString& design : CoverPainter::designs()) {
        const QImage image = drawCover({{"design", design}, {"color", "#2f7f86"}, {"image", photo}},
                                       QStringLiteral("HEIG26"), QString());
        ok &= check(image.pixelColor(2, 100) != QColor(Qt::white) && image.pixelColor(75, 100) != QColor(Qt::white),
                    QStringLiteral("design draws: ") + design);
    }
    const QImage pictured = drawCover({{"design", "picture"}, {"image", photo}}, QString(), QString());
    ok &= check(closeTo(pictured.pixelColor(10, 190), QColor("#c04040")), QStringLiteral("picture fills the cover"));
    ok &= check(closeTo(drawCover({{"design", "picture"}, {"image", "gone.png"}}, QString(), dir).pixelColor(75, 100), blue, 2),
                QStringLiteral("a missing picture: the colour"));

    // A sheet to look at: every design, a dark and a light colour.
    const QStringList designs = CoverPainter::designs();
    QImage sheet(designs.size() * 160 + 10, 2 * 210 + 10, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(QColor("#f0f0f0"));
    {
        QPainter p(&sheet);
        for (int row = 0; row < 2; ++row) {
            for (int i = 0; i < designs.size(); ++i) {
                const QJsonObject cover{{"design", designs[i]}, {"color", row ? "#efe6d2" : "#2c3e63"}, {"image", photo}};
                CoverPainter::paint(p, QRectF(10 + i * 160, 10 + row * 210, 150, 200), cover,
                                    QStringLiteral("Analyse II – exercices"), QString());
            }
        }
    }
    const QString png = QDir::temp().filePath(QStringLiteral("speedynote_covers.png"));
    ok &= check(sheet.save(png), QStringLiteral("sheet of designs: ") + png);

    // A big photo is copied smaller.
    const QString big = makePhoto(dir + QStringLiteral("/big.png"), QSize(1600, 1000), QColor("#3f7d5a"));
    QString error;
    const QString name = CoverPainter::adoptPicture(big, dir + QStringLiteral("/out"), QStringLiteral("cover"), &error);
    const QSize copied = QImageReader(dir + QStringLiteral("/out/") + name).size();
    ok &= check(!name.isEmpty() && copied == QSize(1200, 750), QStringLiteral("a big picture is copied at 1200 px: ") + name + error);
    ok &= check(CoverPainter::adoptPicture(dir + QStringLiteral("/nothing.png"), dir, QStringLiteral("x"), &error).isEmpty()
                && !error.isEmpty(), QStringLiteral("not a picture: refused with a reason"));
    return ok;
}

inline bool testNoteCover(const QString& dir)
{
    bool ok = true;
    SettingsGuard guard;
    DocumentManager manager;
    NewNoteSpec spec = NewNote::defaults(false, dir + QStringLiteral("/Notes"));
    spec.name = QStringLiteral("Cover note");
    spec.customImage.clear();
    spec.cover = {{"design", "label"}, {"color", "#3f7d5a"}, {"image", dir + QStringLiteral("/photo.png")}};
    QString path;
    {
        Document* doc = manager.createDocument(spec.name);
        QString error;
        ok &= check(NewNote::apply(doc, spec, &error), QStringLiteral("apply with a cover ") + error);
        const QString image = doc->cover.value("image").toString();
        ok &= check(!image.isEmpty() && !QFileInfo(image).isAbsolute(), QStringLiteral("the cover's picture is a file of the note: ") + image);
        ok &= check(!doc->modified, QStringLiteral("a cover is not an edit"));
        path = NewNote::bundlePathFor(spec);
        QDir().mkpath(spec.folder);
        ok &= check(manager.saveDocumentAs(doc, path), QStringLiteral("saved"));
        manager.closeDocument(doc);   // runs the orphan cleanup
        ok &= check(QFile::exists(path + QStringLiteral("/assets/images/") + image),
                    QStringLiteral("the picture is kept by the orphan cleanup"));
    }
    Document* back = manager.loadDocument(path);
    ok &= check(back && back->cover.value("design").toString() == QLatin1String("label")
                && back->cover.value("color").toString() == QLatin1String("#3f7d5a"),
                QStringLiteral("the cover loads back"));
    if (back) manager.closeDocument(back);
    bool listed = false;
    for (const NotebookInfo& info : NotebookLibrary::instance()->recentNotebooks())
        if (info.bundlePath == path) listed = info.cover.value("design").toString() == QLatin1String("label");
    ok &= check(listed, QStringLiteral("the start screen knows the cover"));
    {
        Document plain;
        ok &= check(!plain.toJson().contains("cover"), QStringLiteral("no cover: no key written"));
    }

    // Two cards: with a cover, and without (page 1).
    QStandardItemModel model;
    NotebookInfo withCover;
    withCover.bundlePath = path;
    withCover.name = spec.name;
    for (const NotebookInfo& info : NotebookLibrary::instance()->recentNotebooks())
        if (info.bundlePath == path) withCover = info;
    NotebookInfo without = withCover;
    without.cover = {};
    without.name = QStringLiteral("No cover");
    for (const NotebookInfo& info : {withCover, without}) {
        auto* item = new QStandardItem;
        item->setData(QVariant::fromValue(info), NotebookCardDelegate::NotebookInfoRole);
        item->setData(info.displayName(), NotebookCardDelegate::DisplayNameRole);
        item->setData(QDateTime::currentDateTime(), NotebookCardDelegate::LastModifiedRole);
        model.appendRow(item);
    }
    NotebookCardDelegate delegate;
    QImage cards(2 * 150 + 10, 190, QImage::Format_ARGB32_Premultiplied);
    cards.fill(QColor("#f5f5f5"));
    {
        QPainter p(&cards);
        for (int row = 0; row < 2; ++row) {
            QStyleOptionViewItem option;
            option.rect = QRect(5 + row * 150, 5, 140, 180);
            option.state = QStyle::State_Enabled;
            delegate.paint(&p, option, model.index(row, 0));
        }
    }
    const QString png = QDir::temp().filePath(QStringLiteral("speedynote_card_cover.png"));
    ok &= check(cards.save(png), QStringLiteral("card pictures: ") + png);
    ok &= check(closeTo(cards.pixelColor(5 + 70, 5 + 8 + 70), QColor("#3f7d5a"), 30),
                QStringLiteral("the card shows the cover"));
    return ok;
}

inline bool testNotebookCover(const QString& dir)
{
    bool ok = true;
    QString error;
    const QString notebook = LibraryTree::createNotebook(dir, QStringLiteral("Covered"), &error);
    LibraryTree::setColor(notebook, QColor("#b0413e"));
    ok &= check(LibraryTree::setCover(notebook, {{"design", "picture"}, {"image", dir + QStringLiteral("/photo.png")}}, &error),
                QStringLiteral("a notebook cover with a picture ") + error);
    QJsonObject meta = LibraryTree::readMeta(notebook);
    const QString image = meta.value("cover").toObject().value("image").toString();
    ok &= check(image.startsWith(QLatin1String(".speedynote-cover.")) && QFile::exists(notebook + "/" + image),
                QStringLiteral("its picture is copied into the folder: ") + image);
    ok &= check(meta.value("notebook").toBool() && meta.value("color").toString() == QLatin1String("#b0413e"),
                QStringLiteral("the other keys of the small file are kept"));
    ok &= check(LibraryTree::setCover(notebook, {{"design", "spine"}, {"color", "#2c3e63"}}),
                QStringLiteral("a cover without a picture"));
    ok &= check(!QFile::exists(notebook + "/" + image) && !LibraryTree::cover(notebook).contains("image"),
                QStringLiteral("the picture no longer used is gone"));
    ok &= check(LibraryTree::setCover(notebook, {}) && !LibraryTree::readMeta(notebook).contains("cover")
                && LibraryTree::readMeta(notebook).value("notebook").toBool(),
                QStringLiteral("no cover: the key goes, the rest stays"));
    const QString bare = dir + QStringLiteral("/Bare");
    QDir().mkpath(bare);
    ok &= check(LibraryTree::setCover(bare, {}) && !QFile::exists(bare + "/" + LibraryTree::META_FILE),
                QStringLiteral("clearing a cover never creates the small file"));
    LibraryTree::setCover(notebook, {{"design", "picture"}, {"image", dir + QStringLiteral("/photo.png")}});
    return ok;
}

inline bool testMoveAndPictures(const QString& dir)
{
    bool ok = true;
    SettingsGuard guard;
    QSettings settings("SpeedyNote", "App");
    DocumentManager manager;
    QString error;
    const QString notebook = LibraryTree::createNotebook(dir, QStringLiteral("HEIG26"), &error);
    LibraryTree::setColor(notebook, QColor("#3f7d5a"));
    const QString section = LibraryTree::createSection(notebook, QStringLiteral("04_Math_TIN"), &error);
    settings.setValue("library/notebooks", QStringList{QDir::cleanPath(dir + QStringLiteral("/Covered")), notebook});
    settings.setValue("library/lastNotebook", notebook);
    settings.setValue("library/lastSection", section);
    settings.setValue("library/showCovers", true);

    auto makeNote = [&](const QString& folder, const QString& name, const QString& paper, const QJsonObject& cover) {
        NewNoteSpec spec = NewNote::defaults(false, folder);
        spec.name = name;
        spec.paperId = paper;
        spec.customImage.clear();
        spec.cover = cover;
        Document* doc = manager.createDocument(name);
        NewNote::apply(doc, spec);
        const QString path = NewNote::bundlePathFor(spec);
        QDir().mkpath(folder);
        manager.saveDocumentAs(doc, path);
        manager.closeDocument(doc);
        return path;
    };

    // N2: a loose note moves into a section; the start screen follows it.
    const QString loose = makeNote(dir + QStringLiteral("/Loose"), QStringLiteral("Loose note"), QStringLiteral("cornell"), {});
    const QString id = Document::peekBundleId(loose);
    const QString moved = LibraryView::moveNoteInto(nullptr, loose, section);
    ok &= check(moved == section + QStringLiteral("/Loose note.snb") && QFile::exists(moved + QStringLiteral("/document.json"))
                && !QFileInfo::exists(loose), QStringLiteral("moved into the section: ") + moved);
    bool follows = false;
    for (const NotebookInfo& info : NotebookLibrary::instance()->recentNotebooks())
        if (info.bundlePath == moved) follows = info.documentId == id;
    ok &= check(follows, QStringLiteral("the start screen lists it at its new place"));
    ok &= check(LibraryTree::children(section).contains(QStringLiteral("Loose note.snb")),
                QStringLiteral("it is in the section's order"));

    // N4: a note never opened here (not on the start screen) gets its picture.
    const QString quiet = makeNote(section, QStringLiteral("Never opened"), QStringLiteral("month-grid-5"), {});
    NotebookLibrary* library = NotebookLibrary::instance();
    library->invalidateThumbnail(quiet);
    library->removeFromRecent(quiet);
    ok &= check(library->thumbnailPathFor(quiet).isEmpty(), QStringLiteral("no picture yet"));
    {
        auto doc = Document::loadBundle(quiet);
        const QPixmap pm = ThumbnailRenderer::renderFirstPage(doc.get(), 180, 1.0);
        ok &= check(!pm.isNull() && pm.width() == 180 && pm.height() > 180, QStringLiteral("page 1 is drawn, 180 px wide"));
    }
    makeNote(section, QStringLiteral("With a cover"), QStringLiteral("lined-wide"),
             {{"design", "stripes"}, {"color", "#b0413e"}});
    const QString canvas = section + QStringLiteral("/Canvas.snb");
    {
        Document* doc = manager.createEdgelessDocument(QStringLiteral("Canvas"));
        manager.saveDocumentAs(doc, canvas);
        manager.closeDocument(doc);
        library->invalidateThumbnail(canvas);
        library->removeFromRecent(canvas);
    }

    // The Library page makes the missing pictures itself while it is shown.
    LibraryView view;
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(960, 520);
    view.show();
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 6000 && (library->thumbnailPathFor(quiet).isEmpty() || library->thumbnailPathFor(canvas).isEmpty()))
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    ok &= check(!library->thumbnailPathFor(quiet).isEmpty(), QStringLiteral("the Library page made the missing picture"));
    ok &= check(!library->thumbnailPathFor(canvas).isEmpty(), QStringLiteral("... and the one of an endless note"));
    QApplication::processEvents();
    auto* notes = view.findChildren<QListWidget*>().value(1);   // notebooks, then notes (sections is a tree)
    ok &= check(notes && notes->count() == 4 && notes->iconSize() == QSize(48, 64), QStringLiteral("four notes with pictures"));
    const QString png = QDir::temp().filePath(QStringLiteral("speedynote_library_covers.png"));
    ok &= check(view.grab().save(png), QStringLiteral("Library page picture: ") + png);
    settings.setValue("library/showCovers", false);
    view.reload();
    const QString png2 = QDir::temp().filePath(QStringLiteral("speedynote_library_nocovers.png"));
    ok &= check(view.grab().save(png2), QStringLiteral("covers off: ") + png2);
    view.hide();

    for (const QString& p : {quiet, canvas}) library->invalidateThumbnail(p);
    return ok;
}

inline bool testWindows(const QString& dir)
{
    bool ok = true;
    SettingsGuard guard;
    const QString photo = dir + QStringLiteral("/photo.png");
    {
        CoverPicker picker({{"design", "spine"}, {"color", "#6c4f9c"}}, QStringLiteral("Physique"), QString());
        picker.setDesign(QStringLiteral("frame"));
        const QJsonObject c = picker.cover();
        ok &= check(c.value("design").toString() == QLatin1String("frame") && c.value("color").toString() == QLatin1String("#6c4f9c")
                    && !c.contains("showTitle") && !c.contains("image"),
                    QStringLiteral("picker: a design keeps the colour; defaults are not written"));
        picker.setDesign(QString());
        ok &= check(picker.cover().isEmpty(), QStringLiteral("picker: No cover"));
        picker.setPicture(photo);
        ok &= check(picker.cover().value("image").toString() == photo && picker.cover().value("design").toString() == QLatin1String("picture"),
                    QStringLiteral("picker: a picture"));
        picker.setAttribute(Qt::WA_DontShowOnScreen);
        picker.show();
        QApplication::processEvents();
        const QString png = QDir::temp().filePath(QStringLiteral("speedynote_coverpicker.png"));
        ok &= check(picker.grab().save(png), QStringLiteral("cover window picture: ") + png);
    }
    {
        NewNoteSpec start = NewNote::defaults(false, dir);
        start.name = QStringLiteral("Physique");
        start.cover = {{"design", "label"}, {"color", "#2f7f86"}};
        NewNoteDialog dialog(start);
        ok &= check(dialog.spec().cover == start.cover, QStringLiteral("the New note window keeps the cover"));
        NewNote::remember(dialog.spec());
        ok &= check(NewNote::defaults(false).cover == start.cover, QStringLiteral("the cover comes back next time"));
        NewNoteSpec withPhoto = start;
        withPhoto.cover["image"] = photo;
        NewNote::remember(withPhoto);
        ok &= check(NewNote::defaults(false).cover.isEmpty(), QStringLiteral("a photo cover is not kept for the next note"));
        dialog.setAttribute(Qt::WA_DontShowOnScreen);
        dialog.resize(980, 690);
        dialog.show();
        QApplication::processEvents();
        QApplication::processEvents();
        const QString png = QDir::temp().filePath(QStringLiteral("speedynote_newnote_cover.png"));
        ok &= check(dialog.grab().save(png), QStringLiteral("New note window with a cover: ") + png);
    }
    return ok;
}

inline bool runAllTests()
{
    QTemporaryDir temp;
    if (!temp.isValid()) return check(false, QStringLiteral("temporary folder"));
    const QString dir = QDir::cleanPath(temp.path());
    DocumentManager::setUnsavedDirForTests(dir + QStringLiteral("/unsaved"));
    bool ok = true;
    ok &= testPainter(dir);
    ok &= testNoteCover(dir);
    ok &= testNotebookCover(dir);
    ok &= testMoveAndPictures(dir);
    ok &= testWindows(dir);
    DocumentManager::setUnsavedDirForTests(QString());
    // Take the test notes (and their pictures) out of the user's start screen again.
    NotebookLibrary* library = NotebookLibrary::instance();
    for (const NotebookInfo& info : library->recentNotebooks()) {
        if (!info.bundlePath.startsWith(dir)) continue;
        library->invalidateThumbnail(info.bundlePath);
        library->removeFromRecent(info.bundlePath);
    }
    library->save();
    std::printf("\n%s\n", ok ? "All cover tests passed." : "Some cover tests FAILED.");
    return ok;
}

} // namespace CoverTests
