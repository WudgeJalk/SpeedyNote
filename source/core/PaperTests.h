#pragma once

// ============================================================================
// PaperTests - page designs drawn by code (Step N, stage S5). Group: --test-paper
// ============================================================================
// 1. Every paper builds inside the page: A4, A5, Letter, portrait and landscape.
// 2. Endless papers join from tile to tile, each line drawn by one tile only.
// 3. Saved fields: "paper" / "customImage" round trip, absent when unused, an
//    old file loads without a paper; the fallback an older version draws; an
//    unknown id draws its fallback.
// 4. pdfOps: balanced q/Q, no font operators. A real export, read back as
//    pictures in %TEMP%.
// 5. A paper of your own: import, copy into a note, survives a save, the
//    orphan cleanup and a reload. Temporary folders only.
// 6. Timing of the densest papers (printed) and a contact sheet of all papers
//    in %TEMP%\speedynote_papers*.png to look at.
// ============================================================================

#include "Paper.h"
#include "Document.h"
#include "DocumentViewport.h"
#include "Page.h"
#include "../pdf/MuPdfExporter.h"
#include "../pdf/PdfProvider.h"
#include "../ui/dialogs/DocumentSettingsDialog.h"

#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QtMath>
#include <algorithm>
#include <cstdio>

namespace PaperTests {

inline bool check(bool ok, const QString& what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    std::fflush(stdout);
    return ok;
}

inline QString tempPng(const QString& name) { return QDir::temp().filePath(name); }

// ----- 1. Inside the page -----
inline bool testInsidePage()
{
    const QList<QPair<QString, QSizeF>> sizes = {
        {QStringLiteral("A4"), QSizeF(794, 1123)},
        {QStringLiteral("A5"), QSizeF(559, 794)},
        {QStringLiteral("Letter"), QSizeF(816, 1056)},
    };
    bool ok = true;
    for (const Paper::PaperDef& def : Paper::all()) {
        for (const auto& size : sizes) {
            for (bool landscape : {false, true}) {
                const QSizeF s = landscape ? size.second.transposed() : size.second;
                const QRectF page(QPointF(0, 0), s);
                const QRectF inside = page.adjusted(-0.5, -0.5, 0.5, 0.5);
                const Paper::Drawing d = Paper::build(QString::fromLatin1(def.id), page);
                int items = 0;
                bool in = true;
                for (const auto& st : d.strokes) {
                    for (const QLineF& l : st.lines) {
                        in = in && inside.contains(l.p1()) && inside.contains(l.p2());
                        ++items;
                    }
                    for (const QRectF& b : st.boxes) {
                        in = in && inside.contains(b) && b.width() > 0 && b.height() > 0;
                        ++items;
                    }
                }
                for (const auto& dt : d.dots) {
                    in = in && inside.contains(dt.area);
                    ++items;
                }
                if (!d.labels.isEmpty()) in = in && inside.contains(d.labels.boundingRect());
                const bool empty = qstrcmp(def.id, "plain") == 0;
                const QString what = QStringLiteral("%1 on %2 %3").arg(QLatin1String(def.id), size.first,
                    landscape ? QStringLiteral("landscape") : QStringLiteral("portrait"));
                if (!in || (items == 0) != empty) {
                    ok = check(false, what + QStringLiteral(" stays inside the page and draws something"));
                }
            }
        }
    }
    return check(ok, QStringLiteral("all %1 papers stay inside A4, A5 and Letter, both ways").arg(Paper::all().size()));
}

// ----- 2. Endless tiles -----
inline bool testEndlessTiles()
{
    bool ok = true;
    const QRectF left(0, 0, 1024, 1024), right(1024, 0, 1024, 1024), below(0, 1024, 1024, 1024);
    for (const Paper::PaperDef& def : Paper::all()) {
        if (!def.endless || def.rules.isEmpty()) continue;
        const QString id = QString::fromLatin1(def.id);
        const Paper::Drawing a = Paper::build(id, left, true);
        const Paper::Drawing b = Paper::build(id, right, true);
        const Paper::Drawing c = Paper::build(id, below, true);
        // Horizontal lines meet at the seam: the same heights on both tiles.
        auto heights = [](const Paper::Drawing& d) {
            QList<int> ys;
            for (const auto& st : d.strokes)
                for (const QLineF& l : st.lines)
                    if (qFuzzyCompare(l.y1(), l.y2())) ys << qRound(l.y1() * 100);
            std::sort(ys.begin(), ys.end());
            return ys;
        };
        ok &= check(heights(a) == heights(b), id + QStringLiteral(": lines meet at the tile seam"));
        // No line or dot sits on two tiles: the seam belongs to the lower tile.
        bool once = true;
        for (const auto& st : c.strokes)
            for (const QLineF& l : st.lines)
                once = once && l.y1() >= 1024 && l.y2() <= 2048;
        for (const auto& dt : c.dots) once = once && dt.area.top() >= 1024 && dt.area.bottom() < 2048;
        ok &= check(once, id + QStringLiteral(": each line belongs to one tile"));
    }
    // Lined paper on the canvas is a plain lattice from world 0, no margin.
    const Paper::Drawing lined = Paper::build(QStringLiteral("lined-narrow"), left, true);
    ok &= check(!lined.strokes.isEmpty() && !lined.strokes[0].lines.isEmpty()
                && qFuzzyCompare(lined.strokes[0].lines[0].y1() + 1, 1.0),
                QStringLiteral("endless lined paper starts at world 0"));
    return ok;
}

// ----- 3. Saved fields, fallback, unknown ids -----
inline bool testSavedFields()
{
    bool ok = true;
    // A page with a paper writes the key and reads it back.
    Page page(QSizeF(794, 1123));
    page.paperId = QStringLiteral("lined-wide");
    int spacing = 0;
    ok &= check(Paper::fallbackFor(page.paperId, &page.backgroundType, &spacing)
                && page.backgroundType == Page::BackgroundType::Lines && spacing == 33,
                QStringLiteral("lined-wide falls back to lines 33 px apart"));
    page.lineSpacing = spacing;
    const QJsonObject json = page.toJson();
    ok &= check(json.value(QStringLiteral("paper")).toString() == QLatin1String("lined-wide")
                && json.value(QStringLiteral("backgroundType")).toInt() == int(Page::BackgroundType::Lines)
                && !json.contains(QStringLiteral("customImage")),
                QStringLiteral("a page writes \"paper\" and keeps its fallback type for older versions"));
    const auto back = Page::fromJson(json);
    ok &= check(back && back->paperId == QLatin1String("lined-wide"), QStringLiteral("\"paper\" reads back"));

    // Without a paper nothing new is written; an old file has none.
    Page plain(QSizeF(794, 1123));
    const QJsonObject plainJson = plain.toJson();
    ok &= check(!plainJson.contains(QStringLiteral("paper")) && !plainJson.contains(QStringLiteral("customImage")),
                QStringLiteral("a page without a paper writes no new key"));
    QJsonObject old = plainJson;
    old.remove(QStringLiteral("uuid"));
    const auto oldPage = Page::fromJson(old);
    ok &= check(oldPage && oldPage->paperId.isEmpty() && oldPage->customImage.isEmpty(),
                QStringLiteral("an old page loads with no paper"));

    Page custom(QSizeF(794, 1123));
    custom.backgroundType = Page::BackgroundType::Custom;
    custom.customImage = QStringLiteral("paper-mine.png");
    const auto customBack = Page::fromJson(custom.toJson());
    ok &= check(customBack && customBack->customImage == QLatin1String("paper-mine.png")
                && customBack->backgroundType == Page::BackgroundType::Custom,
                QStringLiteral("\"customImage\" reads back"));

    // Document defaults: written only when set, copied onto new pages.
    auto doc = Document::createNew(QStringLiteral("Paper test"));
    ok &= check(!doc->toJson().value(QStringLiteral("default_background")).toObject().contains(QStringLiteral("paper")),
                QStringLiteral("a document without a paper writes no new key"));
    doc->defaultPaperId = QStringLiteral("cornell");
    doc->defaultCustomImage = QStringLiteral("paper-x.png");
    const auto docBack = Document::fromJson(doc->toJson());
    ok &= check(docBack && docBack->defaultPaperId == QLatin1String("cornell")
                && docBack->defaultCustomImage == QLatin1String("paper-x.png"),
                QStringLiteral("default_background \"paper\" and \"custom_image\" read back"));
    doc->defaultCustomImage.clear();
    doc->addPage();
    ok &= check(doc->page(doc->pageCount() - 1)->paperId == QLatin1String("cornell"),
                QStringLiteral("a new page takes the document's paper"));

    // An unknown id (a newer version's paper) draws its fallback.
    Page future(QSizeF(400, 400));
    future.paperId = QStringLiteral("from-a-newer-version");
    future.backgroundType = Page::BackgroundType::Lines;
    future.lineSpacing = 40;
    future.gridColor = Qt::black;
    QImage img(400, 400, QImage::Format_RGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        future.renderBackground(p, nullptr, 1.0);
    }
    ok &= check(!Paper::find(future.paperId)
                && qMin(qGray(img.pixel(200, 39)), qGray(img.pixel(200, 40))) < 128
                && qGray(img.pixel(200, 20)) > 200,
                QStringLiteral("an unknown paper id draws its fallback lines"));
    return ok;
}

// ----- 4. PDF -----
inline bool testPdfOps()
{
    bool ok = true;
    for (const Paper::PaperDef& def : Paper::all()) {
        const QByteArray ops = Paper::pdfOps(QString::fromLatin1(def.id), QSizeF(794, 1123), QColor(200, 200, 200), false);
        const int q = ops.count("q\n") , Q = ops.count("Q\n");
        ok &= check(ops.startsWith("q\n") && ops.endsWith("Q\n") && q == 1 && Q == 1 && !ops.contains(" Tf"),
                    QStringLiteral("%1: pdfOps balanced q/Q, no font (%2 bytes)").arg(QLatin1String(def.id)).arg(ops.size()));
    }
    return ok;
}

inline bool testPdfExport(const QString& tempDir)
{
    auto doc = Document::createNew(QStringLiteral("Paper export"));
    const QStringList ids = {QStringLiteral("cornell"), QStringLiteral("month-grid-5"),
                             QStringLiteral("guitar-score"), QStringLiteral("dotted")};
    for (int i = 0; i < ids.size(); ++i) {
        if (i > 0) doc->addPage();
        Page* page = doc->page(i);
        page->paperId = ids[i];
        Paper::fallbackFor(ids[i], &page->backgroundType, nullptr);
        page->size = QSizeF(794, 1123);
    }
    doc->page(1)->backgroundColor = QColor(0xFD, 0xF6, 0xE3);   // cream

    const QString out = tempDir + QStringLiteral("/papers.pdf");
    MuPdfExporter exporter;
    exporter.setDocument(doc.get());
    PdfExportOptions options;
    options.outputPath = out;
    const PdfExportResult result = exporter.exportPdf(options);
    bool ok = check(result.success, QStringLiteral("a note with four papers exports to PDF"));
    if (!ok) return false;

    std::unique_ptr<PdfProvider> pdf = PdfProvider::create(out);
    ok &= check(pdf && pdf->isValid() && pdf->pageCount() == ids.size(), QStringLiteral("the PDF opens again"));
    if (!pdf || !pdf->isValid()) return false;
    for (int i = 0; i < ids.size(); ++i) {
        const QImage page = pdf->renderPageToImage(i, 60);
        const QString png = tempPng(QStringLiteral("speedynote_paper_pdf_%1.png").arg(ids[i]));
        page.save(png);
        // Ink across the middle column and row: the paper made it into the PDF.
        int inked = 0;
        for (int y = 0; y < page.height(); ++y)
            if (qGray(page.pixel(page.width() / 2, y)) < 235) ++inked;
        for (int x = 0; x < page.width(); ++x)
            if (qGray(page.pixel(x, page.height() / 2)) < 235) ++inked;
        ok &= check(inked >= 6, QStringLiteral("PDF page %1 (%2) shows its lines (%3 px): %4")
                                    .arg(i + 1).arg(ids[i]).arg(inked).arg(png));
    }
    return ok;
}

// ----- 5. A paper of your own -----
inline bool testUserPaper(const QString& tempDir)
{
    bool ok = true;
    Paper::setUserPapersDirForTests(tempDir + QStringLiteral("/papers"));

    QImage source(300, 420, QImage::Format_RGB32);
    source.fill(Qt::white);
    {
        QPainter p(&source);
        p.fillRect(0, 0, 300, 30, QColor(220, 40, 40));   // a red band at the top
    }
    const QString picture = tempDir + QStringLiteral("/My grid.png");
    source.save(picture);
    QString error;
    const QString png = Paper::importUserPaper(picture, &error);
    ok &= check(!png.isEmpty() && png.startsWith(Paper::userPapersDir()) && QFile::exists(png),
                QStringLiteral("a picture becomes a paper in the papers folder ") + error);
    ok &= check(Paper::importUserPaper(tempDir + QStringLiteral("/missing.png"), &error).isEmpty() && !error.isEmpty(),
                QStringLiteral("a missing file gives a plain reason: ") + error);

    auto doc = Document::createNew(QStringLiteral("Own paper"));
    const QString bundle = tempDir + QStringLiteral("/own.snb");
    ok &= check(doc->saveBundle(bundle), QStringLiteral("the note saves"));
    const QString name = Paper::adoptUserPaper(png, bundle, &error);
    ok &= check(!name.isEmpty() && QFile::exists(bundle + QStringLiteral("/assets/images/") + name),
                QStringLiteral("the paper is copied into the note: ") + name);
    ok &= check(Paper::adoptUserPaper(png, bundle, &error) == name, QStringLiteral("the same paper twice is one file"));

    Page* page = doc->page(0);
    page->backgroundType = Page::BackgroundType::Custom;
    page->customImage = name;
    doc->defaultBackgroundType = Page::BackgroundType::Custom;
    doc->defaultCustomImage = name;
    page->loadCustomBackground(bundle);
    ok &= check(!page->customBackground.isNull(), QStringLiteral("the page loads its paper"));
    doc->addPage();
    ok &= check(doc->page(1)->customImage == name
                && doc->page(1)->customBackground.cacheKey() == page->customBackground.cacheKey(),
                QStringLiteral("a new page shares the same picture"));
    QImage drawn(397, 561, QImage::Format_RGB32);
    {
        QPainter p(&drawn);
        page->renderBackground(p, nullptr, 0.5);
    }
    ok &= check(qRed(drawn.pixel(200, 5)) > 180 && qGreen(drawn.pixel(200, 5)) < 100,
                QStringLiteral("the page draws its own paper"));

    doc->markPageDirty(0);
    doc->markModified();
    ok &= check(doc->saveBundle(bundle), QStringLiteral("the note saves with its paper"));
    doc->cleanupOrphanedAssets();
    ok &= check(QFile::exists(bundle + QStringLiteral("/assets/images/") + name),
                QStringLiteral("the orphan cleanup keeps the paper"));
    doc.reset();
    auto again = Document::loadBundle(bundle);
    Page* reloaded = again ? again->page(0) : nullptr;
    ok &= check(reloaded && reloaded->customImage == name && !reloaded->customBackground.isNull()
                && again->defaultCustomImage == name,
                QStringLiteral("after a reload the page has its paper"));
    Paper::setUserPapersDirForTests(QString());
    return ok;
}

// ----- 6. Timing and pictures -----
inline void paintPaper(QImage& image, const QString& id, const QSizeF& pagePx, const QColor& paper,
                       const QColor& line, qreal scale, const QRect& clip = QRect())
{
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (!clip.isNull()) p.setClipRect(clip);
    p.scale(scale, scale);
    const QRectF rect(QPointF(0, 0), pagePx);
    p.fillRect(rect, paper);
    Paper::paint(p, id, rect, line);
}

inline bool testTiming()
{
    // The densest papers, a whole A3 page at 25 % and the few pixels of a pen event.
    for (const QString& id : {QStringLiteral("dotted"), QStringLiteral("squared"), QStringLiteral("month-grid-6")}) {
        const QSizeF a3(1123, 1587);
        QElapsedTimer t;
        t.start();
        const Paper::Drawing d = Paper::build(id, QRectF(QPointF(0, 0), a3));
        const qint64 buildUs = t.nsecsElapsed() / 1000;
        QImage quarter(281, 397, QImage::Format_ARGB32_Premultiplied);
        paintPaper(quarter, id, a3, Qt::white, QColor(200, 200, 200), 0.25);   // fills the cache
        t.restart();
        for (int i = 0; i < 20; ++i) paintPaper(quarter, id, a3, Qt::white, QColor(200, 200, 200), 0.25);
        const qint64 quarterUs = t.nsecsElapsed() / 1000 / 20;
        QImage full(1123, 1587, QImage::Format_ARGB32_Premultiplied);
        t.restart();
        for (int i = 0; i < 5; ++i) paintPaper(full, id, a3, Qt::white, QColor(200, 200, 200), 1.0);
        const qint64 fullUs = t.nsecsElapsed() / 1000 / 5;
        t.restart();
        for (int i = 0; i < 200; ++i)
            paintPaper(full, id, a3, Qt::white, QColor(200, 200, 200), 1.0, QRect(500, 700, 40, 40));
        const qint64 penUs = t.nsecsElapsed() / 1000 / 200;
        std::printf("TIME: %s on A3: build %lld us, whole page at 25%% %lld us, at 100%% %lld us, "
                    "a 40x40 pen update %lld us\n", qPrintable(id), buildUs, quarterUs, fullUs, penUs);
    }
    // The old grid at the same 19 px spacing, drawn the old way, for comparison.
    {
        QImage full(1123, 1587, QImage::Format_ARGB32_Premultiplied);
        auto oldGrid = [&](const QRect& clip) {
            QPainter p(&full);
            p.setRenderHint(QPainter::Antialiasing, true);
            if (!clip.isNull()) p.setClipRect(clip);
            Page::renderBackgroundPattern(p, QRectF(0, 0, 1123, 1587), Qt::white, Page::BackgroundType::Grid,
                                          QColor(200, 200, 200), 19, 19, 1.0);
        };
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 5; ++i) oldGrid(QRect());
        const qint64 fullUs = t.nsecsElapsed() / 1000 / 5;
        t.restart();
        for (int i = 0; i < 200; ++i) oldGrid(QRect(500, 700, 40, 40));
        const qint64 penUs = t.nsecsElapsed() / 1000 / 200;
        std::printf("TIME: the old grid (19 px) on A3: at 100%% %lld us, a 40x40 pen update %lld us\n",
                    fullUs, penUs);
    }
    std::fflush(stdout);
    return true;
}

