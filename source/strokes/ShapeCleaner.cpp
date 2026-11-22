#include "ShapeCleaner.h"

#include "StrokeStyle.h"

#include <QRectF>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ShapeCleaner {
namespace {

constexpr int kWindow = 3;          ///< ShortStraw window, in resampled points
constexpr int kSmoothPasses = 8;    ///< Smoothing of a curved part

qreal dist(const QPointF& a, const QPointF& b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

QPointF unit(const QPointF& v)
{
    const qreal len = std::hypot(v.x(), v.y());
    return len > 0.0 ? v / len : QPointF(1, 0);
}

qreal length(const QVector<QPointF>& p)
{
    qreal sum = 0.0;
    for (int i = 1; i < p.size(); ++i) {
        sum += dist(p[i - 1], p[i]);
    }
    return sum;
}

qreal diagonal(const QVector<QPointF>& p)
{
    const QRectF box = QPolygonF(p).boundingRect();
    return std::hypot(box.width(), box.height());
}

/// Turn in degrees from direction u to direction v (0 = straight on).
qreal turnDeg(const QPointF& u, const QPointF& v)
{
    return qAbs(qRadiansToDegrees(std::atan2(u.x() * v.y() - u.y() * v.x(),
                                             u.x() * v.x() + u.y() * v.y())));
}

QPointF closestOnSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const qreal len2 = QPointF::dotProduct(ab, ab);
    if (len2 <= 0.0) {
        return a;
    }
    return a + ab * qBound(0.0, QPointF::dotProduct(p - a, ab) / len2, 1.0);
}

/// The same line with its points @p step apart; the last point is kept exactly.
QVector<QPointF> resample(const QVector<QPointF>& raw, qreal step)
{
    QVector<QPointF> out{raw.first()};
    qreal walked = 0.0;   // since the last output point
    for (int i = 1; i < raw.size(); ++i) {
        QPointF a = raw[i - 1];
        qreal d = dist(a, raw[i]);
        while (d > 0.0 && walked + d >= step) {
            a += (raw[i] - a) * ((step - walked) / d);
            out << a;
            d = dist(a, raw[i]);
            walked = 0.0;
        }
        walked += d;
    }
    if (out.size() > 1 && dist(out.last(), raw.last()) <= 0.25 * step) {
        out.last() = raw.last();
    } else {
        out << raw.last();
    }
    return out;
}

/// Binomial smoothing; the two ends stay unless @p cyclic.
void smooth(QVector<QPointF>& p, int passes, bool cyclic)
{
    const int n = p.size();
    if (n < 3) {
        return;
    }
    QVector<QPointF> q(n);
    for (int k = 0; k < passes; ++k) {
        for (int i = 0; i < n; ++i) {
            q[i] = (!cyclic && (i == 0 || i == n - 1))
                ? p[i]
                : 0.25 * p[(i + n - 1) % n] + 0.5 * p[i] + 0.25 * p[(i + 1) % n];
        }
        p.swap(q);
    }
}

/**
 * Corner indices: ShortStraw (the chord across the point is short) plus a
 * turn of at least @p minTurnDeg. A long run of short chords is a tight
 * curve (an ellipse tip, the inside of a spiral), not a corner.
 */
QVector<int> findCorners(const QVector<QPointF>& p, bool cyclic, qreal minTurnDeg)
{
    const int n = p.size();
    QVector<int> corners;
    if (n < 2 * kWindow + 1) {
        return corners;
    }
    const auto at = [&](int i) { return p[cyclic ? (i % n + n) % n : qBound(0, i, n - 1)]; };
    const int first = cyclic ? 0 : kWindow;
    const int count = cyclic ? n : n - 2 * kWindow;
    QVector<qreal> straw(n, 0.0);
    QVector<qreal> sorted;
    sorted.reserve(count);
    for (int k = 0; k < count; ++k) {
        straw[first + k] = dist(at(first + k - kWindow), at(first + k + kWindow));
        sorted << straw[first + k];
    }
    std::nth_element(sorted.begin(), sorted.begin() + count / 2, sorted.end());
    const qreal threshold = 0.95 * sorted[count / 2];

    // A cyclic scan starts outside a run, so no run is cut in two.
    int start = first;
    if (cyclic) {
        while (start < n - 1 && straw[start] < threshold) {
            ++start;
        }
    }
    const auto index = [&](int k) { return cyclic ? (start + k) % n : first + k; };
    for (int k = 0; k < count;) {
        if (straw[index(k)] >= threshold) {
            ++k;
            continue;
        }
        int best = index(k);
        int run = 0;
        for (; k < count && straw[index(k)] < threshold; ++k, ++run) {
            if (straw[index(k)] < straw[best]) {
                best = index(k);
            }
        }
        if (run <= 3 * kWindow
            && turnDeg(at(best) - at(best - kWindow), at(best + kWindow) - at(best)) >= minTurnDeg) {
            corners << best;
        }
    }
    std::sort(corners.begin(), corners.end());
    return corners;
}

struct Analysis {
    QVector<QPointF> p;     ///< Resampled; a closed loop does not repeat its first point
    QVector<int> corners;   ///< Indices into p
    bool closed = false;
};

Analysis analyse(const QVector<QPointF>& raw, const Params& prm)
{
    Analysis a;
    const qreal diag = diagonal(raw);
    // Wiggles finer than half the line width are pen noise, not shape.
    const qreal step = qMax(diag / 64.0, qMax(0.5 * prm.width, 0.25));
    a.p = resample(raw, step);
    QVector<QPointF>& p = a.p;

    // The pen may land with a tail, and the end may stop short of the start or
    // run past it: the loop is the part between the two points, one in each
    // fifth at the ends, that come nearest each other. That cuts the tails at
    // both ends; an overlap or a crossing closes where the line meets itself.
    // (n/5)^2 checks, once at pen lift.
    const int n = p.size();
    int from = 0;
    int to = n - 1;
    qreal nearest = std::numeric_limits<qreal>::max();
    for (int i = 0; i <= n / 5; ++i) {
        for (int j = n - 1; j >= n - 1 - n / 5 && j > i; --j) {
            const QPointF d = p[j] - p[i];
            const qreal d2 = QPointF::dotProduct(d, d);
            if (d2 < nearest) {
                nearest = d2;
                from = i;
                to = j;
            }
        }
    }
    const qreal closeDist = qMax(prm.closeMinDist, prm.closePercent / 100.0 * diag);
    a.closed = n >= 2 * kWindow + 2 && dist(p[from], p[to]) <= closeDist
               && length(p.mid(from, to - from + 1)) > 2.0 * closeDist;
    if (a.closed) {
        p = p.mid(from, to - from + 1);
        // Fill the gap at the same spacing; the loop never repeats its start.
        const QPointF from = p.last();
        const QPointF to = p.first();
        const int fill = static_cast<int>(dist(from, to) / step);
        for (int k = 1; k < fill; ++k) {
            p << from + (to - from) * (qreal(k) / fill);
        }
        if (p.size() > 2 && dist(p.last(), p.first()) < 0.5 * step) {
            p.removeLast();
        }
    }
    a.corners = findCorners(p, a.closed, prm.cornerAngleDeg);
    return a;
}

/**
 * Interior points of the straight edge a-b. Their spacing starts at @p sa and
 * @p sb at the two ends and doubles toward the middle. The renderer smooths
 * with uniform Catmull-Rom, which overshoots where a long segment meets a
 * short one; here no segment is more than about four times its neighbour.
 */
void appendEdge(QVector<QPointF>& out, QPointF a, QPointF b, qreal sa, qreal sb)
{
    const qreal len = dist(a, b);
    if (len <= 0.0) {
        return;
    }
    const QPointF dir = (b - a) / len;
    qreal ta = 0.0;
    qreal tb = len;
    QVector<qreal> fromB;
    for (;;) {
        const bool fromA = sa <= sb;
        qreal& s = fromA ? sa : sb;
        if (tb - ta <= 2.0 * s) {
            break;
        }
        if (fromA) {
            ta += s;
            out << a + dir * ta;
        } else {
            tb -= s;
            fromB.prepend(tb);
        }
        s *= 2.0;
    }
    for (qreal t : fromB) {
        out << a + dir * t;
    }
}

/// The point where line a0-a1 meets line b0-b1; false when they are parallel.
bool meet(const QPointF& a0, const QPointF& a1, const QPointF& b0, const QPointF& b1, QPointF* at)
{
    const QPointF r = a1 - a0;
    const QPointF s = b1 - b0;
    const qreal den = r.x() * s.y() - r.y() * s.x();
    if (qAbs(den) < 1e-9 * (std::hypot(r.x(), r.y()) * std::hypot(s.x(), s.y()) + 1e-12)) {
        return false;
    }
    const QPointF d = b0 - a0;
    *at = a0 + r * ((d.x() * s.y() - d.y() * s.x()) / den);
    return true;
}

/// How far from corner @p c a round corner of @p radius touches its sides
/// (an arc tangent to both); at most 0.45 of the shorter side, 0 when sharp.
qreal tangentLength(const QPointF& prev, const QPointF& c, const QPointF& next, qreal radius)
{
    const qreal in = dist(prev, c);
    const qreal out = dist(c, next);
    if (radius <= 0.0 || in <= 0.0 || out <= 0.0) {
        return 0.0;
    }
    const qreal cosA = qBound(-1.0, QPointF::dotProduct(prev - c, next - c) / (in * out), 1.0);
    const qreal tanHalf = std::tan(0.5 * std::acos(cosA));   // half the inside angle
    const qreal limit = 0.45 * qMin(in, out);
    return tanHalf > 1e-9 ? qMin(radius / tanHalf, limit) : limit;
}

/**
 * The dense line through @p pts. Each corner becomes a sharp point (a short
 * first step on both edges keeps Catmull-Rom from rounding it) or, with a
 * radius, a circular arc tangent to both edges, its points about 7.5 degrees
 * apart so the renderer's smoothing stays on the arc. Curve points pass
 * through. @p joints gets the first and last index of each corner.
 */
QVector<QPointF> build(const QVector<QPointF>& pts, const QVector<bool>& corner, bool closed,
                       qreal radius, qreal width, QVector<int>* jointsOut = nullptr)
{
    const int n = pts.size();
    if (n < 2 || (n == 2 && !closed)) {
        return pts;
    }
    const qreal eps = qMax(0.3 * width, 0.05);
    struct Joint {
        QVector<QPointF> pts;
        qreal step = 0.0;   ///< First step of the edges around it; 0 = not a corner
    };
    QVector<Joint> joints(n);
    for (int i = 0; i < n; ++i) {
        Joint& j = joints[i];
        const QPointF c = pts[i];
        j.pts = {c};
        if (!corner[i] || (!closed && (i == 0 || i == n - 1))) {
            continue;
        }
        const QPointF prev = pts[(i + n - 1) % n];
        const QPointF next = pts[(i + 1) % n];
        if (dist(prev, c) <= 0.0 || dist(c, next) <= 0.0) {
            continue;
        }
        j.step = eps;
        const qreal t = tangentLength(prev, c, next, radius);
        if (t > eps) {
            const QPointF u = unit(prev - c);
            const QPointF v = unit(next - c);
            const QPointF pin = c + u * t;
            const QPointF pout = c + v * t;
            QPointF centre;   // where the normals at the two touch points meet
            if (!meet(pin, pin + QPointF(-u.y(), u.x()), pout, pout + QPointF(-v.y(), v.x()), &centre)) {
                continue;
            }
            const qreal r = dist(centre, pin);
            const qreal a0 = std::atan2(pin.y() - centre.y(), pin.x() - centre.x());
            const qreal sweep = std::remainder(std::atan2(pout.y() - centre.y(), pout.x() - centre.x()) - a0,
                                               2.0 * M_PI);
            const int steps = qMax(2, static_cast<int>(std::ceil(qAbs(sweep) / qDegreesToRadians(7.5))));
            j.pts = {pin};
            for (int s = 1; s < steps; ++s) {
                const qreal a = a0 + sweep * s / steps;
                j.pts << centre + r * QPointF(std::cos(a), std::sin(a));
            }
            j.pts << pout;
            j.step = dist(pin, j.pts[1]);
        }
    }

    const auto edge = [](QVector<QPointF>& out, QPointF a, qreal sa, QPointF b, qreal sb) {
        if (sa > 0.0 || sb > 0.0) {
            const qreal len = dist(a, b);
            appendEdge(out, a, b, sa > 0.0 ? sa : len, sb > 0.0 ? sb : len);
        }
    };
    QVector<QPointF> out;
    QPointF start;
    if (jointsOut) {
        jointsOut->clear();
    }
    const auto addJoint = [&](int i) {
        if (jointsOut && joints[i].step > 0.0) {
            *jointsOut << out.size() << out.size() + joints[i].pts.size() - 1;
        }
        out += joints[i].pts;
    };
    if (closed) {
        // Start and end in the middle of the edge from the last vertex to the first.
        start = 0.5 * (joints[n - 1].pts.last() + joints[0].pts.first());
        out << start;
        edge(out, start, 0.0, joints[0].pts.first(), joints[0].step);
    }
    addJoint(0);
    for (int i = 1; i < n; ++i) {
        edge(out, out.last(), joints[i - 1].step, joints[i].pts.first(), joints[i].step);
        addJoint(i);
    }
    if (closed) {
        edge(out, out.last(), joints[n - 1].step, start, 0.0);
        out << start;
    }
    return out;
}

/// The point @p back along the line from pts[0].
QPointF pointBack(const QVector<QPointF>& pts, qreal back)
{
    qreal walked = 0.0;
    for (int i = 1; i < pts.size(); ++i) {
        const qreal d = dist(pts[i - 1], pts[i]);
        if (d > 0.0 && walked + d >= back) {
            return pts[i - 1] + (pts[i] - pts[i - 1]) * ((back - walked) / d);
        }
        walked += d;
    }
    return pts.last();
}

qreal cross(const QPointF& a, const QPointF& b)
{
    return a.x() * b.y() - a.y() * b.x();
}

/// Angle between two lines, 0..90 degrees.
qreal lineAngleDeg(const QPointF& u, const QPointF& v)
{
    const qreal t = turnDeg(u, v);
    return qMin(t, 180.0 - t);
}

void rotateAbout(QVector<QPointF>& pts, const QPointF& c, qreal rad)
{
    const qreal cs = std::cos(rad);
    const qreal sn = std::sin(rad);
    for (QPointF& q : pts) {
        const QPointF d = q - c;
        q = c + QPointF(d.x() * cs - d.y() * sn, d.x() * sn + d.y() * cs);
    }
}

/// Signed angle (degrees) that turns @p dir onto the nearest page axis.
qreal offLevelDeg(const QPointF& dir)
{
    const qreal deg = qRadiansToDegrees(std::atan2(dir.y(), dir.x()));
    return deg - 90.0 * std::round(deg / 90.0);
}

/// Turn @p pts about @p c so the one of @p dirs nearest the page axes lies on
/// them, when it is within @p maxDeg.
void level(QVector<QPointF>& pts, const QPointF& c, const QVector<QPointF>& dirs, qreal maxDeg)
{
    qreal best = 90.0;
    for (const QPointF& d : dirs) {
        const qreal off = offLevelDeg(d);
        if (qAbs(off) < qAbs(best)) {
            best = off;
        }
    }
    if (qAbs(best) <= maxDeg && best != 0.0) {
        rotateAbout(pts, c, -qDegreesToRadians(best));
    }
}

/// Least-squares line through @p pts: a point on it and its direction.
void fitLine(const QVector<QPointF>& pts, QPointF& at, QPointF& dir)
{
    at = QPointF();
    for (const QPointF& q : pts) {
        at += q;
    }
    at /= pts.size();
    qreal sxx = 0.0, syy = 0.0, sxy = 0.0;
    for (const QPointF& q : pts) {
        const QPointF d = q - at;
        sxx += d.x() * d.x();
        syy += d.y() * d.y();
        sxy += d.x() * d.y();
    }
    const qreal t = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
    dir = QPointF(std::cos(t), std::sin(t));
}

struct Ellipse {
    QPointF c;
    qreal a = 0.0;      ///< Half axis along `angle`
    qreal b = 0.0;
    qreal angle = 0.0;  ///< Radians
    qreal area = 0.0;   ///< Signed area of the loop: its sign is the way it was drawn
};

/// The ellipse with the area moments of the closed loop @p p (exact for an ellipse).
Ellipse fitEllipse(const QVector<QPointF>& p)
{
    Ellipse e;
    const int n = p.size();
    QPointF m;
    for (const QPointF& q : p) {
        m += q;
    }
    m /= n;
    qreal area = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0, sxy = 0.0;
    for (int i = 0; i < n; ++i) {
        const QPointF p0 = p[i] - m;
        const QPointF p1 = p[(i + 1) % n] - m;
        const qreal c = cross(p0, p1);
        area += c;
        sx += (p0.x() + p1.x()) * c;
        sy += (p0.y() + p1.y()) * c;
        sxx += (p0.x() * p0.x() + p0.x() * p1.x() + p1.x() * p1.x()) * c;
        syy += (p0.y() * p0.y() + p0.y() * p1.y() + p1.y() * p1.y()) * c;
        sxy += (p0.x() * p1.y() + 2.0 * p0.x() * p0.y() + 2.0 * p1.x() * p1.y() + p1.x() * p0.y()) * c;
    }
    area /= 2.0;
    if (qAbs(area) < 1e-9) {
        return e;
    }
    const qreal cx = sx / (6.0 * area);
    const qreal cy = sy / (6.0 * area);
    const qreal mxx = sxx / (12.0 * area) - cx * cx;
    const qreal myy = syy / (12.0 * area) - cy * cy;
    const qreal mxy = sxy / (24.0 * area) - cx * cy;
    const qreal half = 0.5 * (mxx + myy);
    const qreal d = std::hypot(0.5 * (mxx - myy), mxy);
    e.c = m + QPointF(cx, cy);
    e.a = 2.0 * std::sqrt(qMax(0.0, half + d));   // a filled ellipse has moment a²/4
    e.b = 2.0 * std::sqrt(qMax(0.0, half - d));
    e.angle = 0.5 * std::atan2(2.0 * mxy, mxx - myy);
    e.area = area;
    return e;
}

/// Mean distance of @p p from the ellipse, measured along the rays from its centre.
qreal ellipseError(const QVector<QPointF>& p, const Ellipse& e)
{
    const QPointF u(std::cos(e.angle), std::sin(e.angle));
    const QPointF v(-u.y(), u.x());
    qreal sum = 0.0;
    for (const QPointF& q : p) {
        const QPointF d = q - e.c;
        const qreal rho = std::hypot(QPointF::dotProduct(d, u) / e.a, QPointF::dotProduct(d, v) / e.b);
        sum += rho > 0.0 ? std::hypot(d.x(), d.y()) * qAbs(1.0 - 1.0 / rho) : qMin(e.a, e.b);
    }
    return sum / p.size();
}

/// Points of the ellipse, a circle when its axes are nearly equal; it starts
/// near @p start and turns the way the line was drawn.
QVector<QPointF> ellipsePoints(Ellipse e, const QPointF& start, const Params& prm)
{
    if (e.b >= (1.0 - prm.equalPercent / 100.0) * e.a) {
        e.a = e.b = 0.5 * (e.a + e.b);
    } else if (qAbs(offLevelDeg(QPointF(std::cos(e.angle), std::sin(e.angle)))) <= prm.levelDeg) {
        e.angle -= qDegreesToRadians(offLevelDeg(QPointF(std::cos(e.angle), std::sin(e.angle))));
    }
    const QPointF u(std::cos(e.angle), std::sin(e.angle));
    const QPointF v(-u.y(), u.x());
    const QPointF s = start - e.c;
    const qreal t0 = std::atan2(QPointF::dotProduct(s, v) / e.b, QPointF::dotProduct(s, u) / e.a);
    const qreal turn = e.area > 0.0 ? 2.0 * M_PI : -2.0 * M_PI;
    // Ramanujan's perimeter; a point every 1.5 widths, enough for Catmull-Rom at any zoom.
    const qreal perimeter = M_PI * (3.0 * (e.a + e.b) - std::sqrt((3.0 * e.a + e.b) * (e.a + 3.0 * e.b)));
    const int n = qBound(48, static_cast<int>(perimeter / (1.5 * prm.width)), 240);
    QVector<QPointF> out;
    out.reserve(n + 1);
    for (int k = 0; k < n; ++k) {
        const qreal t = t0 + turn * k / n;
        out << e.c + u * (e.a * std::cos(t)) + v * (e.b * std::sin(t));
    }
    out << out.first();
    return out;
}

/// Corners of the closed loop as the meeting points of its straight sides,
/// fitted to the drawn points; empty when it has fewer than 3. A corner where
/// the fitted sides turn less than @p minTurnDeg (a wobble, a stub) is dropped.
QVector<QPointF> polygonCorners(const QVector<QPointF>& p, QVector<int> c, qreal minTurnDeg)
{
    const int n = p.size();
    // A hook at a corner gives two corners close together: keep one.
    for (bool merged = true; merged && c.size() > 3;) {
        merged = false;
        for (int j = 0; j < c.size(); ++j) {
            if ((c[(j + 1) % c.size()] - c[j] + n) % n < 0.08 * n) {
                c.removeAt((j + 1) % c.size());
                merged = true;
                break;
            }
        }
    }
    int k = 0;
    QVector<QPointF> at, dir;
    const auto fitSides = [&]() {
        k = c.size();
        at.resize(k);
        dir.resize(k);
        for (int j = 0; j < k; ++j) {
            // The side without the points next to its corners, which a hand rounds.
            const int len = (c[(j + 1) % k] - c[j] + n) % n;
            const int skip = len / 8;
            QVector<QPointF> side;
            for (int i = skip; i <= len - skip; ++i) {
                side << p[(c[j] + i) % n];
            }
            fitLine(side, at[j], dir[j]);
            // Pointed the way the line runs, so a turn back reads as one.
            if (QPointF::dotProduct(dir[j], p[(c[j] + len) % n] - p[c[j]]) < 0.0) {
                dir[j] = -dir[j];
            }
        }
    };
    if (c.size() < 3) {
        return {};
    }
    fitSides();
    // Drop the corners the fitted sides hardly turn at, then fit once more.
    QVector<int> kept;
    for (int j = 0; j < k; ++j) {
        if (turnDeg(dir[(j + k - 1) % k], dir[j]) >= minTurnDeg) {
            kept << c[j];
        }
    }
    if (kept.size() < 3) {
        return {};
    }
    if (kept.size() < k) {
        c = kept;
        fitSides();
    }
    QVector<QPointF> out(k);
    for (int j = 0; j < k; ++j) {
        const int prev = (j + k - 1) % k;
        const QPointF drawn = p[c[j]];
        out[j] = drawn;
        const qreal den = cross(dir[prev], dir[j]);
        if (qAbs(den) < 0.17) {
            continue;   // Nearly in line (under 10 degrees): keep the drawn corner
        }
        const QPointF meet = at[prev] + dir[prev] * (cross(at[j] - at[prev], dir[j]) / den);
        const qreal reach = 0.3 * qMin(dist(drawn, p[c[prev]]), dist(drawn, p[c[(j + 1) % k]]));
        if (dist(meet, drawn) <= reach) {
            out[j] = meet;
        }
    }
    return out;
}

qreal polygonError(const QVector<QPointF>& p, const QVector<QPointF>& corners)
{
    qreal sum = 0.0;
    const int k = corners.size();
    for (const QPointF& q : p) {
        qreal best = std::numeric_limits<qreal>::max();
        for (int j = 0; j < k; ++j) {
            best = qMin(best, dist(q, closestOnSegment(q, corners[j], corners[(j + 1) % k])));
        }
        sum += best;
    }
    return sum / p.size();
}

/**
 * The polygon made regular where it nearly is: four corners become a square,
 * rectangle, rhombus, parallelogram or trapezoid, in that order; then the
 * shape is made level when it is nearly level.
 */
QVector<QPointF> regularPolygon(QVector<QPointF> c, const Params& prm, QVector<int>* joints)
{
    QPointF m;
    for (const QPointF& q : c) {
        m += q;
    }
    m /= c.size();
    QVector<QPointF> dirs;   // the lines that may be made level
    for (int j = 0; j < c.size(); ++j) {
        dirs << c[(j + 1) % c.size()] - c[j];
    }
    if (c.size() == 4) {
        const qreal snap = prm.angleSnapDeg;
        bool par02 = lineAngleDeg(dirs[0], dirs[2]) <= snap;
        bool par13 = lineAngleDeg(dirs[1], dirs[3]) <= snap;
        // A quick box has one pair of sides off by more than the snap: still a
        // box, unless that pair is clearly apart (a real trapezoid).
        if (par02 != par13 && lineAngleDeg(dirs[par02 ? 1 : 0], dirs[par02 ? 3 : 2]) <= prm.trapezoidDeg) {
            par02 = par13 = true;
        }
        if (par02 && par13) {
            QPointF u = unit(dirs[0] - dirs[2]);   // opposite sides run opposite ways
            QPointF v = unit(dirs[1] - dirs[3]);
            qreal lu = 0.5 * (std::hypot(dirs[0].x(), dirs[0].y()) + std::hypot(dirs[2].x(), dirs[2].y()));
            qreal lv = 0.5 * (std::hypot(dirs[1].x(), dirs[1].y()) + std::hypot(dirs[3].x(), dirs[3].y()));
            const bool equal = qAbs(lu - lv) <= prm.equalPercent / 100.0 * qMax(lu, lv);
            if (lineAngleDeg(u, v) >= 90.0 - snap) {
                // Square corners: u and v turned onto u, averaged.
                const qreal sign = cross(u, v) > 0.0 ? 1.0 : -1.0;
                u = unit(u + QPointF(v.y(), -v.x()) * sign);
                v = QPointF(-u.y(), u.x()) * sign;
            }
            dirs = {u, v};
            if (equal) {
                lu = lv = 0.5 * (lu + lv);
                dirs << u + v << u - v;   // a square or a rhombus may stand on a corner
            }
            const QPointF hu = u * (0.5 * lu);
            const QPointF hv = v * (0.5 * lv);
            c = {m - hu - hv, m + hu - hv, m + hu + hv, m - hu + hv};
        } else if (par02 || par13) {
            // Trapezoid: the two nearly parallel sides made parallel, each about its middle.
            const int a = par02 ? 0 : 1;
            const QPointF u = unit(dirs[a] - dirs[a + 2]);
            const QPointF m0 = 0.5 * (c[a] + c[a + 1]);
            const QPointF m2 = 0.5 * (c[a + 2] + c[(a + 3) % 4]);
            const qreal h0 = 0.5 * std::hypot(dirs[a].x(), dirs[a].y());
            const qreal h2 = 0.5 * std::hypot(dirs[a + 2].x(), dirs[a + 2].y());
            c[a] = m0 - u * h0;
            c[a + 1] = m0 + u * h0;
            c[a + 2] = m2 + u * h2;
            c[(a + 3) % 4] = m2 - u * h2;
            dirs = {u};
        }
    }
    level(c, m, dirs, prm.levelDeg);
    return polyline(c, true, stepRadius(prm.rounded ? 1 : 0, prm.width), prm.width, joints);
}

/// The exact ellipse or polygon of a closed line, or nothing when neither fits.
QVector<QPointF> perfectShape(const QVector<QPointF>& p, const QVector<int>& corners, const Params& prm,
                              QVector<int>* joints)
{
    if (prm.roughPercent <= 0.0 || p.size() < 8) {
        return {};
    }
    const Ellipse e = fitEllipse(p);
    const qreal size = std::sqrt(e.a * e.b);
    if (!(size > 0.0)) {
        return {};
    }
    const qreal tol = prm.roughPercent / 100.0 * size;
    const qreal ellipseErr = ellipseError(p, e);
    QVector<QPointF> poly = polygonCorners(p, corners, prm.cornerAngleDeg);
    qreal polyErr = poly.isEmpty() ? std::numeric_limits<qreal>::max() : polygonError(p, poly);
    // A corner a quick hand rounds wide is a gentle curve at this scale; with
    // every second point it is sharp enough to be found. Only for a line that
    // already has corners: on a round one this would find a polygon. The
    // better fit wins.
    if (corners.size() >= 3) {
        QVector<QPointF> half;
        for (int i = 0; i < p.size(); i += 2) {
            half << p[i];
        }
        QVector<int> wide = findCorners(half, true, prm.cornerAngleDeg);
        for (int& c : wide) {
            c *= 2;
        }
        const QVector<QPointF> widePoly = polygonCorners(p, wide, prm.cornerAngleDeg);
        const qreal wideErr = widePoly.isEmpty() ? polyErr : polygonError(p, widePoly);
        if (wideErr < polyErr) {
            poly = widePoly;
            polyErr = wideErr;
        }
    }
    // A round line with a stray corner stays round: a polygon must fit clearly better.
    if (polyErr <= tol && polyErr < 0.75 * ellipseErr) {
        return regularPolygon(poly, prm, joints);
    }
    if (ellipseErr <= tol) {
        return ellipsePoints(e, p.first(), prm);
    }
    return polyErr <= tol ? regularPolygon(poly, prm, joints) : QVector<QPointF>();
}

/// An open curve without corners that is nearly a circular arc: that exact arc.
QVector<QPointF> arcPoints(const QVector<QPointF>& p, const Params& prm)
{
    const int n = p.size();
    if (prm.roughPercent <= 0.0 || n < 5) {
        return {};
    }
    // Fit a circle (Kasa, about the mean point).
    QPointF m;
    for (const QPointF& q : p) {
        m += q;
    }
    m /= n;
    qreal suu = 0, svv = 0, suv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
    for (const QPointF& q : p) {
        const qreal u = q.x() - m.x();
        const qreal v = q.y() - m.y();
        suu += u * u;
        svv += v * v;
        suv += u * v;
        suuu += u * u * u;
        svvv += v * v * v;
        suvv += u * v * v;
        svuu += v * u * u;
    }
    const qreal det = suu * svv - suv * suv;
    if (qAbs(det) < 1e-12) {
        return {};
    }
    const qreal bu = 0.5 * (suuu + suvv);
    const qreal bv = 0.5 * (svvv + svuu);
    const QPointF c = m + QPointF((svv * bu - suv * bv) / det, (suu * bv - suv * bu) / det);
    const qreal r = std::sqrt(QPointF::dotProduct(c - m, c - m) + (suu + svv) / n);
    qreal sweep = 0.0;
    qreal err = 0.0;
    qreal prev = std::atan2(p[0].y() - c.y(), p[0].x() - c.x());
    const qreal start = prev;
    for (const QPointF& q : p) {
        const qreal a = std::atan2(q.y() - c.y(), q.x() - c.x());
        sweep += std::remainder(a - prev, 2.0 * M_PI);
        prev = a;
        err += qAbs(dist(q, c) - r);
    }
    err /= n;
    const qreal span = qAbs(sweep);
    // The sag of the arc: a nearly straight S must not pass for a flat arc.
    const qreal sag = r * (1.0 - std::cos(0.5 * qMin(span, M_PI)));
    if (span < qDegreesToRadians(15.0) || span > qDegreesToRadians(330.0)
        || err > qMin(prm.roughPercent / 100.0 * r, 0.25 * sag)) {
        return {};
    }
    const int k = qBound(12, static_cast<int>(span * r / (1.5 * prm.width)), 180);
    QVector<QPointF> out;
    out.reserve(k + 1);
    for (int i = 0; i <= k; ++i) {
        const qreal a = start + sweep * i / k;
        out << c + r * QPointF(std::cos(a), std::sin(a));
    }
    return out;
}

} // namespace

