#include "Paper.h"

#include "DarkModeUtils.h"
#include "Document.h"
#include "../pdf/PdfProvider.h"

#include <QCache>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QImageReader>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QTransform>
#include <cmath>
#include <memory>

namespace Paper {

namespace {

constexpr qreal PXMM = 96.0 / 25.4;   // page pixels per mm
constexpr float MMPX = 25.4f / 96.f;  // mm per page pixel

// ----- Table helpers -----
Len L(float mm) { return {0.f, mm}; }            // from the left or top edge
Len R(float mm) { return {1.f, -mm}; }           // from the right or bottom edge
Len F(float frac, float mm = 0) { return {frac, mm}; }

Rule rule(Rule::Kind kind, const Box& box, Ink ink = Line)
{
    Rule r;
    r.kind = kind;
    r.box = box;
    r.ink = ink;
    return r;
}
Rule lines(const Box& b, float step, Ink ink = Line)
{
    Rule r = rule(Rule::Lines, b, ink);
    r.step = step;
    return r;
}
Rule numbered(const Box& b, quint8 rows, quint8 first)
{
    Rule r = rule(Rule::Lines, b);
    r.rows = rows;
    r.text = Numbers;
    r.first = first;
    return r;
}
// Groups of `count` lines: a staff (5) or a tablature (6). bars: height of
// the bar at both ends, 0 for none.
Rule staff(const Box& b, quint8 count, float step, float pitch, float offset, float bars)
{
    Rule r = lines(b, step, Strong);
    r.group = count;
    r.pitch = pitch;
    r.offset = offset;
    r.size = bars;
    return r;
}
Rule grid(const Box& b, float step) { Rule r = rule(Rule::Grid, b); r.step = step; return r; }
Rule dots(const Box& b, float step, float d)
{
    Rule r = rule(Rule::Dots, b);
    r.step = step;
    r.size = d;
    return r;
}
Rule hline(Len y, Len x1, Len x2, Ink ink = Line) { return rule(Rule::HLine, {x1, y, x2, y}, ink); }
Rule vline(Len x, Len y1, Len y2, Ink ink = Line) { return rule(Rule::VLine, {x, y1, x, y2}, ink); }
Rule frame(const Box& b, Ink ink = Line) { return rule(Rule::Frame, b, ink); }
Rule cells(const Box& b, quint8 cols, quint8 rows, Text text = NoText)
{
    Rule r = rule(Rule::Cells, b);
    r.cols = cols;
    r.rows = rows;
    r.text = text;
    return r;
}
Rule checks(const Box& b, float step, float size)
{
    Rule r = rule(Rule::Checks, b);
    r.step = step;
    r.size = size;
    return r;
}
Rule labels(const Box& b, quint8 cols, Text text)
{
    Rule r = rule(Rule::Labels, b);
    r.cols = cols;
    r.text = text;
    return r;
}

// ----- Build helpers -----
qreal at(const Len& l, qreal side) { return l.frac * side + l.mm * PXMM; }

// Multiples of step counted from origin. A page takes those in (lo, hi] or
// (lo, hi); an endless tile those in [lo, hi), so each line of the plane is
// drawn by exactly one tile.
QVector<qreal> lattice(qreal origin, qreal lo, qreal hi, qreal step, bool endless, bool includeHi)
{
    QVector<qreal> out;
    if (step < 0.5) return out;
    qreal k = endless ? std::ceil((lo - origin) / step - 1e-6)
                      : std::floor((lo - origin) / step + 1e-6) + 1;
    for (int guard = 0; guard < 5000; ++guard, ++k) {
        const qreal v = origin + k * step;
        if (endless ? v >= hi - 1e-6 : (includeHi ? v > hi + 0.01 : v >= hi - 0.01)) break;
        out << v;
    }
    return out;
}

QString labelText(Text text, int i, int first, bool longNames)
{
    switch (text) {
    case Weekdays:
    case WeekdaysNotes:
        if (i < 7) {
            const QLocale locale;
            const int day = (static_cast<int>(locale.firstDayOfWeek()) - 1 + i) % 7 + 1;
            return locale.dayName(day, longNames ? QLocale::LongFormat : QLocale::ShortFormat);
        }
        return text == WeekdaysNotes && i == 7
            ? QCoreApplication::translate("Paper", "Notes") : QString();
    case Numbers:
        return first + i <= 31 ? QString::number(first + i) : QString();
    case NoText:
        break;
    }
    return {};
}

// Text as a filled path (exports as vector shapes, no font). h: height of the
// capitals; the text is centred or left-aligned in area, shrunk to fit it.
void addLabel(Drawing& d, const QString& text, const QRectF& area, qreal h, bool center)
{
    if (text.isEmpty() || h <= 0 || area.width() <= 0) return;
    QFont font;
    font.setPixelSize(100);   // outlines at a large size, then scaled: no hinting
    QPainterPath p;
    p.addText(0, 0, font, text);
    qreal k = h / 72.0;       // capitals and digits are about 72 px tall at 100 px
    QRectF br = QTransform::fromScale(k, k).mapRect(p.boundingRect());
    if (br.width() > area.width() * 0.94) {
        const qreal fit = area.width() * 0.94 / br.width();
        k *= fit;
        h *= fit;
    }
    p = QTransform::fromScale(k, k).map(p);
    br = p.boundingRect();
    const qreal dx = center ? area.center().x() - br.center().x() : area.left() - br.left();
    p.translate(dx, area.center().y() + h / 2);
    d.labels.addPath(p);
    d.labelPx = d.labelPx > 0 ? qMin<float>(d.labelPx, h) : h;
}

QColor strongInk(const QColor& line)
{
    // Darker on light paper; a dark grid colour belongs to dark paper.
    return line.lightness() >= 96 ? line.darker(160) : line.lighter(220);
}
QColor accentInk() { return QColor(0xE0, 0x66, 0x66); }
// Dots are small: a little more contrast than the lines, on light or dark paper.
QColor dotInk(const QColor& line) { return line.lightness() >= 96 ? line.darker(120) : line.lighter(140); }

// ----- User paper caches -----
QHash<QString, QPixmap>& pixmapCache() { static QHash<QString, QPixmap> c; return c; }
QSet<QString>& missingPixmaps() { static QSet<QString> s; return s; }
QMutex& imageMutex() { static QMutex m; return m; }
QHash<QString, QImage>& imageCache() { static QHash<QString, QImage> c; return c; }

void setError(QString* error, const QString& text) { if (error) *error = text; }

} // namespace

// ============================================================================
// The papers
// ============================================================================

const QVector<PaperDef>& all()
{
    static const QVector<PaperDef> papers = [] {
        using BT = Page::BackgroundType;
        // Line gaps are whole pixels where the fallback is ruled, so an older
        // version and OCR snapping see the same lines: 24 px = 1/4 in,
        // 27 px = 9/32 in (college), 33 px = 11/32 in (wide).
        const float s19 = 19 * MMPX, s23 = 23 * MMPX, s24 = 24 * MMPX,
                    s27 = 27 * MMPX, s33 = 33 * MMPX, s34 = 34 * MMPX;
        const Box page{L(0), L(0), R(0), R(0)};
        const float top = 4 * s27;   // 28.6 mm: first ruled line one gap below it
        auto column = [&](Len x1, Len x2) { return lines({x1, L(top), x2, R(15)}, s27); };
        const float todoTop = 3 * s34;   // 27 mm
        const Box music{L(15), L(20), R(15), R(15)};
        const float acTop = 2 * s23, acHead = 4 * s23;   // accounting table, header

        QVector<PaperDef> v;
        // ----- Basic -----
        v.push_back({"plain", Basic, QT_TRANSLATE_NOOP("Paper", "Plain"), {}, BT::None, 0, true});
        v.push_back({"dotted", Basic, QT_TRANSLATE_NOOP("Paper", "Dotted"),
                     {dots(page, 5, 0.5f)}, BT::None, 0, true});
        v.push_back({"lined-narrow", Basic, QT_TRANSLATE_NOOP("Paper", "Lined, narrow"),
                     {lines({L(0), L(3 * s24), R(0), R(0)}, s24)}, BT::Lines, s24, true});
        v.push_back({"lined-wide", Basic, QT_TRANSLATE_NOOP("Paper", "Lined, wide"),
                     {lines({L(0), L(3 * s33), R(0), R(0)}, s33)}, BT::Lines, s33, true});
        v.push_back({"squared", Basic, QT_TRANSLATE_NOOP("Paper", "Squared"),
                     {grid(page, s19)}, BT::Grid, s19, true});
        // ----- Writing paper -----
        v.push_back({"cornell", Writing, QT_TRANSLATE_NOOP("Paper", "Cornell"),
                     {hline(L(top), L(0), R(0), Strong),
                      lines({L(0), L(top), R(0), R(55)}, s27),
                      vline(L(60), L(top), R(55), Strong),
                      hline(R(55), L(0), R(0), Strong)},
                     BT::Lines, s27, false});
        v.push_back({"legal", Writing, QT_TRANSLATE_NOOP("Paper", "Legal"),
                     {lines({L(0), L(4 * s33), R(0), R(0)}, s33),
                      vline(L(31.75f), L(0), R(0), Accent),
                      vline(L(33.25f), L(0), R(0), Accent)},
                     BT::Lines, s33, false});
        v.push_back({"column-1", Writing, QT_TRANSLATE_NOOP("Paper", "One column"),
                     {column(L(20), R(20))}, BT::Lines, s27, false});
        v.push_back({"column-1-mixed", Writing, QT_TRANSLATE_NOOP("Paper", "One column, mixed"),
                     {frame({L(20), L(top), R(20), F(0.45f)}),
                      lines({L(20), F(0.45f), R(20), R(15)}, s27)},
                     BT::Lines, s27, false});
        v.push_back({"columns-2", Writing, QT_TRANSLATE_NOOP("Paper", "Two columns"),
                     {column(L(20), F(0.5f, -4)), column(F(0.5f, 4), R(20))}, BT::Lines, s27, false});
        v.push_back({"columns-2-left", Writing, QT_TRANSLATE_NOOP("Paper", "Two columns, left"),
                     {column(L(20), F(1 / 3.f, -4)), column(F(1 / 3.f, 4), R(20))}, BT::Lines, s27, false});
        v.push_back({"columns-2-right", Writing, QT_TRANSLATE_NOOP("Paper", "Two columns, right"),
                     {column(L(20), F(2 / 3.f, -4)), column(F(2 / 3.f, 4), R(20))}, BT::Lines, s27, false});
        v.push_back({"columns-3", Writing, QT_TRANSLATE_NOOP("Paper", "Three columns"),
                     {column(L(20), F(1 / 3.f, -4)), column(F(1 / 3.f, 4), F(2 / 3.f, -4)),
                      column(F(2 / 3.f, 4), R(20))},
                     BT::Lines, s27, false});
        // ----- Planning -----
        v.push_back({"accounting", Planning, QT_TRANSLATE_NOOP("Paper", "Accounting"),
                     {frame({L(10), L(acTop), R(10), R(10)}),
                      hline(L(acHead - 0.8f), L(10), R(10), Strong),
                      hline(L(acHead), L(10), R(10), Strong),
                      lines({L(10), L(acHead), R(10), R(10)}, s23),
                      vline(L(32), L(acTop), R(10)),
                      vline(R(82), L(acTop), R(10)),
                      vline(R(58), L(acTop), R(10)),
                      vline(R(34), L(acTop), R(10))},
                     BT::Lines, s23, false});
        for (quint8 weeks : {quint8(5), quint8(6)}) {
            v.push_back({weeks == 5 ? "month-grid-5" : "month-grid-6", Planning,
                         weeks == 5 ? QT_TRANSLATE_NOOP("Paper", "Month, 5 weeks")
                                    : QT_TRANSLATE_NOOP("Paper", "Month, 6 weeks"),
                         {frame({L(10), L(30), R(10), L(38)}),
                          labels({L(10), L(30), R(10), L(38)}, 7, Weekdays),
                          cells({L(10), L(38), R(10), R(10)}, 7, weeks)},
                         BT::None, 0, false});
        }
        v.push_back({"month-list", Planning, QT_TRANSLATE_NOOP("Paper", "Month, list"),
                     {hline(L(25), L(15), R(15), Strong),
                      numbered({L(15), L(25), F(0.5f, -4), R(15)}, 16, 1),
                      numbered({F(0.5f, 4), L(25), R(15), R(15)}, 16, 17)},
                     BT::None, 0, false});
        v.push_back({"week-boxes", Planning, QT_TRANSLATE_NOOP("Paper", "Week, boxes"),
                     {cells({L(10), L(25), R(10), R(10)}, 2, 4, WeekdaysNotes)}, BT::None, 0, false});
        v.push_back({"week-rows", Planning, QT_TRANSLATE_NOOP("Paper", "Week, rows"),
                     {cells({L(10), L(25), R(10), R(10)}, 1, 7, Weekdays)}, BT::None, 0, false});
        v.push_back({"week-columns", Planning, QT_TRANSLATE_NOOP("Paper", "Week, columns"),
                     {frame({L(10), L(25), R(10), L(33)}),
                      labels({L(10), L(25), R(10), L(33)}, 7, Weekdays),
                      cells({L(10), L(33), R(10), R(10)}, 7, 1)},
                     BT::None, 0, false});
        v.push_back({"todo", Planning, QT_TRANSLATE_NOOP("Paper", "To-do list"),
                     {hline(L(todoTop), L(15), R(15), Strong),
                      lines({L(15), L(todoTop), R(15), R(15)}, s34),
                      checks({L(15), L(todoTop), R(15), R(15)}, s34, 4.5f)},
                     BT::Lines, s34, false});
        v.push_back({"todo-2", Planning, QT_TRANSLATE_NOOP("Paper", "To-do list, two columns"),
                     {hline(L(todoTop), L(15), R(15), Strong),
                      lines({L(15), L(todoTop), F(0.5f, -4), R(15)}, s34),
                      checks({L(15), L(todoTop), F(0.5f, -4), R(15)}, s34, 4.5f),
                      lines({F(0.5f, 4), L(todoTop), R(15), R(15)}, s34),
                      checks({F(0.5f, 4), L(todoTop), R(15), R(15)}, s34, 4.5f)},
                     BT::Lines, s34, false});
        // ----- Music -----
        v.push_back({"staves", Music, QT_TRANSLATE_NOOP("Paper", "Music staves"),
                     {staff(music, 5, 2, 16, 4, 8)}, BT::None, 0, true});
        v.push_back({"tablature", Music, QT_TRANSLATE_NOOP("Paper", "Guitar tablature"),
                     {staff(music, 6, 2.5f, 22, 4.75f, 12.5f)}, BT::None, 0, false});
        // A staff, then a tablature 7 mm below it; one bar joins each system.
        v.push_back({"guitar-score", Music, QT_TRANSLATE_NOOP("Paper", "Guitar score"),
                     {staff(music, 5, 2, 42, 0, 8 + 7 + 12.5f),
                      staff(music, 6, 2.5f, 42, 8 + 7, 0)},
                     BT::None, 0, false});
        return v;
    }();
    return papers;
}

const PaperDef* find(const QString& id)
{
    if (id.isEmpty()) return nullptr;
    for (const PaperDef& def : all()) {
        if (id == QLatin1String(def.id)) return &def;
    }
    return nullptr;
}

QString displayName(const PaperDef& def)
{
    return QCoreApplication::translate("Paper", def.name);
}

QString groupName(Group group)
{
    switch (group) {
    case Basic: return QCoreApplication::translate("Paper", "Basic");
    case Writing: return QCoreApplication::translate("Paper", "Writing paper");
    case Planning: return QCoreApplication::translate("Paper", "Planning");
    case Music: return QCoreApplication::translate("Paper", "Music");
    }
    return {};
}

bool fallbackFor(const QString& id, Page::BackgroundType* type, int* spacingPx)
{
    const PaperDef* def = find(id);
    if (!def) return false;
    if (type) *type = def->fallback;
    if (spacingPx) *spacingPx = def->fallbackMm > 0 ? qRound(def->fallbackMm * PXMM) : 0;
    return true;
}

// ============================================================================
// Build
// ============================================================================

Drawing build(const QString& id, const QRectF& rect, bool endless)
{
    Drawing d;
    const PaperDef* def = find(id);
    if (!def || rect.isEmpty()) return d;

    const qreal ox = endless ? 0 : rect.left();
    const qreal oy = endless ? 0 : rect.top();
    for (const Rule& r : def->rules) {
        if (endless && r.kind != Rule::Lines && r.kind != Rule::Grid && r.kind != Rule::Dots) continue;
        qreal x1 = rect.left(), y1 = rect.top(), x2 = rect.right(), y2 = rect.bottom();
        if (!endless) {
            x1 = rect.left() + at(r.box.x1, rect.width());
            x2 = rect.left() + at(r.box.x2, rect.width());
            y1 = rect.top() + at(r.box.y1, rect.height());
            y2 = rect.top() + at(r.box.y2, rect.height());
        }
        const qreal step = r.step * PXMM;
        Drawing::Strokes st{r.ink, 0.f, {}, {}};

        switch (r.kind) {
        case Rule::Lines:
            if (r.group > 1) {
                const qreal pitch = r.pitch * PXMM, offset = r.offset * PXMM;
                const qreal height = qMax<qreal>((r.group - 1) * step, r.size * PXMM);
                if (pitch < 1) break;
                st.gap = float(step);
                if (!endless) {
                    for (qreal top = y1 + offset; top + height <= y2 + 0.01; top += pitch) {
                        for (int i = 0; i < r.group; ++i) {
                            st.lines << QLineF(x1, top + i * step, x2, top + i * step);
                        }
                        if (r.size > 0) {
                            st.lines << QLineF(x1, top, x1, top + r.size * PXMM)
                                     << QLineF(x2, top, x2, top + r.size * PXMM);
                        }
                    }
                } else {
                    for (qreal k = std::floor((y1 - offset - height) / pitch);; ++k) {
                        const qreal top = offset + k * pitch;
                        if (top >= y2) break;
                        for (int i = 0; i < r.group; ++i) {
                            const qreal y = top + i * step;
                            if (y >= y1 - 1e-6 && y < y2 - 1e-6) st.lines << QLineF(x1, y, x2, y);
                        }
                    }
                }
            } else if (r.rows > 0) {
                const qreal gap = (y2 - y1) / r.rows;
                st.gap = float(gap);
                for (int k = 1; k <= r.rows; ++k) {
                    const qreal y = y1 + k * gap;
                    st.lines << QLineF(x1, y, x2, y);
                    if (r.text != NoText) {
                        addLabel(d, labelText(r.text, k - 1, r.first, false),
                                 QRectF(x1 + 1.5 * PXMM, y - gap, x2 - x1, gap),
                                 qMin(gap * 0.42, 3.2 * PXMM), false);
                    }
                }
            } else {
                st.gap = float(step);
                for (qreal y : lattice(oy, y1, y2, step, endless, true)) st.lines << QLineF(x1, y, x2, y);
            }
            break;
        case Rule::Grid:
            st.gap = float(step);
            for (qreal x : lattice(ox, x1, x2, step, endless, false)) st.lines << QLineF(x, y1, x, y2);
            for (qreal y : lattice(oy, y1, y2, step, endless, false)) st.lines << QLineF(x1, y, x2, y);
            break;
        case Rule::Dots: {
            const QVector<qreal> xs = lattice(ox, x1, x2, step, endless, false);
            const QVector<qreal> ys = lattice(oy, y1, y2, step, endless, false);
            if (!xs.isEmpty() && !ys.isEmpty()) {
                d.dots.push_back({QRectF(QPointF(xs.first(), ys.first()), QPointF(xs.last(), ys.last())),
                                  float(step), float(r.size * PXMM)});
            }
            break;
        }
        case Rule::HLine:
            st.lines << QLineF(x1, y1, x2, y1);
            break;
        case Rule::VLine:
            st.lines << QLineF(x1, y1, x1, y2);
            break;
        case Rule::Frame:
            st.boxes << QRectF(QPointF(x1, y1), QPointF(x2, y2));
            break;
        case Rule::Cells: {
            if (r.cols == 0 || r.rows == 0) break;
            st.boxes << QRectF(QPointF(x1, y1), QPointF(x2, y2));
            const qreal cw = (x2 - x1) / r.cols, ch = (y2 - y1) / r.rows;
            for (int c = 1; c < r.cols; ++c) st.lines << QLineF(x1 + c * cw, y1, x1 + c * cw, y2);
            for (int k = 1; k < r.rows; ++k) st.lines << QLineF(x1, y1 + k * ch, x2, y1 + k * ch);
            if (r.text != NoText) {
                const qreal h = qMin(3.0 * PXMM, ch * 0.25);
                for (int i = 0; i < r.cols * r.rows; ++i) {
                    const qreal cx = x1 + (i % r.cols) * cw, cy = y1 + (i / r.cols) * ch;
                    addLabel(d, labelText(r.text, i, r.first, true),
                             QRectF(cx + 1.5 * PXMM, cy + 1.5 * PXMM, cw - 3 * PXMM, h), h, false);
                }
            }
            break;
        }
        case Rule::Checks: {
            st.gap = float(step);
            const qreal side = r.size * PXMM;
            for (qreal y : lattice(oy, y1, y2, step, false, true)) {
                if (y - step < y1 - 0.01) continue;   // only rows wholly in the box
                st.boxes << QRectF(x1, y - step / 2 - side / 2, side, side);
            }
            break;
        }
        case Rule::Labels: {
            if (r.cols == 0) break;
            const qreal cw = (x2 - x1) / r.cols;
            for (int i = 0; i < r.cols; ++i) {
                addLabel(d, labelText(r.text, i, r.first, false),
                         QRectF(x1 + i * cw, y1, cw, y2 - y1), qMin((y2 - y1) * 0.45, 3.2 * PXMM), true);
            }
            break;
        }
        }
        if (!st.lines.isEmpty() || !st.boxes.isEmpty()) d.strokes << st;
    }
    return d;
}

// ============================================================================
// Paint
// ============================================================================

void paintDrawing(QPainter& p, const Drawing& d, const QColor& line, qreal detail)
{
    const QTransform& t = p.worldTransform();
    const qreal s = std::sqrt(std::abs(t.m11() * t.m22() - t.m12() * t.m21()));
    if (s <= 0) return;

    // While writing, the clip is the few pixels the pen touched: draw only those.
    QRectF vis;
    if (p.hasClipping()) {
        vis = p.clipBoundingRect();
        if (vis.isEmpty()) return;
        vis.adjust(-2 / s, -2 / s, 2 / s, 2 / s);
    }
    const bool cull = !vis.isNull();
    const QColor inks[3] = {line, strongInk(line), accentInk()};

    p.save();
    p.setBrush(Qt::NoBrush);
    QVector<QLineF> shown;
    for (const Drawing::Strokes& st : d.strokes) {
        if (st.gap > 0 && st.gap * s < 2.0 * detail) continue;   // lines closer than 2 px: a grey wash
        p.setPen(QPen(inks[st.ink], 1.0 / s));           // one screen pixel, as the old grid
        if (!cull) {
            p.drawLines(st.lines);
            p.drawRects(st.boxes);
            continue;
        }
        shown.clear();
        for (const QLineF& l : st.lines) {
            if (QRectF(l.p1(), l.p2()).normalized().adjusted(-1, -1, 1, 1).intersects(vis)) shown << l;
        }
        if (!shown.isEmpty()) p.drawLines(shown);
        for (const QRectF& b : st.boxes) {
            if (b.adjusted(-1, -1, 1, 1).intersects(vis)) p.drawRect(b);
        }
    }

    QVector<QPointF> points;
    for (const Drawing::Dots& dt : d.dots) {
        if (dt.step * s < 3.0 * detail) continue;
        const qreal size = qMax<qreal>(dt.d, 1.2 / s);
        // Below 3 screen pixels a square looks the same and is cheaper.
        p.setPen(QPen(dotInk(line), size, Qt::SolidLine,
                      size * s < 3 ? Qt::SquareCap : Qt::RoundCap));
        int i0 = 0, j0 = 0;
        int i1 = qRound(dt.area.width() / dt.step), j1 = qRound(dt.area.height() / dt.step);
        if (cull) {
            i0 = qMax(i0, int(std::ceil((vis.left() - dt.area.left()) / dt.step)));
            i1 = qMin(i1, int(std::floor((vis.right() - dt.area.left()) / dt.step)));
            j0 = qMax(j0, int(std::ceil((vis.top() - dt.area.top()) / dt.step)));
            j1 = qMin(j1, int(std::floor((vis.bottom() - dt.area.top()) / dt.step)));
        }
        if (i1 < i0 || j1 < j0) continue;
        points.clear();
        points.reserve((i1 - i0 + 1) * (j1 - j0 + 1));
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                points << QPointF(dt.area.left() + i * dt.step, dt.area.top() + j * dt.step);
            }
        }
        p.drawPoints(points.constData(), points.size());
    }

    if (!d.labels.isEmpty() && d.labelPx * s >= 4.0 * detail
        && (!cull || d.labels.boundingRect().intersects(vis))) {
        p.fillPath(d.labels, inks[Strong]);
    }
    p.restore();
}