inline bool saveSheet(const QString& file, const QSizeF& pagePx, const QColor& paper, const QColor& line)
{
    const QVector<Paper::PaperDef>& papers = Paper::all();
    const int cols = 5, cellW = 230;
    const qreal scale = cellW / pagePx.width();
    const int cellH = qCeil(pagePx.height() * scale), labelH = 22, gap = 12;
    const int rows = (papers.size() + cols - 1) / cols;
    QImage sheet(cols * (cellW + gap) + gap, rows * (cellH + labelH + gap) + gap, QImage::Format_RGB32);
    sheet.fill(QColor(120, 120, 120));
    for (int i = 0; i < papers.size(); ++i) {
        const int x = gap + (i % cols) * (cellW + gap), y = gap + (i / cols) * (cellH + labelH + gap);
        QImage cell(cellW, cellH, QImage::Format_RGB32);
        paintPaper(cell, QString::fromLatin1(papers[i].id), pagePx, paper, line, scale);
        QPainter p(&sheet);
        p.drawImage(x, y, cell);
        p.setPen(Qt::white);
        p.drawText(QRect(x, y + cellH, cellW, labelH), Qt::AlignCenter,
                   QStringLiteral("%1 (%2)").arg(Paper::displayName(papers[i]), QLatin1String(papers[i].id)));
    }
    return check(sheet.save(file), QStringLiteral("contact sheet saved: ") + file);
}