QVector<QPointF> polyline(const QVector<QPointF>& corners, bool closed, qreal cornerRadius, qreal width,
                          QVector<int>* joints)
{
    return build(corners, QVector<bool>(corners.size(), true), closed, cornerRadius, width, joints);
}

qreal stepRadius(int step, qreal width)
{
    switch (step) {
    case 0:  return 0.0;
    case 2:  return qMax(8.0 * width, 12.0);
    case 3:  return qMax(16.0 * width, 32.0);
    default: return 3.0 * width;   // Small: what "Rounded corners" has always given
    }
}

QVector<QPointF> jointCorners(const QVector<QPointF>& pts, const QVector<int>& joints)
{
    const int n = pts.size();
    const int k = joints.size() / 2;
    // A closed outline whose joints are in order and leave a point on each side.
    if (k < 3 || joints.size() % 2 != 0 || n < 4 || dist(pts.first(), pts.last()) > 1e-6) {
        return {};
    }
    QVector<QPointF> out;
    out.reserve(k);
    int previous = 0;
    for (int j = 0; j < k; ++j) {
        const int first = joints[2 * j];
        const int last = joints[2 * j + 1];
        if (first <= previous || last < first || last > n - 2) {
            return {};
        }
        previous = last;
        QPointF c = pts[first];
        // A round corner: where the two straight sides it joins would meet.
        if (last > first && !meet(pts[first - 1], pts[first], pts[last], pts[last + 1], &c)) {
            return {};
        }
        out << c;
    }
    return out;
}