void paint(QPainter& p, const QString& id, const QRectF& rect, const QColor& line, bool endless)
{
    QCoreApplication* app = QCoreApplication::instance();
    if (!app || QThread::currentThread() != app->thread()) {
        paintDrawing(p, build(id, rect, endless), line);   // worker: no shared cache
        return;
    }
    static QCache<QString, Drawing> cache(32);
    const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(id).arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()).arg(int(endless));
    Drawing* d = cache.object(key);
    if (!d) {
        d = new Drawing(build(id, rect, endless));
        cache.insert(key, d);
    }
    paintDrawing(p, *d, line);
}

// ============================================================================
// PDF
// ============================================================================

QByteArray pdfOps(const QString& id, const QSizeF& pagePx, const QColor& line, bool invert)
{
    const Drawing d = build(id, QRectF(QPointF(0, 0), pagePx));
    const qreal k = 72.0 / 96.0;
    const qreal H = pagePx.height() * k;
    auto n = [](qreal v) { return QByteArray::number(v, 'f', 2); };
    auto pt = [&](qreal x, qreal y) { return n(x * k) + ' ' + n(H - y * k); };
    auto rgb = [&](const QColor& c, const char* op) {
        return n(c.redF()) + ' ' + n(c.greenF()) + ' ' + n(c.blueF()) + ' ' + op + '\n';
    };
    const QColor accent = invert ? DarkModeUtils::invertColorLightness(accentInk()) : accentInk();
    const QColor inks[3] = {line, strongInk(line), accent};

    QByteArray out("q\n0.5 w\n");
    for (const Drawing::Strokes& st : d.strokes) {
        out += rgb(inks[st.ink], "RG");
        for (const QLineF& l : st.lines) {
            out += pt(l.x1(), l.y1()) + " m " + pt(l.x2(), l.y2()) + " l\n";
        }
        for (const QRectF& b : st.boxes) {
            out += pt(b.left(), b.bottom()) + ' ' + n(b.width() * k) + ' ' + n(b.height() * k) + " re\n";
        }
        out += "S\n";
    }
    if (!d.dots.isEmpty()) {
        out += rgb(dotInk(line), "rg");
        for (const Drawing::Dots& dt : d.dots) {
            const int ni = qRound(dt.area.width() / dt.step), nj = qRound(dt.area.height() / dt.step);
            const qreal half = dt.d / 2;
            for (int j = 0; j <= nj; ++j) {
                for (int i = 0; i <= ni; ++i) {
                    const qreal x = dt.area.left() + i * dt.step, y = dt.area.top() + j * dt.step;
                    out += pt(x - half, y + half) + ' ' + n(dt.d * k) + ' ' + n(dt.d * k) + " re\n";
                }
            }
        }
        out += "f\n";
    }
    if (!d.labels.isEmpty()) {
        out += rgb(inks[Strong], "rg");
        for (int i = 0; i < d.labels.elementCount(); ++i) {
            const QPainterPath::Element e = d.labels.elementAt(i);
            if (e.type == QPainterPath::MoveToElement) {
                out += pt(e.x, e.y) + " m\n";
            } else if (e.type == QPainterPath::LineToElement) {
                out += pt(e.x, e.y) + " l\n";
            } else if (e.type == QPainterPath::CurveToElement && i + 2 < d.labels.elementCount()) {
                const QPainterPath::Element c2 = d.labels.elementAt(i + 1);
                const QPainterPath::Element end = d.labels.elementAt(i + 2);
                out += pt(e.x, e.y) + ' ' + pt(c2.x, c2.y) + ' ' + pt(end.x, end.y) + " c\n";
                i += 2;
            }
        }
        out += "f*\n";   // QPainterPath fills odd-even
    }
    out += "Q\n";
    return out;
}

