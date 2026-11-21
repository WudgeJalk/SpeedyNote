#include "StrokeStyleGeometry.h"

#include <QLineF>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

QPointF lerp(const QPointF& a, const QPointF& b, qreal t) { return a + (b - a) * t; }
qreal lerp(qreal a, qreal b, qreal t) { return a + (b - a) * t; }

void addDisc(StrokePolygonResult& r, const QPointF& c, qreal radius)
{
    r.discCenters.append(c);
    r.discRadii.append(radius);
}

/// Pattern position of each smoothed point, measured on the stored points.
/// Smoothed point k >= 1 lies in stored segment i = (k-1)/sub, at step
/// s = (k-1)%sub + 1 of sub; without smoothing k is the stored index.
QVector<qreal> patternPositions(const VectorStroke& stroke, int n)
{
    const QVector<StrokePoint>& sp = stroke.points;
    const int m = static_cast<int>(sp.size());
    QVector<qreal> c(m);
    c[0] = 0.0;
    for (int i = 1; i < m; ++i) {
        c[i] = c[i - 1] + QLineF(sp[i - 1].pos, sp[i].pos).length();
    }
    QVector<qreal> pos(n);
    if (n == m) {
        for (int k = 0; k < n; ++k) {
            pos[k] = stroke.dashOffset + c[k];
        }
        return pos;
    }
    const int sub = (n - 1) / (m - 1);
    pos[0] = stroke.dashOffset;
    for (int k = 1; k < n; ++k) {
        const int i = qMin((k - 1) / sub, m - 2);
        const int s = (k - 1) % sub + 1;
        pos[k] = stroke.dashOffset + c[i] + (c[i + 1] - c[i]) * s / sub;
    }
    return pos;
}

void addDashes(const QVector<qreal>& pos, const QVector<StrokePoint>& pts,
               const QVector<QPointF>& left, const QVector<QPointF>& right,
               const QVector<qreal>& hw, qreal dash, qreal period,
               StrokePolygonResult& r)
{
    const int n = static_cast<int>(pts.size());
    QPolygonF l;
    QPolygonF rt;
    const auto finish = [&](const QPointF& c, qreal radius) {
        QPolygonF piece;
        piece.reserve(l.size() + rt.size());
        piece << l;
        for (int i = rt.size() - 1; i >= 0; --i) {
            piece << rt[i];
        }
        r.pieces.append(piece);
        addDisc(r, c, radius);
    };

    // Boundaries are numbered: even = a dash starts at j*P, odd = it ends at
    // j*P + D. Walking the numbers (not the phase) keeps rounding from ever
    // seeing the same boundary twice.
    const qint64 j = static_cast<qint64>(std::floor(pos[0] / period));
    bool on = (pos[0] - j * period) < dash;
    qint64 next = on ? 2 * j + 1 : 2 * j + 2;
    if (on) {
        l << left[0];
        rt << right[0];
        addDisc(r, pts[0].pos, hw[0]);
    }
    for (int k = 0; k + 1 < n; ++k) {
        const qreal a = pos[k];
        const qreal b = pos[k + 1];
        for (;;) {
            const qreal v = std::floor(next / 2.0) * period + ((next & 1) ? dash : 0.0);
            if (v >= b) {
                break;
            }
            const qreal t = (b > a) ? qBound(0.0, (v - a) / (b - a), 1.0) : 0.0;
            const QPointF c = lerp(pts[k].pos, pts[k + 1].pos, t);
            const qreal radius = lerp(hw[k], hw[k + 1], t);
            if (next & 1) {
                l << lerp(left[k], left[k + 1], t);
                rt << lerp(right[k], right[k + 1], t);
                finish(c, radius);
                on = false;
            } else {
                l.clear();
                rt.clear();
                l << lerp(left[k], left[k + 1], t);
                rt << lerp(right[k], right[k + 1], t);
                addDisc(r, c, radius);
                on = true;
            }
            ++next;
        }
        if (on) {
            l << left[k + 1];
            rt << right[k + 1];
        }
    }
    if (on) {
        finish(pts[n - 1].pos, hw[n - 1]);
    }
}