qreal cornerRadius(const QVector<QPointF>& pts, const QVector<int>& joints, qreal width)
{
    const QVector<QPointF> c = jointCorners(pts, joints);
    const int k = c.size();
    if (k == 0) {
        return -1.0;
    }
    // tangentLength() turned round: the radius whose arc touches the sides
    // where this corner's does. A corner cut short by a short side gives less.
    qreal radius = 0.0;
    for (int j = 0; j < k; ++j) {
        const QPointF prev = c[(j + k - 1) % k];
        const QPointF next = c[(j + 1) % k];
        const qreal in = dist(prev, c[j]);
        const qreal out = dist(c[j], next);
        const qreal t = dist(c[j], pts[joints[2 * j]]);
        if (in <= 0.0 || out <= 0.0 || t <= qMax(0.3 * width, 0.05)) {
            continue;   // sharp, as build() makes it
        }
        const qreal cosA = qBound(-1.0, QPointF::dotProduct(prev - c[j], next - c[j]) / (in * out), 1.0);
        radius = qMax(radius, t * std::tan(0.5 * std::acos(cosA)));
    }
    return radius;
}

QVector<QPointF> clean(const QVector<QPointF>& raw, const Params& prm, bool* closedOut, QVector<int>* joints)
{
    if (closedOut) {
        *closedOut = false;
    }
    if (joints) {
        joints->clear();
    }
    if (raw.size() < 2 || diagonal(raw) <= 0.0) {
        return raw;
    }
    const Analysis a = analyse(raw, prm);
    const QVector<QPointF>& p = a.p;
    const int n = p.size();
    // Nearly straight: only its two ends are kept.
    const auto straight = [&prm](const QVector<QPointF>& part) {
        qreal deviation = 0.0;
        for (const QPointF& q : part) {
            deviation = qMax(deviation, dist(q, closestOnSegment(q, part.first(), part.last())));
        }
        return deviation < qMax(1.5 * prm.width, 0.04 * dist(part.first(), part.last()));
    };

    // Exact shapes first: an ellipse or a polygon, or an arc.
    QVector<QPointF> exact;
    if (a.closed) {
        exact = perfectShape(p, a.corners, prm, joints);
    } else if (a.corners.isEmpty() && !straight(p)) {
        exact = arcPoints(p, prm);
        if (!exact.isEmpty() && prm.snapTargets && prm.snapDist > 0.0) {
            snapEnds(exact, *prm.snapTargets, prm.snapDist);
        }
    }
    if (!exact.isEmpty()) {
        if (closedOut) {
            *closedOut = a.closed;
        }
        return exact;
    }

    QVector<QPointF> v;     // vertices of the result
    QVector<bool> corner;   // v[i] is a corner (else a curve point or an open end)
    if (a.closed && a.corners.isEmpty()) {
        v = p;
        smooth(v, kSmoothPasses, true);
        corner.fill(false, v.size());
    } else {
        QVector<int> breaks = a.corners;
        if (a.closed) {
            breaks << a.corners.first() + n;
        } else {
            breaks.prepend(0);
            breaks << n - 1;
        }
        v << p[breaks.first() % n];
        corner << a.closed;
        for (int b = 1; b < breaks.size(); ++b) {
            QVector<QPointF> part;
            for (int i = breaks[b - 1]; i <= breaks[b]; ++i) {
                part << p[i % n];
            }
            if (!straight(part)) {
                // Fewer passes on a short part, which would flatten.
                smooth(part, qMin(kSmoothPasses, part.size() / 4), false);
                for (int i = 1; i + 1 < part.size(); ++i) {
                    v << part[i];
                    corner << false;
                }
            }
            const bool last = b == breaks.size() - 1;
            if (!(a.closed && last)) {   // a loop ends on its first vertex
                v << part.last();
                corner << !last;
            }
        }
    }

    if (!a.closed && prm.snapTargets && prm.snapDist > 0.0) {
        snapEnds(v, *prm.snapTargets, prm.snapDist);
    }
    if (closedOut) {
        *closedOut = a.closed;
    }
    return build(v, corner, a.closed, stepRadius(prm.rounded ? 1 : 0, prm.width), prm.width);
}

