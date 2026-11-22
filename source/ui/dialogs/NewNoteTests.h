#pragma once

// ============================================================================
// NewNoteTests - the "New note" window (Step N1). Group: --test-newnote
// ============================================================================
// 1. A note made from a spec (NewNote::apply + save at its place): size and
//    orientation, colour, paper and its fallback, a paper of your own, an
//    endless note, saved at once and loaded back, not marked changed.
// 2. The place: a name taken gets " (2)"; describe() inside and outside the
//    Library.
// 3. remember() then defaults() give the same choices (the user's settings are
//    put back after).
// 4. The window: its spec, Paged/Endless keeps or brings back the paper, and
//    pictures of it in light and dark mode in %TEMP%\speedynote_newnote_*.png.
// Temporary folders only.
// ============================================================================

#include "NewNoteDialog.h"
#include "DialogSizing.h"
#include "../widgets/LibraryPlacePicker.h"
#include "../../core/Document.h"
#include "../../core/DocumentManager.h"
#include "../../core/NotebookLibrary.h"
#include "../../core/Page.h"
#include "../../core/Paper.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cstdio>
#include <memory>

namespace NewNoteTests {

inline bool check(bool ok, const QString& what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    std::fflush(stdout);
    return ok;
}

/// Puts the user's settings back, whatever the test wrote.
struct SettingsGuard {
    QStringList keys{"page/width", "page/height", "background/color", "background/gridColor",
                     "background/paper", "background/type", "background/gridSpacing",
                     "background/lineSpacing", "newNote/ask", "newNote/landscape",
                     "newNote/edgeless", "newNote/place", "newNote/customImage", "newNote/cover", "library/notebooks",
                     "RecentDocuments"};   // DocumentManager adds every note it saves or loads
    QHash<QString, QVariant> saved;
    bool hadNewNoteGroup = false;
    SettingsGuard()
    {
        QSettings s("SpeedyNote", "App");
        for (const QString& k : keys) if (s.contains(k)) saved.insert(k, s.value(k));
        hadNewNoteGroup = s.childGroups().contains(QStringLiteral("newNote"));
    }
    ~SettingsGuard()
    {
        QSettings s("SpeedyNote", "App");
        for (const QString& k : keys) {
            if (saved.contains(k)) s.setValue(k, saved.value(k));
            else s.remove(k);
        }
        if (!hadNewNoteGroup) s.remove(QStringLiteral("newNote"));   // no empty key left behind
    }
};

inline NewNoteSpec sampleSpec(const QString& folder)
{
    NewNoteSpec spec;
    spec.name = QStringLiteral("Note test");
    spec.folder = folder;
    spec.sizePx = QSizeF(559, 794);   // A5
    spec.landscape = true;
    spec.paperColor = QColor("#fdf6e3");
    spec.lineColor = NewNote::lineColorFor(spec.paperColor, QColor());
    spec.paperId = QStringLiteral("cornell");
    return spec;
}

inline bool testMakeNotes(const QString& dir)
{
    SettingsGuard guard;
    bool ok = true;
    DocumentManager manager;
    const NewNoteSpec spec = sampleSpec(dir + QStringLiteral("/Section"));
    {
        Document* doc = manager.createDocument(spec.name);
        ok &= check(NewNote::apply(doc, spec), QStringLiteral("apply: paged note"));
        ok &= check(!doc->modified, QStringLiteral("the choices are not an edit"));
        const QString path = NewNote::bundlePathFor(spec);
        QDir().mkpath(spec.folder);
        ok &= check(manager.saveDocumentAs(doc, path), QStringLiteral("saved at its place at once: ") + path);
        manager.closeDocument(doc);
        Document* back = manager.loadDocument(path);
        Page* page = back ? back->page(0) : nullptr;
        ok &= check(page && page->size == QSizeF(794, 559), QStringLiteral("A5 landscape kept"));
        ok &= check(page && page->paperId == QLatin1String("cornell")
                    && page->backgroundType == Page::BackgroundType::Lines && page->lineSpacing == 27,
                    QStringLiteral("Cornell paper, its fallback lines for older versions"));
        ok &= check(page && page->backgroundColor == QColor("#fdf6e3") && page->gridColor == QColor("#c8c8c8"),
                    QStringLiteral("cream paper, grey lines"));
        ok &= check(back && back->defaultPaperId == QLatin1String("cornell")
                    && back->defaultPageSize == QSizeF(794, 559),
                    QStringLiteral("new pages of the note get the same paper and size"));
        if (back) manager.closeDocument(back);
        ok &= check(QFileInfo(NewNote::bundlePathFor(spec)).fileName() == QLatin1String("Note test (2).snb"),
                    QStringLiteral("a name taken gets (2)"));
    }
    {
        NewNoteSpec endless = spec;
        endless.edgeless = true;
        endless.name = QStringLiteral("Canvas test");
        Document* doc = manager.createEdgelessDocument(endless.name);
        NewNote::apply(doc, endless);
        ok &= check(doc->defaultPaperId == QLatin1String("lined-wide")
                    && doc->defaultBackgroundType == Page::BackgroundType::Lines,
                    QStringLiteral("endless: Cornell becomes lined paper that goes on forever"));
        ok &= check(manager.saveDocumentAs(doc, NewNote::bundlePathFor(endless)), QStringLiteral("endless note saved"));
        manager.closeDocument(doc);
    }
    {
        // Settings > Page's classic grid: kept at its own spacing, not turned into a paper.
        NewNoteSpec classic = spec;
        classic.name = QStringLiteral("Grid test");
        classic.paperId = QStringLiteral("grid");
        classic.spacing = 40;
        Document* doc = manager.createDocument(classic.name);
        NewNote::apply(doc, classic);
        const Page* page = doc->page(0);
        ok &= check(page && page->backgroundType == Page::BackgroundType::Grid && page->gridSpacing == 40
                        && page->paperId.isEmpty() && doc->defaultBackgroundType == Page::BackgroundType::Grid
                        && doc->defaultGridSpacing == 40,
                    QStringLiteral("the classic grid at its own spacing (40 px), not a paper"));
        manager.closeDocument(doc);
    }
    {
        // A paper of your own: copied into the note.
        QImage picture(200, 280, QImage::Format_RGB32);
        picture.fill(QColor("#e8f0ff"));
        const QString source = dir + QStringLiteral("/mine.png");
        picture.save(source);
        Paper::setUserPapersDirForTests(dir + QStringLiteral("/papers"));
        QString error;
        const QString png = Paper::importUserPaper(source, &error);
        NewNoteSpec mine = spec;
        mine.name = QStringLiteral("Own paper");
        mine.customImage = png;
        Document* doc = manager.createDocument(mine.name);
        ok &= check(!png.isEmpty() && NewNote::apply(doc, mine, &error), QStringLiteral("apply: your own paper ") + error);
        const QString path = NewNote::bundlePathFor(mine);
        ok &= check(manager.saveDocumentAs(doc, path), QStringLiteral("saved with your own paper"));
        const QString image = doc->defaultCustomImage;
        manager.closeDocument(doc);
        ok &= check(!image.isEmpty() && QFile::exists(path + QStringLiteral("/assets/images/") + image),
                    QStringLiteral("the picture is inside the note: ") + image);
        Document* back = manager.loadDocument(path);
        ok &= check(back && back->page(0) && back->page(0)->backgroundType == Page::BackgroundType::Custom,
                    QStringLiteral("the page loads with your own paper"));
        if (back) manager.closeDocument(back);
        Paper::setUserPapersDirForTests(QString());
    }
    return ok;
}

inline bool testPlaceAndDefaults(const QString& dir)
{
    bool ok = true;
    SettingsGuard guard;
    QSettings settings("SpeedyNote", "App");
    const QString notebook = QDir::cleanPath(dir + QStringLiteral("/HEIG26"));
    QDir().mkpath(notebook + QStringLiteral("/04_Math_TIN"));
    settings.setValue("library/notebooks", QStringList{notebook});
    ok &= check(LibraryPlacePicker::describe(notebook + QStringLiteral("/04_Math_TIN"))
                    == QStringLiteral("HEIG26 › 04_Math_TIN"),
                QStringLiteral("a section reads as Notebook > Section"));
    ok &= check(LibraryPlacePicker::describe(dir) == QDir::toNativeSeparators(QDir::cleanPath(dir)),
                QStringLiteral("a folder outside the Library reads as its path"));

    NewNoteSpec spec = sampleSpec(notebook);
    NewNote::remember(spec);
    const NewNoteSpec back = NewNote::defaults(false);
    ok &= check(back.folder == notebook && back.sizePx == spec.sizePx && back.landscape
                && back.paperColor == spec.paperColor && back.paperId == spec.paperId
                && back.customImage.isEmpty(),
                QStringLiteral("remember() then defaults(): the same choices"));
    ok &= check(settings.value("background/type").toInt() == static_cast<int>(Page::BackgroundType::Lines),
                QStringLiteral("the old background type follows the paper (Settings > Page reads it)"));
    // An endless note does not change the paper of pages.
    NewNoteSpec endless = spec;
    endless.edgeless = true;
    endless.paperId = QStringLiteral("dotted");
    NewNote::remember(endless);
    ok &= check(NewNote::defaults(false).paperId == QLatin1String("cornell"),
                QStringLiteral("an endless note keeps the paper of pages"));
    settings.remove("background/paper");
    settings.setValue("background/type", static_cast<int>(Page::BackgroundType::Grid));
    settings.setValue("background/gridSpacing", 40);
    const NewNoteSpec classic = NewNote::defaults(false);
    ok &= check(classic.paperId == QLatin1String("grid") && classic.spacing == 40,
                QStringLiteral("no paper yet: Settings > Page's grid stays, at its spacing"));
    NewNote::remember(classic);
    ok &= check(!settings.contains("background/paper")
                    && settings.value("background/type").toInt() == static_cast<int>(Page::BackgroundType::Grid)
                    && settings.value("background/gridSpacing").toInt() == 40,
                QStringLiteral("remember() leaves Settings > Page on its grid"));
    {
        NewNoteDialog dialog(classic);
        ok &= check(dialog.spec().paperId == QLatin1String("grid"), QStringLiteral("the window offers that grid first"));
        dialog.setAttribute(Qt::WA_DontShowOnScreen);
        dialog.resize(980, 690);
        dialog.show();
        QApplication::processEvents();
        const QString png = QDir::tempPath() + QStringLiteral("/speedynote_newnote_classic.png");
        ok &= check(dialog.grab().save(png), QStringLiteral("classic grid picture: ") + png);
        dialog.setEdgeless(true);
        ok &= check(dialog.spec().paperId == QLatin1String("grid"), QStringLiteral("the grid goes on an endless canvas"));
    }
    ok &= check(NewNote::defaults(false, dir).folder == QDir::cleanPath(dir), QStringLiteral("a folder hint wins"));
    ok &= check(NewNote::lineColorFor(QColor("#2b2b2b"), QColor("#c8c8c8")) == QColor("#404040")
                && NewNote::lineColorFor(QColor("#ffffff"), QColor("#a0a0ff")) == QColor("#a0a0ff"),
                QStringLiteral("dark paper gets dark lines; a fitting line colour is kept"));
    return ok;
}

inline QPalette lightPalette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(243, 243, 243));
    p.setColor(QPalette::WindowText, Qt::black);
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
    p.setColor(QPalette::Text, Qt::black);
    p.setColor(QPalette::Button, QColor(250, 250, 250));
    p.setColor(QPalette::ButtonText, Qt::black);
    p.setColor(QPalette::Highlight, QColor("#0078d4"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    return p;
}

inline QPalette darkPalette()
{
    QPalette p;
    p.setColor(QPalette::Window, QColor(45, 45, 45));
    p.setColor(QPalette::WindowText, Qt::white);
    p.setColor(QPalette::Base, QColor(35, 35, 35));
    p.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    p.setColor(QPalette::Text, Qt::white);
    p.setColor(QPalette::Button, QColor(53, 53, 53));
    p.setColor(QPalette::ButtonText, Qt::white);
    p.setColor(QPalette::Highlight, QColor("#316882"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(128, 128, 128));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(128, 128, 128));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(128, 128, 128));
    return p;
}