inline bool testPictures()
{
    bool ok = true;
    ok &= saveSheet(tempPng(QStringLiteral("speedynote_papers.png")), QSizeF(794, 1123), Qt::white, QColor(200, 200, 200));
    ok &= saveSheet(tempPng(QStringLiteral("speedynote_papers_landscape_dark.png")), QSizeF(794, 559),
                    QColor(0x2B, 0x2B, 0x2B), QColor(0x40, 0x40, 0x40));
    // A few papers at half size, to read the labels and the fine lines.
    const QStringList detail = {QStringLiteral("month-grid-5"), QStringLiteral("week-boxes"),
                                QStringLiteral("todo"), QStringLiteral("guitar-score")};
    QImage big(4 * 400, 566, QImage::Format_RGB32);
    big.fill(QColor(120, 120, 120));
    for (int i = 0; i < detail.size(); ++i) {
        QImage cell(397, 562, QImage::Format_RGB32);
        paintPaper(cell, detail[i], QSizeF(794, 1123), QColor(0xFD, 0xF6, 0xE3), QColor(200, 200, 200), 0.5);
        QPainter(&big).drawImage(i * 400, 2, cell);
    }
    const QString detailPng = tempPng(QStringLiteral("speedynote_papers_detail.png"));
    ok &= check(big.save(detailPng), QStringLiteral("detail sheet saved: ") + detailPng);
    // Two endless tiles side by side at 25 %: the seam must not show.
    QImage seam(512, 256, QImage::Format_RGB32);
    seam.fill(Qt::white);
    {
        QPainter p(&seam);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(0.25, 0.25);
        for (int tx = 0; tx < 2; ++tx) {
            const QRectF tile(tx * 1024, 0, 1024, 1024);
            Page::renderBackgroundPattern(p, tile, Qt::white, Page::BackgroundType::None, QColor(150, 150, 150),
                                          32, 32, 4.0, QStringLiteral("squared"), true);
        }
    }
    const QString seamPng = tempPng(QStringLiteral("speedynote_papers_seam.png"));
    ok &= check(seam.save(seamPng), QStringLiteral("endless seam picture saved: ") + seamPng);
    return ok;
}