void snapEnds(QVector<QPointF>& pts, const QVector<QPolygonF>& targets, qreal maxDist)
{
    if (pts.size() < 2 || maxDist <= 0.0) {
        return;
    }
    for (int end = 0; end < 2; ++end) {
        const int i0 = end == 0 ? 0 : pts.size() - 1;
        const QPointF p = pts[i0];
        qreal cornerDist = maxDist;
        qreal edgeDist = maxDist;
        QPointF cornerPt;
        QPointF edgePt;
        bool haveCorner = false;
        bool haveEdge = false;
        for (const QPolygonF& t : targets) {
            const int m = t.size();
            const bool loop = m > 2 && t.first() == t.last();
            for (int k = 0; k < m; ++k) {
                // A corner: an end of an open line, or a point where it turns sharply.
                const bool isCorner = (k == 0 || k == m - 1)
                    ? !loop || turnDeg(t[0] - t[m - 2], t[1] - t[0]) >= 30.0
                    : turnDeg(t[k] - t[k - 1], t[k + 1] - t[k]) >= 30.0;
                const qreal d = dist(p, t[k]);
                if (isCorner && d < cornerDist) {
                    cornerDist = d;
                    cornerPt = t[k];
                    haveCorner = true;
                }
                if (k > 0) {
                    const QPointF q = closestOnSegment(p, t[k - 1], t[k]);
                    const qreal e = dist(p, q);
                    if (e < edgeDist) {
                        edgeDist = e;
                        edgePt = q;
                        haveEdge = true;
                    }
                }
            }
        }
        if (!haveCorner && !haveEdge) {
            continue;
        }
        const QPointF delta = (haveCorner ? cornerPt : edgePt) - p;
        // The move fades out over the next points, never past the middle of the line.
        const qreal fade = 3.0 * std::hypot(delta.x(), delta.y());
        const int dir = end == 0 ? 1 : -1;
        QPointF prev = p;   // where the previous point was before the move
        qreal walked = 0.0;
        pts[i0] += delta;
        for (int k = 1, i = i0 + dir; k < pts.size() / 2; ++k, i += dir) {
            walked += dist(prev, pts[i]);
            prev = pts[i];
            if (walked >= fade) {
                break;
            }
            pts[i] += delta * (1.0 - walked / fade);
        }
    }
}

