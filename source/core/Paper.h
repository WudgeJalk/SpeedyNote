#pragma once

// ============================================================================
// Paper - page designs drawn by code (Step N, stage S5)
// ============================================================================
// Every paper is a short list of rules in a table (Paper.cpp). A position is
// frac x side + mm, so one rule works for every page size and orientation.
// build() turns a paper into lines, boxes, dot lattices and a label path, in
// page pixels (96 per inch). paint() keeps the last builds in a small cache,
// draws only what the clip shows, and drops details too small to see, so a pen
// event costs about what the old grid cost.
//
// Saved on a page as the optional "paper" key. The page's backgroundType keeps
// the paper's fallback (lines or grid with its spacing, else plain), so an
// older SpeedyNote shows ruled or plain paper, and an unknown id draws its
// fallback here too. A paper of your own is BackgroundType::Custom plus the
// optional "customImage" key: a PNG in the note's assets/images folder.
// ============================================================================

#include "Page.h"

#include <QColor>
#include <QImage>
#include <QLineF>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>
#include <QVector>

class QPainter;

namespace Paper {

struct Len { float frac, mm; };            ///< value = frac * side + mm
struct Box { Len x1, y1, x2, y2; };

enum Ink : quint8 { Line, Strong, Accent };   ///< gridColor, a stronger shade, red
enum Text : quint8 { NoText, Weekdays, WeekdaysNotes, Numbers };
enum Group : quint8 { Basic, Writing, Planning, Music };

struct Rule {
    enum Kind : quint8 { Lines, Grid, Dots, HLine, VLine, Frame, Cells, Checks, Labels };
    Kind kind = Lines;
    Box box{};
    float step = 0;       ///< Lines/Grid/Dots/Checks: gap, mm
    quint8 group = 0;     ///< Lines: groups of `group` lines `step` apart...
    float pitch = 0;      ///< ...one group every `pitch` mm,
    float offset = 0;     ///< ...starting `offset` mm into each pitch
    float size = 0;       ///< Dots: diameter; Checks: box side; groups: end bars
    quint8 cols = 0, rows = 0;  ///< Cells/Labels; Lines: rows > 0 = that many lines
    Ink ink = Line;
    Text text = NoText;
    quint8 first = 1;     ///< Numbers: the first number
};

struct PaperDef {
    const char* id;
    Group group;
    const char* name;     ///< English, translated through displayName()
    QVector<Rule> rules;
    Page::BackgroundType fallback;
    float fallbackMm;     ///< spacing of the fallback lines or grid
    bool endless;         ///< can fill an endless canvas (anchored at world 0)
};

/// A paper in page pixels. Pure data: built on any thread.
struct Drawing {
    struct Strokes { Ink ink; float gap; QVector<QLineF> lines; QVector<QRectF> boxes; };
    struct Dots { QRectF area; float step, d; };   ///< dots from area.topLeft every step
    QVector<Strokes> strokes;   ///< gap: smallest spacing, for level of detail (0 = always)
    QVector<Dots> dots;
    QPainterPath labels;        ///< filled with the strong ink
    float labelPx = 0;          ///< smallest label height
};

const QVector<PaperDef>& all();
const PaperDef* find(const QString& id);
QString displayName(const PaperDef& def);
QString groupName(Group group);

/// The fallback type and its spacing in pixels (0 when the fallback is plain).
/// False for an unknown id.
bool fallbackFor(const QString& id, Page::BackgroundType* type, int* spacingPx);

/// endless: a lattice anchored at world 0 over all of rectPx, for tiles.
Drawing build(const QString& id, const QRectF& rectPx, bool endless = false);
/// GUI thread: cached builds. Scale and clip come from the painter.
void paint(QPainter& painter, const QString& id, const QRectF& rectPx,
           const QColor& line, bool endless = false);
/// detail < 1 keeps finer lines and dots (small pictures, e.g. the New note gallery).
void paintDrawing(QPainter& painter, const Drawing& drawing, const QColor& line, qreal detail = 1.0);
/// PDF content operators (points, origin at the bottom left), balanced q/Q, no fonts.
QByteArray pdfOps(const QString& id, const QSizeF& pagePx, const QColor& line, bool invert);

// ===== A paper of your own =====
QString userPapersDir();   ///< <AppData>/papers
void setUserPapersDirForTests(const QString& dir);   ///< empty: back to AppData
/// A PDF (first page, 150 DPI) or a picture, at most 2400 px on its long side,
/// saved as a PNG in userPapersDir(). Returns its path, or empty with *error.
QString importUserPaper(const QString& pdfOrImage, QString* error);
/// Copies a user paper into <bundle>/assets/images. Returns the file name there.
QString adoptUserPaper(const QString& pngPath, const QString& bundlePath, QString* error);
QPixmap userPaperPixmap(const QString& path);   ///< GUI thread, one shared pixmap per file
QImage userPaperImage(const QString& path);     ///< any thread, a small copy for thumbnails

} // namespace Paper