inline bool testWindow(const QString& dir)
{
    bool ok = true;
    SettingsGuard guard;
    QSettings("SpeedyNote", "App").setValue("library/notebooks", QStringList{QDir::cleanPath(dir + QStringLiteral("/HEIG26"))});
    QDir().mkpath(dir + QStringLiteral("/HEIG26/04_Math_TIN"));
    QImage picture(300, 420, QImage::Format_RGB32);
    picture.fill(QColor("#f4efe6"));
    {
        QPainter p(&picture);
        p.setPen(QPen(QColor("#b08968"), 3));
        for (int y = 40; y < 420; y += 30) p.drawLine(20, y, 280, y);
    }
    picture.save(dir + QStringLiteral("/Kraft.png"));
    Paper::setUserPapersDirForTests(dir + QStringLiteral("/papers"));
    Paper::importUserPaper(dir + QStringLiteral("/Kraft.png"), nullptr);

    NewNoteSpec start = sampleSpec(QDir::cleanPath(dir + QStringLiteral("/HEIG26/04_Math_TIN")));
    start.landscape = false;
    start.sizePx = QSizeF(794, 1123);
    start.paperColor = Qt::white;
    start.lineColor = QColor("#c8c8c8");
    {
        NewNoteDialog dialog(start);
        const NewNoteSpec s = dialog.spec();
        ok &= check(s.name == start.name && s.folder == start.folder && s.sizePx == start.sizePx
                    && !s.landscape && s.paperId == QLatin1String("cornell") && !s.edgeless,
                    QStringLiteral("the window starts from the given choices"));
        dialog.setEdgeless(true);
        ok &= check(dialog.spec().edgeless && dialog.spec().paperId == QLatin1String("lined-wide"),
                    QStringLiteral("Endless: a page paper gives way to one that goes on forever"));
        dialog.setEdgeless(false);
        ok &= check(dialog.spec().paperId == QLatin1String("cornell"), QStringLiteral("Paged again: Cornell comes back"));
        dialog.selectPaper(QStringLiteral("staves"));
        dialog.setEdgeless(true);
        ok &= check(dialog.spec().paperId == QLatin1String("staves"), QStringLiteral("staves go on forever: kept"));
        dialog.setEdgeless(false);
        dialog.selectPaper(QStringLiteral("no-such-paper"));
        ok &= check(dialog.spec().paperId == QLatin1String("plain"), QStringLiteral("an unknown paper falls back to plain"));
    }

    // Pictures to look at, light and dark.
    const QPalette old = QApplication::palette();
    for (const bool dark : {false, true}) {
        QApplication::setPalette(dark ? darkPalette() : lightPalette());
        NewNoteSpec s = start;
        s.paperColor = dark ? QColor("#2b2b2b") : QColor(Qt::white);
        s.lineColor = NewNote::lineColorFor(s.paperColor, QColor());
        NewNoteDialog dialog(s);
        dialog.setAttribute(Qt::WA_DontShowOnScreen);
        dialog.resize(980, 690);
        dialog.show();
        QApplication::processEvents();
        QApplication::processEvents();
        const QString png = QDir::temp().filePath(dark ? QStringLiteral("speedynote_newnote_dark.png")
                                                       : QStringLiteral("speedynote_newnote_light.png"));
        ok &= check(dialog.grab().save(png), QStringLiteral("window picture: ") + png);
        if (!dark) {
            dialog.setLandscape(true);
            dialog.selectPaper(QStringLiteral("month-grid-5"));
            QApplication::processEvents();
            const QString png2 = QDir::temp().filePath(QStringLiteral("speedynote_newnote_landscape.png"));
            ok &= check(dialog.grab().save(png2), QStringLiteral("landscape picture: ") + png2);
            dialog.setEdgeless(true);
            QApplication::processEvents();
            const QString png3 = QDir::temp().filePath(QStringLiteral("speedynote_newnote_endless.png"));
            ok &= check(dialog.grab().save(png3), QStringLiteral("endless picture: ") + png3);
        }
    }
    {
        // A phone screen: one column, no page picture.
        QApplication::setPalette(lightPalette());
        NewNoteDialog::setNarrowForTests(true);
        NewNoteDialog dialog(start);
        NewNoteDialog::setNarrowForTests(false);
        dialog.setAttribute(Qt::WA_DontShowOnScreen);
        dialog.resize(400, 760);
        dialog.show();
        QApplication::processEvents();
        QApplication::processEvents();
        const QString png = QDir::temp().filePath(QStringLiteral("speedynote_newnote_narrow.png"));
        ok &= check(dialog.grab().save(png), QStringLiteral("narrow picture: ") + png);
    }
    QApplication::setPalette(old);
    Paper::setUserPapersDirForTests(QString());
    return ok;
}