void addDots(const QVector<qreal>& pos, const QVector<StrokePoint>& pts,
             const QVector<qreal>& hw, qreal spacing, StrokePolygonResult& r)
{
    const int n = static_cast<int>(pts.size());
    qint64 m = static_cast<qint64>(std::ceil(pos[0] / spacing));
    for (int k = 0; k + 1 < n; ++k) {
        const qreal a = pos[k];
        const qreal b = pos[k + 1];
        for (qreal v = m * spacing; v <= b; v = (++m) * spacing) {
            const qreal t = (b > a) ? qBound(0.0, (v - a) / (b - a), 1.0) : 0.0;
            addDisc(r, lerp(pts[k].pos, pts[k + 1].pos, t), lerp(hw[k], hw[k + 1], t));
        }
    }
}

/// Make a polygon wind the way the stroke ribbons do, so the PDF's single
/// non-zero fill unites it with them instead of cutting a hole.
void orientLikeRibbon(QPolygonF& poly)
{
    qreal area = 0.0;
    for (int i = 0; i < poly.size(); ++i) {
        const QPointF& p = poly[i];
        const QPointF& q = poly[(i + 1) % poly.size()];
        area += p.x() * q.y() - q.x() * p.y();
    }
    if (area > 0.0) {
        std::reverse(poly.begin(), poly.end());
    }
}

/// One arrow head. Its direction runs from the tip back to the point
/// arrowLength earlier along the stored points.
void addArrow(const VectorStroke& s, bool atEnd, quint8 kind, StrokePolygonResult& r)
{
    const QVector<StrokePoint>& p = s.points;
    const int m = static_cast<int>(p.size());
    if (m < 2 || kind == StrokeStyle::None) {
        return;
    }
    const qreal length = StrokeStyle::arrowLength(s.baseThickness);
    const StrokePoint& tipPoint = atEnd ? p[m - 1] : p[0];
    const QPointF tip = tipPoint.pos;
    const qreal hw = s.widthAt(tipPoint.pressure) / 2.0;

    QPointF back = tip;
    qreal remaining = length;
    for (int step = 1; step < m; ++step) {
        const QPointF q = atEnd ? p[m - 1 - step].pos : p[step].pos;
        const qreal seg = QLineF(back, q).length();
        if (seg >= remaining) {
            back = lerp(back, q, remaining / seg);
            break;
        }
        remaining -= seg;
        back = q;
    }
    QPointF dir = tip - back;
    const qreal len = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
    if (len < 1e-6) {
        return;
    }
    dir /= len;

    // Wings leave the tip backwards, turned 30 degrees each way.
    static const qreal kCos = std::cos(qDegreesToRadians(30.0));
    static const qreal kSin = std::sin(qDegreesToRadians(30.0));
    const QPointF b(-dir.x(), -dir.y());
    const QPointF wings[2] = {
        QPointF(b.x() * kCos - b.y() * kSin, b.x() * kSin + b.y() * kCos),
        QPointF(b.x() * kCos + b.y() * kSin, -b.x() * kSin + b.y() * kCos),
    };

    if (kind == StrokeStyle::Filled) {
        // The tip sits past the line end so the line's round end stays inside.
        QPolygonF tri;
        tri << tip + dir * (hw / kSin) << tip + wings[0] * length << tip + wings[1] * length;
        orientLikeRibbon(tri);
        r.pieces.append(tri);
        return;
    }
    for (const QPointF& w : wings) {
        // A short ribbon from the tip along the wing, built like the stroke's.
        const QPointF end = tip + w * length;
        const QPointF perp(-w.y() * hw, w.x() * hw);
        QPolygonF quad;
        quad << tip + perp << end + perp << end - perp << tip - perp;
        r.pieces.append(quad);
        addDisc(r, end, hw);
    }
    addDisc(r, tip, hw);
}

/// The smoothed ribbon of a stroke: its points, edges, half-widths and
/// pattern positions, all indexed alike.
struct Ribbon {
    QVector<StrokePoint> pts;
    QVector<QPointF> left;
    QVector<QPointF> right;
    QVector<qreal> hw;
    QVector<qreal> pos;   ///< Empty for a solid line