QPointF centre(const QVector<QPointF>& loop)
{
    if (loop.size() < 3) {
        return QPolygonF(loop).boundingRect().center();
    }
    const Ellipse e = fitEllipse(loop);
    return e.area != 0.0 ? e.c : QPolygonF(loop).boundingRect().center();
}

ArrowMatch detectArrowHead(const QVector<QPointF>& shaft, const QVector<QPointF>& drawn,
                           qreal width, qreal px)
{
    ArrowMatch match;
    if (shaft.size() < 2 || drawn.size() < 2) {
        return match;
    }
    const QRectF box = QPolygonF(drawn).boundingRect();
    const qreal size = qMax(box.width(), box.height());
    const qreal shaftLength = length(shaft);
    // A scribble, not a dot, and small next to its shaft.
    if (size < qMax(2.0 * width, 4.0 * px) || size > 0.55 * shaftLength) {
        return match;
    }
    // Evenly spaced, so a quick stroke with few points weighs like a slow one.
    const QVector<QPointF> head = resample(drawn, size / 24.0);
    const qreal slack = width + 6.0 * px;   // a quick hand misses the end a little

    for (const bool atEnd : {true, false}) {
        QVector<QPointF> line = shaft;   // line[0] is the end being tried
        if (atEnd) {
            std::reverse(line.begin(), line.end());
        }
        const QPointF end = line[0];
        const QPointF back = unit(pointBack(line, qMin(size, 0.5 * shaftLength)) - end);
        qreal nearest = std::numeric_limits<qreal>::max();
        qreal minAlong = nearest, maxAlong = -nearest, sumAlong = 0.0, left = 0.0, right = 0.0;
        for (const QPointF& q : head) {
            const QPointF d = q - end;
            const qreal along = QPointF::dotProduct(d, back);
            const qreal side = cross(back, d);
            nearest = qMin(nearest, std::hypot(d.x(), d.y()));
            minAlong = qMin(minAlong, along);
            maxAlong = qMax(maxAlong, along);
            sumAlong += along;
            left = qMax(left, side);
            right = qMax(right, -side);
        }
        // At the end, mostly behind it, reaching back, nothing far ahead
        // (a < opening forward is no head).
        if (nearest > 0.5 * size + slack || maxAlong < 0.25 * size || sumAlong < -0.1 * size * head.size()
            || minAlong < -(0.6 * size + slack)) {
            continue;
        }
        const qreal spread = qMax(qMax(width, 4.0 * px), 0.05 * size);
        const bool twoSided = left >= spread && right >= spread;
        if (!twoSided) {
            // One barb or a hook: short, starting at the end, going out sideways
            // and back. A letter beside the end of a line ("lv") stands off it;
            // one whose stroke comes forward again ("h", "n") ends level with it.
            const QPointF a = drawn.first() - end;
            const QPointF b = drawn.last() - end;
            const bool aNear = QPointF::dotProduct(a, a) <= QPointF::dotProduct(b, b);
            const QPointF nearEnd = aNear ? a : b;
            const QPointF farEnd = aNear ? b : a;
            if (size > 0.2 * shaftLength || std::hypot(nearEnd.x(), nearEnd.y()) > 0.25 * size + slack
                || qMax(left, right) < qMax(1.5 * width, 0.12 * size)
                || QPointF::dotProduct(farEnd, back) < 0.3 * std::hypot(farEnd.x(), farEnd.y())) {
                continue;
            }
        }
        // Filled: a loop that encloses its head (a triangle). A V, or a
        // back-and-forth over the same line, encloses little.
        qreal area = 0.0;
        for (int i = 0; i < head.size(); ++i) {
            area += cross(head[i], head[(i + 1) % head.size()]);
        }
        const bool loop = dist(drawn.first(), drawn.last()) <= 0.35 * size + 0.5 * slack;
        const bool filled = twoSided && loop
                            && qAbs(0.5 * area) >= 0.25 * (maxAlong - minAlong) * (left + right);
        match.arrow = filled ? StrokeStyle::Filled : StrokeStyle::Open;
        match.atEnd = atEnd;
        return match;
    }
    return match;
}