// ============================================================================
// A paper of your own
// ============================================================================

static QString& papersDirOverride() { static QString dir; return dir; }

void setUserPapersDirForTests(const QString& dir) { papersDirOverride() = dir; }

QString userPapersDir()
{
    if (!papersDirOverride().isEmpty()) return papersDirOverride();
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/papers");
}

QString importUserPaper(const QString& source, QString* error)
{
    QImage image;
    // A PDF by its bytes, not its suffix: Android's picker gives content:// URIs.
    QFile in(source);
    const bool isPdf = in.open(QIODevice::ReadOnly) && in.peek(1024).contains("%PDF");
    QTemporaryFile local(QDir::tempPath() + QStringLiteral("/sn_paper_XXXXXX.pdf"));
    QString pdfPath = source;
    if (isPdf && !QFileInfo(source).isNativePath()) {
        // MuPDF opens a file path: copy what Qt can read (a content:// URI) to one.
        if (!local.open() || local.write(in.readAll()) < 0 || !local.flush()) {
            setError(error, QCoreApplication::translate("Paper", "This PDF cannot be opened."));
            return {};
        }
        pdfPath = local.fileName();
    }
    in.close();
    if (isPdf) {
        std::unique_ptr<PdfProvider> pdf;
        if (PdfProvider::isAvailable()) pdf = PdfProvider::create(pdfPath);
        if (!pdf || !pdf->isValid() || pdf->isLocked() || pdf->pageCount() < 1) {
            setError(error, QCoreApplication::translate("Paper", "This PDF cannot be opened."));
            return {};
        }
        const QSizeF pt = pdf->pageSize(0);
        const qreal longPt = qMax(pt.width(), pt.height());
        image = pdf->renderPageToImage(0, longPt > 0 ? qMin(150.0, 2400.0 * 72.0 / longPt) : 150.0);
    } else {
        QImageReader reader(source);
        reader.setAutoTransform(true);
        image = reader.read();
        if (qMax(image.width(), image.height()) > 2400) {
            image = image.scaled(2400, 2400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }
    if (image.isNull()) {
        setError(error, QCoreApplication::translate(
            "Paper", "This file is not a picture or a PDF that SpeedyNote can read."));
        return {};
    }
    if (!image.hasAlphaChannel()) image = image.convertToFormat(QImage::Format_RGB32);

    const QString dir = userPapersDir();
    const QString base = QFileInfo(source).completeBaseName();
    QString path = dir + QLatin1Char('/') + base + QStringLiteral(".png");
    for (int n = 2; QFile::exists(path) && n < 1000; ++n) {
        path = QStringLiteral("%1/%2-%3.png").arg(dir, base).arg(n);
    }
    if (!QDir().mkpath(dir) || !image.save(path, "PNG")) {
        setError(error, QCoreApplication::translate("Paper", "Could not write the paper into %1.")
                            .arg(QDir::toNativeSeparators(dir)));
        return {};
    }
    return path;
}

QString adoptUserPaper(const QString& pngPath, const QString& bundlePath, QString* error)
{
    if (bundlePath.isEmpty()) {
        setError(error, QCoreApplication::translate("Paper", "Save this note once, then add your own paper."));
        return {};
    }
    QFile source(pngPath);
    if (!source.open(QIODevice::ReadOnly)) {
        setError(error, QCoreApplication::translate("Paper", "Could not read %1.")
                            .arg(QDir::toNativeSeparators(pngPath)));
        return {};
    }
    const QByteArray data = source.readAll();
    const QString dir = bundlePath + QStringLiteral("/assets/images");
    if (!QDir().mkpath(dir)) {
        setError(error, QCoreApplication::translate("Paper", "Could not write the paper into %1.")
                            .arg(QDir::toNativeSeparators(dir)));
        return {};
    }
    const QString base = QStringLiteral("paper-") + QFileInfo(pngPath).completeBaseName();
    QString name = base + QStringLiteral(".png");
    for (int n = 2; QFile::exists(dir + QLatin1Char('/') + name) && n < 1000; ++n) {
        QFile existing(dir + QLatin1Char('/') + name);
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == data) return name;   // already here
        name = QStringLiteral("%1-%2.png").arg(base).arg(n);
    }
    const QString target = dir + QLatin1Char('/') + name;
    if (!Document::commitFile(target, data, error)) return {};
    missingPixmaps().remove(target);
    return name;
}

QPixmap userPaperPixmap(const QString& path)
{
    // One pixmap per file for the whole app: every page with this paper shares it.
    // shortcut: kept until the app quits (a few MB per paper used), add eviction if users collect many.
    QHash<QString, QPixmap>& cache = pixmapCache();
    const auto it = cache.constFind(path);
    if (it != cache.constEnd()) return it.value();
    if (missingPixmaps().contains(path)) return {};   // a paint must not stat the disk each time
    const QPixmap pixmap(path);
    if (pixmap.isNull()) {
        missingPixmaps().insert(path);
        return {};
    }
    cache.insert(path, pixmap);
    return pixmap;
}

QImage userPaperImage(const QString& path)
{
    QMutexLocker lock(&imageMutex());
    QHash<QString, QImage>& cache = imageCache();
    const auto it = cache.constFind(path);
    if (it != cache.constEnd()) return it.value();
    QImage image(path);
    if (!image.isNull() && qMax(image.width(), image.height()) > 600) {
        image = image.scaled(600, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    cache.insert(path, image);   // a missing file stays null: no retry per thumbnail
    return image;
}

} // namespace Paper