    /// Drop the last @p cut of the line's length (all of it when shorter).
    void cutEnd(qreal cut)
    {
        qreal walked = 0.0;
        for (int i = pts.size() - 1; i > 0; --i) {
            const qreal seg = QLineF(pts[i - 1].pos, pts[i].pos).length();
            if (walked + seg > cut) {
                const qreal t = (walked + seg - cut) / seg;   // from i-1 toward i
                pts[i].pos = lerp(pts[i - 1].pos, pts[i].pos, t);
                left[i] = lerp(left[i - 1], left[i], t);
                right[i] = lerp(right[i - 1], right[i], t);
                hw[i] = lerp(hw[i - 1], hw[i], t);
                if (!pos.isEmpty()) {
                    pos[i] = lerp(pos[i - 1], pos[i], t);
                    pos.resize(i + 1);
                }
                pts.resize(i + 1);
                left.resize(i + 1);
                right.resize(i + 1);
                hw.resize(i + 1);
                return;
            }
            walked += seg;
        }
        pts.clear();
    }

    void reverse()
    {
        std::reverse(pts.begin(), pts.end());
        std::reverse(left.begin(), left.end());
        std::reverse(right.begin(), right.end());
        std::reverse(hw.begin(), hw.end());
        std::reverse(pos.begin(), pos.end());
    }
};

} // namespace

namespace StrokeStyleGeometry {

void addStyle(const VectorStroke& stroke, const QVector<StrokePoint>& pts,
              const QVector<QPointF>& left, const QVector<QPointF>& right,
              const QVector<qreal>& halfWidths, StrokePolygonResult& result)
{
    const int n = static_cast<int>(pts.size());
    const qreal base = stroke.baseThickness;
    const StrokeStyle::Look& look = StrokeStyle::look();
    const bool dashed = n >= 2 && base > 0.0 && stroke.lineStyle == StrokeStyle::Dashed;
    const bool dotted = n >= 2 && base > 0.0 && stroke.lineStyle == StrokeStyle::Dotted;
    Ribbon line{pts, left, right, halfWidths,
                dashed || dotted ? patternPositions(stroke, n) : QVector<qreal>()};

    // The line stops well inside a filled head, so the tip of the arrow is the
    // sharp tip of the head, even where the line bends into it.
    if (n >= 2 && (stroke.arrowStart == StrokeStyle::Filled || stroke.arrowEnd == StrokeStyle::Filled)) {
        const qreal cut = 0.5 * StrokeStyle::arrowLength(base);
        if (stroke.arrowEnd == StrokeStyle::Filled) {
            line.cutEnd(cut);
        }
        if (stroke.arrowStart == StrokeStyle::Filled && line.pts.size() >= 2) {
            line.reverse();
            line.cutEnd(cut);
            line.reverse();
        }
        result.polygon.clear();
        result.hasRoundCaps = line.pts.size() >= 2;
        if (result.hasRoundCaps) {
            result.polygon << QPolygonF(line.left);
            for (int i = line.right.size() - 1; i >= 0; --i) {
                result.polygon << line.right[i];
            }
            result.startCapCenter = line.pts.first().pos;
            result.startCapRadius = line.hw.first();
            result.endCapCenter = line.pts.last().pos;
            result.endCapRadius = line.hw.last();
        }
    }

    if (dashed) {
        const qreal dash = look.dashLength * base;
        const qreal period = dash + look.dashGap * base;
        if (dash > 0.0 && period > dash) {
            if (line.pts.size() >= 2) {
                addDashes(line.pos, line.pts, line.left, line.right, line.hw, dash, period, result);
            }
            result.polygon.clear();
            result.hasRoundCaps = false;
        }
    } else if (dotted) {
        const qreal spacing = (1.0 + look.dotGap) * base;
        if (spacing > 0.0) {
            if (line.pts.size() >= 2) {
                addDots(line.pos, line.pts, line.hw, spacing, result);
            }
            result.polygon.clear();
            result.hasRoundCaps = false;
        }
    }
    addArrow(stroke, false, stroke.arrowStart, result);
    addArrow(stroke, true, stroke.arrowEnd, result);
}

StrokePolygonResult arrowHeads(const VectorStroke& stroke)
{
    StrokePolygonResult result;
    addArrow(stroke, false, stroke.arrowStart, result);
    addArrow(stroke, true, stroke.arrowEnd, result);
    return result;
}

} // namespace StrokeStyleGeometry