ArrowMatch splitOneStrokeArrow(const QVector<QPointF>& raw, qreal width, qreal px, int* tip)
{
    const ArrowMatch none{};
    const qreal total = raw.size() < 3 ? 0.0 : length(raw);
    // A closed line (a triangle, a loop) is a shape, not an arrow.
    if (total <= 0.0 || dist(raw.first(), raw.last()) < 0.3 * total) {
        return none;
    }
    const qreal step = qMax(total / 120.0, 0.5 * px);
    const QVector<QPointF> p = resample(raw, step);
    constexpr int k = 2;
    int first = -1;   // the first sharp turn back: the tip, or the stroke is no arrow
    for (int i = k; i + k < p.size(); ++i) {
        if (first >= 0 && i > first + k) {
            break;
        }
        if (turnDeg(p[i] - p[i - k], p[i + k] - p[i]) < 110.0) {
            continue;
        }
        if (first < 0) {
            first = i;
        }
        // The shaft is most of the line and never turns back ("k", "n", "N" do early).
        if (i * step < 0.4 * total) {
            break;
        }
        const ArrowMatch m = detectArrowHead(p.mid(0, i + 1), p.mid(i), width, px);
        if (m.arrow == StrokeStyle::None || !m.atEnd) {
            continue;
        }
        // The raw point near there that goes farthest forward is the tip.
        const QPointF forward = unit(p[i] - p[i - k]);
        qreal walked = 0.0;
        int best = -1;
        for (int j = 0; j < raw.size(); ++j) {
            walked += j > 0 ? dist(raw[j - 1], raw[j]) : 0.0;
            if (qAbs(walked - i * step) <= 3.0 * step
                && (best < 0 || QPointF::dotProduct(raw[j] - raw[best], forward) > 0.0)) {
                best = j;
            }
        }
        if (best <= 0) {
            continue;
        }
        if (tip) {
            *tip = best;
        }
        return m;
    }
    return none;
}

} // namespace ShapeCleaner