// While writing, only the pen's few pixels are repainted and paint() skips
// what lies outside them. Painting a page in many small clipped pieces must
// give exactly the picture of one whole paint.
inline bool testClippedPaintsMatch()
{
    bool ok = true;
    const QSizeF a5(559, 794);
    for (const QString& id : {QStringLiteral("dotted"), QStringLiteral("squared"), QStringLiteral("month-grid-5"),
                              QStringLiteral("guitar-score"), QStringLiteral("todo-2")}) {
        for (qreal zoom : {1.0, 0.7}) {
            const QSize px = (a5 * zoom).toSize();
            QImage whole(px, QImage::Format_RGB32), pieces(px, QImage::Format_RGB32);
            paintPaper(whole, id, a5, Qt::white, QColor(200, 200, 200), zoom);
            for (int y = 0; y < px.height(); y += 41)
                for (int x = 0; x < px.width(); x += 37)
                    paintPaper(pieces, id, a5, Qt::white, QColor(200, 200, 200), zoom, QRect(x, y, 37, 41));
            ok &= check(whole == pieces, QStringLiteral("%1 at %2 %: small repaints match one whole paint")
                                             .arg(id).arg(zoom * 100));
        }
    }
    return ok;
}

// The real viewport, paged and endless, as a picture in %TEMP%.
inline bool testViewportPictures()
{
    bool ok = true;
    auto doc = Document::createNew(QStringLiteral("Paper viewport"));
    doc->defaultPaperId = QStringLiteral("month-grid-5");
    Page* first = doc->page(0);
    first->paperId = QStringLiteral("cornell");
    Paper::fallbackFor(first->paperId, &first->backgroundType, &first->lineSpacing);
    doc->addPage();   // takes the document's paper
    {
        DocumentViewport viewport;
        viewport.resize(900, 700);
        viewport.setDocument(doc.get());
        viewport.setZoomLevel(0.55);
        viewport.setPanOffset(QPointF(-40, -20));
        const QString png = tempPng(QStringLiteral("speedynote_paper_viewport.png"));
        ok &= check(viewport.grab().save(png), QStringLiteral("viewport picture: ") + png);
    }
    {
        // The note's settings list every paper and select the note's own.
        QTemporaryDir noPapers;
        Paper::setUserPapersDirForTests(noPapers.path());
        DocumentSettingsDialog dialog(nullptr, doc.get());
        Paper::setUserPapersDirForTests(QString());
        dialog.resize(480, 560);
        QComboBox* combo = nullptr;
        for (QComboBox* c : dialog.findChildren<QComboBox*>())
            if (c->findData(QStringLiteral("cornell")) >= 0) combo = c;
        QStringList items;
        for (int i = 0; combo && i < combo->count(); ++i)
            if (!combo->itemText(i).isEmpty()) items << combo->itemText(i);
        std::printf("Paper list: %s\n", qPrintable(items.join(QStringLiteral(" | "))));
        ok &= check(combo && combo->currentData().toString() == QLatin1String("month-grid-5")
                    && items.size() == 3 + Paper::all().size() - 1 + 1,
                    QStringLiteral("the note's settings list every paper and show its own"));
        const QString png = tempPng(QStringLiteral("speedynote_paper_settings.png"));
        ok &= check(dialog.grab().save(png), QStringLiteral("settings picture: ") + png);
    }
    auto canvas = Document::createNew(QStringLiteral("Paper canvas"), Document::Mode::Edgeless);
    canvas->defaultPaperId = QStringLiteral("staves");
    {
        DocumentViewport viewport;
        viewport.resize(900, 500);
        viewport.setDocument(canvas.get());
        viewport.setZoomLevel(0.8);
        viewport.setPanOffset(QPointF(800, 800));   // across four tiles
        const QString png = tempPng(QStringLiteral("speedynote_paper_canvas.png"));
        ok &= check(viewport.grab().save(png), QStringLiteral("endless canvas picture: ") + png);
    }
    return ok;
}

inline bool runAllTests()
{
    QTemporaryDir temp;
    if (!temp.isValid()) return check(false, QStringLiteral("temporary folder"));
    bool ok = true;
    ok &= testInsidePage();
    ok &= testEndlessTiles();
    ok &= testSavedFields();
    ok &= testPdfOps();
    ok &= testPdfExport(temp.path());
    ok &= testUserPaper(temp.path());
    ok &= testClippedPaintsMatch();
    ok &= testTiming();
    ok &= testPictures();
    ok &= testViewportPictures();
    std::printf("%s\n", ok ? "=== paper: ALL PASSED ===" : "=== paper: SOME FAILED ===");
    std::fflush(stdout);
    return ok;
}

} // namespace PaperTests