// F5 (settings window): a wheel over a box without the focus scrolls the page.
inline bool testWheelNeedsFocus()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    auto* combo = new QComboBox;
    combo->addItems({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    auto* spin = new QSpinBox;
    layout->addWidget(combo);
    layout->addWidget(spin);
    layout->addSpacing(2000);
    std::unique_ptr<QScrollArea> area(DialogSizing::inScrollArea(content));
    area->resize(240, 200);
    area->setAttribute(Qt::WA_DontShowOnScreen);
    area->show();
    QApplication::processEvents();
    DialogSizing::wheelNeedsFocus(area.get());
    // A sent event is not spontaneous, so Qt does not pass it on to the parent
    // here; a real one, left unaccepted, goes on to the scroll area.
    bool passedOn = true;
    for (QWidget* target : {static_cast<QWidget*>(combo), static_cast<QWidget*>(spin)}) {
        const QPointF at(10, 10);
        QWheelEvent wheel(at, target->mapToGlobal(at), QPoint(), QPoint(0, -120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(target, &wheel);
        passedOn &= !wheel.isAccepted();
    }
    return check(combo->currentIndex() == 0 && spin->value() == 0 && passedOn
                     && combo->focusPolicy() == Qt::StrongFocus,
                 QStringLiteral("a wheel over an unfocused list or number box leaves its value, for the page to scroll"));
}

inline bool runAllTests()
{
    QTemporaryDir temp;
    if (!temp.isValid()) return check(false, QStringLiteral("temporary folder"));
    DocumentManager::setUnsavedDirForTests(temp.path() + QStringLiteral("/unsaved"));
    bool ok = testWheelNeedsFocus();
    ok &= testMakeNotes(temp.path());
    ok &= testPlaceAndDefaults(temp.path());
    ok &= testWindow(temp.path());
    DocumentManager::setUnsavedDirForTests(QString());
    // DocumentManager lists every note it saves or loads on the start screen:
    // take the test notes out of the user's library again.
    for (const NotebookInfo& info : NotebookLibrary::instance()->recentNotebooks()) {
        if (info.bundlePath.startsWith(temp.path())) NotebookLibrary::instance()->removeFromRecent(info.bundlePath);
    }
    NotebookLibrary::instance()->save();
    std::printf("\n%s\n", ok ? "All New note tests passed." : "Some New note tests FAILED.");
    return ok;
}

} // namespace NewNoteTests
