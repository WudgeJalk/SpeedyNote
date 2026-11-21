#pragma once

// ============================================================================
// ShapeCleanerTests - the shape recogniser (draw and hold, free shape, arrows)
// ============================================================================
// Run: speedynote --test-shapes (desktop debug builds)
// ============================================================================

#include "ShapeCleaner.h"
#include "StrokeStyle.h"
#include "VectorStroke.h"
#include "../layers/VectorLayer.h"
#include "../core/Document.h"
#include "../core/Page.h"
#include "../objects/ConnectorObject.h"
#include "../objects/ShapeObject.h"

#include <QImage>
#include <QPainter>
#include <QTransform>
#include <QtMath>

#include <cmath>
#include <cstdio>

class ShapeCleanerTests {
public:
    static bool runAllTests()
    {
        printf("\n=== ShapeCleaner Tests ===\n\n");
        int failed = 0;
        const auto run = [&failed](bool (*test)(), const char* name) {
            printf("  %s... ", name);
            if (test()) {
                printf("PASSED\n");
            } else {
                printf("  [FAILED] %s\n", name);
                ++failed;
            }
        };
        run(testLineAndPolyline, "testLineAndPolyline");
        run(testSquareClosesWithFourCorners, "testSquareClosesWithFourCorners");
        run(testCurvesStaySmooth, "testCurvesStaySmooth");
        run(testArrowHeads, "testArrowHeads");
        run(testExactShapes, "testExactShapes");
        run(testQuickArrows, "testQuickArrows");
        run(testSnapping, "testSnapping");
        run(testShapeObjects, "testShapeObjects");
        run(testConnectors, "testConnectors");
        run(testConnectorRoutes, "testConnectorRoutes");
        run(testCornersAndArrowTips, "testCornersAndArrowTips");
        printf("\n=== Results: %s ===\n\n", failed == 0 ? "all passed" : "FAILED");
        return failed == 0;
    }

private:
    static bool fail(const char* message)
    {
        printf("FAILED: %s\n", message);
        return false;
    }

    /// Points from a to b with a sideways wobble of about @p amp, about 56 units
    /// long like a hand's (no pen is that steady).
    static QVector<QPointF> wobbly(QPointF a, QPointF b, int n, qreal amp)
    {
        QVector<QPointF> out;
        const QPointF d = b - a;
        const qreal len = std::hypot(d.x(), d.y());
        const QPointF normal(-d.y() / len, d.x() / len);
        for (int i = 0; i <= n; ++i) {
            const qreal t = qreal(i) / n;
            out << a + d * t + normal * (amp * std::sin(len * t / 9.0) * std::sin(t * M_PI));
        }
        return out;
    }

    /// Number of points where the line turns by at least @p deg.
    static int sharpTurns(const QVector<QPointF>& p, qreal deg)
    {
        int count = 0;
        for (int i = 1; i + 1 < p.size(); ++i) {
            const QPointF u = p[i] - p[i - 1];
            const QPointF v = p[i + 1] - p[i];
            const qreal turn = qAbs(qRadiansToDegrees(std::atan2(u.x() * v.y() - u.y() * v.x(),
                                                                 u.x() * v.x() + u.y() * v.y())));
            if (turn >= deg) {
                ++count;
            }
        }
        return count;
    }

    /// Bounds of what the renderer fills for these points (Catmull-Rom smoothed).
    static QRectF drawnBounds(const QVector<QPointF>& pts, qreal width)
    {
        VectorStroke stroke;
        stroke.baseThickness = width;
        for (const QPointF& p : pts) {
            StrokePoint sp;
            sp.pos = p;
            stroke.points << sp;
        }
        return VectorLayer::buildStrokePolygon(stroke).polygon.boundingRect();
    }

    static ShapeCleaner::Params params(qreal width, bool rounded)
    {
        ShapeCleaner::Params p;
        p.width = width;
        p.rounded = rounded;
        return p;
    }

    static bool testLineAndPolyline()
    {
        bool closed = true;
        const QVector<QPointF> line = ShapeCleaner::clean(
            wobbly(QPointF(10, 20), QPointF(310, 60), 120, 2.5), params(3, true), &closed);
        if (line.size() != 2 || closed) {
            return fail("a wobbly line must become 2 points, open");
        }
        if (line.first() != QPointF(10, 20) || line.last() != QPointF(310, 60)) {
            return fail("the line must keep its ends");
        }

        // A sharp square: the smoothing of the renderer must not overshoot the corners.
        const QVector<QPointF> square{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
        const QVector<QPointF> sharp = ShapeCleaner::polyline(square, true, 0.0, 3.0);
        if (sharp.first() != QPointF(0, 50) || sharp.last() != QPointF(0, 50)) {
            return fail("a closed outline must start and end in the middle of an edge");
        }
        if (sharpTurns(sharp, 60.0) != 4) {
            return fail("a sharp square must keep 4 corners");
        }
        const QRectF limit = QRectF(0, 0, 100, 100).adjusted(-2.0, -2.0, 2.0, 2.0);
        if (!limit.contains(drawnBounds(sharp, 3.0))) {
            return fail("the drawn sharp square goes past its corners");
        }
        const QVector<QPointF> round = ShapeCleaner::polyline(square, true, 9.0, 3.0);
        if (!limit.contains(drawnBounds(round, 3.0)) || sharpTurns(round, 60.0) != 0) {
            return fail("a rounded square must have no sharp corner and stay inside");
        }
        // An open polyline keeps its ends.
        const QVector<QPointF> ell = ShapeCleaner::polyline({{0, 0}, {50, 0}, {50, 80}}, false, 0.0, 2.0);
        if (ell.first() != QPointF(0, 0) || ell.last() != QPointF(50, 80) || sharpTurns(ell, 60.0) != 1) {
            return fail("an open L must keep its ends and its one corner");
        }
        return true;
    }

    static bool testSquareClosesWithFourCorners()
    {
        QVector<QPointF> raw = wobbly(QPointF(100, 100), QPointF(300, 102), 60, 2.0);
        raw += wobbly(QPointF(300, 102), QPointF(298, 300), 60, 2.0).mid(1);
        raw += wobbly(QPointF(298, 300), QPointF(101, 297), 60, 2.0).mid(1);
        raw += wobbly(QPointF(101, 297), QPointF(103, 106), 60, 2.0).mid(1);   // ends short of the start
        bool closed = false;
        const QVector<QPointF> sharp = ShapeCleaner::clean(raw, params(3, false), &closed);
        if (!closed) {
            return fail("a square whose end is near its start must close");
        }
        if (sharpTurns(sharp, 60.0) != 4) {
            printf("(%d corners) ", sharpTurns(sharp, 60.0));
            return fail("a wobbly square must give 4 corners");
        }
        if (sharp.first() != sharp.last()) {
            return fail("a closed result must end where it starts");
        }
        const QRectF limit = QRectF(100, 100, 200, 200).adjusted(-6.0, -6.0, 6.0, 6.0);
        if (!limit.contains(drawnBounds(sharp, 3.0))) {
            return fail("the cleaned square is drawn past its corners");
        }
        const QVector<QPointF> round = ShapeCleaner::clean(raw, params(3, true), &closed);
        if (!closed || sharpTurns(round, 60.0) != 0 || !limit.contains(drawnBounds(round, 3.0))) {
            return fail("rounded corners must leave no sharp turn");
        }
        return true;
    }

    /// Corners kept by a cleaned polygon (Edit points), true round corners,
    /// the rounding steps, and the line stopping inside a filled arrow head.
    static bool testCornersAndArrowTips()
    {
        const QVector<QPointF> square{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
        QVector<int> joints;
        const QVector<QPointF> round = ShapeCleaner::polyline(square, true, 9.0, 3.0, &joints);
        QVector<QPointF> corners = ShapeCleaner::jointCorners(round, joints);
        if (joints.size() != 8 || corners.size() != 4) {
            return fail("a rounded square must keep its 4 corners");
        }
        for (int j = 0; j < 4; ++j) {
            if (!samePoint(corners[j], square[j])) {
                return fail("the kept corners must be where the sides meet");
            }
        }
        // A true arc: every point of the top right corner is 9 from its centre.
        for (int i = joints[2]; i <= joints[3]; ++i) {
            if (qAbs(QLineF(round[i], QPointF(91, 9)).length() - 9.0) > 0.01) {
                return fail("a round corner must be a circular arc tangent to both sides");
            }
        }
        // Turned and stretched, the corners still come back.
        QTransform t;
        t.rotate(30);
        t.scale(2.0, 1.0);
        QVector<QPointF> turned;
        for (const QPointF& p : round) turned << t.map(p);
        corners = ShapeCleaner::jointCorners(turned, joints);
        if (corners.size() != 4 || !samePoint(corners[2], t.map(square[2]))) {
            return fail("turned and stretched, the corners must still be found");
        }
        // Rounding steps read back from the drawn corners.
        for (int step = 0; step <= 3; ++step) {
            QVector<int> j;
            const QVector<QPointF> p = ShapeCleaner::polyline(square, true, ShapeCleaner::stepRadius(step, 2.0), 2.0, &j);
            if (ShapeCleaner::roundingStep(p, j, 2.0) != step) {
                return fail("each rounding step must be read back");
            }
        }
        if (!ShapeCleaner::jointCorners(round, {1, 2}).isEmpty() || !ShapeCleaner::jointCorners(round, {}).isEmpty()) {
            return fail("joints that do not fit must give no corners");
        }
        // A rough square drawn by hand keeps its corners, in the file too.
        QVector<QPointF> raw = wobbly(QPointF(100, 100), QPointF(300, 102), 60, 2.0);
        raw += wobbly(QPointF(300, 102), QPointF(298, 300), 60, 2.0).mid(1);
        raw += wobbly(QPointF(298, 300), QPointF(101, 297), 60, 2.0).mid(1);
        raw += wobbly(QPointF(101, 297), QPointF(103, 106), 60, 2.0).mid(1);
        bool closed = false;
        QVector<int> cleanedJoints;
        const QVector<QPointF> cleaned = ShapeCleaner::clean(raw, params(3, true), &closed, &cleanedJoints);
        if (!closed || ShapeCleaner::jointCorners(cleaned, cleanedJoints).size() != 4) {
            return fail("a cleaned square must keep its 4 corners");
        }
        VectorStroke stroke;
        for (const QPointF& p : cleaned) {
            StrokePoint sp;
            sp.pos = p;
            stroke.points << sp;
        }
        stroke.cornerJoints = cleanedJoints;
        if (VectorStroke::fromJson(stroke.toJson()).cornerJoints != cleanedJoints) {
            return fail("a stroke must save its corners");
        }
        stroke.cornerJoints.clear();
        if (stroke.toJson().contains("corners")) {
            return fail("a stroke without corners must not write the key");
        }
        VectorStroke piece = stroke;
        piece.cornerJoints = cleanedJoints;
        piece.points = stroke.points.mid(3);
        piece.keepStyleAsSlice(stroke.points, 3, piece.points.size());
        if (!piece.cornerJoints.isEmpty()) {
            return fail("a piece of a shape must drop its corners");
        }

        // Filled head: the line stops inside it; open head: the line ends at the tip.
        VectorStroke arrow;
        arrow.baseThickness = 6.0;
        for (const QPointF& p : {QPointF(0, 0), QPointF(60, 0), QPointF(120, 0), QPointF(150, 20)}) {
            StrokePoint sp;
            sp.pos = p;
            arrow.points << sp;
        }
        arrow.arrowEnd = StrokeStyle::Filled;
        StrokePolygonResult filled = VectorLayer::buildStrokePolygon(arrow);
        const qreal back = QLineF(filled.endCapCenter, QPointF(150, 20)).length();
        if (!filled.hasRoundCaps || back < 0.4 * StrokeStyle::arrowLength(6.0)
            || back > 0.6 * StrokeStyle::arrowLength(6.0) || filled.pieces.size() != 1) {
            return fail("the line must stop inside a filled head");
        }
        const QPolygonF head = filled.pieces.first();
        for (const QPointF& p : filled.polygon) {
            if (QLineF(p, QPointF(150, 20)).length() < 0.3 * StrokeStyle::arrowLength(6.0)
                && !head.containsPoint(p, Qt::WindingFill)) {
                return fail("nothing of the line may stick out of a filled head");
            }
        }
        arrow.arrowEnd = StrokeStyle::Open;
        const StrokePolygonResult open = VectorLayer::buildStrokePolygon(arrow);
        if (!samePoint(open.endCapCenter, QPointF(150, 20))) {
            return fail("the line must end at the tip of an open head");
        }

        // SN_TEST_SHOTS=<folder>: thick and thin, filled and open, a hand-drawn
        // shaft that bends into its head, and a connector, as the renderer draws them.
        const QString shots = qEnvironmentVariable("SN_TEST_SHOTS");
        if (!shots.isEmpty()) {
            QImage img(1000, 520, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::white);
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing, true);
            int row = 0;
            for (const qreal width : {14.0, 3.0}) {
                int col = 0;
                for (const quint8 head : {quint8(StrokeStyle::Filled), quint8(StrokeStyle::Open)}) {
                    VectorStroke s;
                    s.color = Qt::black;
                    s.baseThickness = width;
                    s.arrowEnd = head;
                    const QPointF at(30 + col * 480, 60 + row * 170);
                    for (const QPointF& q : {QPointF(0, 90), QPointF(120, 60), QPointF(260, 40), QPointF(330, 10),
                                             QPointF(352, 0)}) {
                        StrokePoint sp;
                        sp.pos = at + q;
                        s.points << sp;
                    }
                    s.updateBoundingBox();
                    VectorLayer::renderStroke(p, s);
                    ++col;
                }
                ++row;
            }
            ConnectorObject line;
            line.lineWidth = 10;
            line.color = Qt::black;
            line.arrowEnd = StrokeStyle::Filled;
            line.arrowStart = StrokeStyle::Open;
            line.startDir = ConnectorObject::sideOf(QPointF(840, -70));   // As the shape tool sets them
            line.endDir = (line.startDir + 2) % 4;
            line.setEnds(QPointF(60, 470), QPointF(900, 400));
            VectorLayer::renderStroke(p, line.outlineStroke());
            p.end();
            img.save(shots + QLatin1String("/a4_arrows.png"));
        }
        return true;
    }

    static bool testCurvesStaySmooth()
    {
        // A circle drawn with a wobble, ending a little past its start.
        QVector<QPointF> circle;
        for (int i = 0; i <= 130; ++i) {
            const qreal a = i * 2.0 * M_PI / 120.0;
            const qreal r = 80.0 + 2.0 * std::sin(i * 0.25);
            circle << QPointF(200 + r * std::cos(a), 200 + r * std::sin(a));
        }
        bool closed = false;
        const QVector<QPointF> round = ShapeCleaner::clean(circle, params(3, true), &closed);
        if (!closed || sharpTurns(round, 25.0) != 0) {
            return fail("a circle must close and stay smooth");
        }
        // A 2:1 ellipse keeps round ends: a tight curve is not a corner.
        QVector<QPointF> ellipse;
        for (int i = 0; i <= 120; ++i) {
            const qreal a = i * 2.0 * M_PI / 120.0;
            ellipse << QPointF(200 + 120 * std::cos(a), 200 + 60 * std::sin(a));
        }
        if (sharpTurns(ShapeCleaner::clean(ellipse, params(3, false), &closed), 25.0) != 0 || !closed) {
            return fail("an ellipse must not get pointed ends");
        }
        // A spiral ends far from its start: it stays an open smooth curve.
        // (A much tighter start, under about a tenth of its size, can get a corner.)
        QVector<QPointF> spiral;
        for (int i = 0; i <= 200; ++i) {
            const qreal a = i * 4.0 * M_PI / 200.0;
            const qreal r = 40.0 + 10.0 * a;
            spiral << QPointF(300 + r * std::cos(a), 300 + r * std::sin(a));
        }
        const QVector<QPointF> s = ShapeCleaner::clean(spiral, params(2, true), &closed);
        if (closed || sharpTurns(s, 25.0) != 0) {
            return fail("a spiral must stay open and smooth");
        }
        if (s.first() != spiral.first() || s.last() != spiral.last()) {
            return fail("an open curve must keep its ends");
        }
        return true;
    }

    static bool testArrowHeads()
    {
        const QVector<QPointF> shaft = wobbly(QPointF(0, 0), QPointF(200, 0), 80, 1.0);
        const auto vee = [](QPointF w1, QPointF tip, QPointF w2) {
            QVector<QPointF> v = wobbly(w1, tip, 20, 0.8);
            v += wobbly(tip, w2, 20, 0.8).mid(1);
            return v;
        };
        ShapeCleaner::ArrowMatch m = ShapeCleaner::detectArrowHead(
            shaft, vee(QPointF(184, -12), QPointF(201, 1), QPointF(185, 13)), 2.0, 1.0);
        if (m.arrow != StrokeStyle::Open || !m.atEnd) {
            return fail("a V at the end must be an open arrow at the end");
        }
        m = ShapeCleaner::detectArrowHead(shaft, vee(QPointF(16, -12), QPointF(-1, 0), QPointF(15, 12)), 2.0, 1.0);
        if (m.arrow != StrokeStyle::Open || m.atEnd) {
            return fail("a V at the start must be an open arrow at the start");
        }
        QVector<QPointF> triangle = vee(QPointF(184, -10), QPointF(200, 0), QPointF(184, 10));
        triangle += wobbly(QPointF(184, 10), QPointF(184, -9), 15, 0.8).mid(1);
        m = ShapeCleaner::detectArrowHead(shaft, triangle, 2.0, 1.0);
        if (m.arrow != StrokeStyle::Filled || !m.atEnd) {
            return fail("a triangle at the end must be a filled arrow");
        }
        if (ShapeCleaner::detectArrowHead(shaft, vee(QPointF(84, 68), QPointF(100, 80), QPointF(84, 92)), 2.0, 1.0).arrow
            != StrokeStyle::None) {
            return fail("a V far from both ends is no arrow head");
        }
        if (ShapeCleaner::detectArrowHead(shaft, vee(QPointF(216, -12), QPointF(200, 0), QPointF(216, 12)), 2.0, 1.0).arrow
            != StrokeStyle::None) {
            return fail("a V opening forward is no arrow head");
        }
        // "lv": a letter v written just after a vertical line, its tip near the line's end.
        const QVector<QPointF> ell = wobbly(QPointF(0, 0), QPointF(0, 60), 30, 0.5);
        if (ShapeCleaner::detectArrowHead(ell, vee(QPointF(6, 46), QPointF(9, 60), QPointF(14, 45)), 2.0, 1.0).arrow
            != StrokeStyle::None) {
            return fail("a v beside the end of a line is no arrow head");
        }
        // A curved shaft: the head is judged against the direction at its end.
        QVector<QPointF> arc;
        for (int i = 0; i <= 60; ++i) {
            const qreal a = i * M_PI / 2.0 / 60.0;
            arc << QPointF(150 * std::sin(a), 150 - 150 * std::cos(a));   // ends going down at (150, 150)
        }
        m = ShapeCleaner::detectArrowHead(arc, vee(QPointF(138, 134), QPointF(150, 151), QPointF(162, 135)), 2.0, 1.0);
        if (m.arrow != StrokeStyle::Open || !m.atEnd) {
            return fail("a V at the end of a curve must be an arrow head");
        }
        return true;
    }

    static bool testSnapping()
    {
        QPolygonF square;
        square << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 100) << QPointF(0, 100) << QPointF(0, 0);
        const QVector<QPolygonF> targets{square};

        QVector<QPointF> toCorner{{50, -60}, {103, 2}};
        ShapeCleaner::snapEnds(toCorner, targets, 10.0);
        if (toCorner.last() != QPointF(100, 0) || toCorner.first() != QPointF(50, -60)) {
            return fail("an end near a corner must snap onto it, the far end must stay");
        }
        QVector<QPointF> toEdge{{50, -60}, {60, 4}};
        ShapeCleaner::snapEnds(toEdge, targets, 10.0);
        if (toEdge.last() != QPointF(60, 0)) {
            return fail("an end near an edge must snap onto it");
        }
        QVector<QPointF> away{{50, -60}, {60, -30}};
        ShapeCleaner::snapEnds(away, targets, 10.0);
        if (away.last() != QPointF(60, -30)) {
            return fail("an end far from every line must stay");
        }
        // Through clean(): a held line that ends near the corner meets it.
        ShapeCleaner::Params p = params(2, true);
        p.snapTargets = &targets;
        p.snapDist = 10.0;
        const QVector<QPointF> line = ShapeCleaner::clean(wobbly(QPointF(-150, -80), QPointF(-4, 3), 80, 2.0), p);
        if (line.last() != QPointF(0, 0) || line.size() != 2) {
            return fail("a cleaned line must snap its end onto the corner");
        }
        return true;
    }
    static bool samePoint(const QPointF& a, const QPointF& b) { return QLineF(a, b).length() < 0.01; }

    /// A hand-drawn closed outline through @p corners: wobbly sides, a little past the start.
    static QVector<QPointF> handPolygon(const QVector<QPointF>& corners)
    {
        QVector<QPointF> out;
        for (int i = 0; i < corners.size(); ++i) {
            const QVector<QPointF> side = wobbly(corners[i], corners[(i + 1) % corners.size()], 50, 1.5);
            out += i == 0 ? side : side.mid(1);
        }
        out << corners.first() + (corners[1] - corners[0]) * 0.04;
        return out;
    }

    /// The sharp corners of a cleaned outline (turns of 50 degrees or more).
    static QVector<QPointF> cornersOf(const QVector<QPointF>& p)
    {
        QVector<QPointF> out;
        for (int i = 1; i + 1 < p.size(); ++i) {
            const QPointF u = p[i] - p[i - 1];
            const QPointF v = p[i + 1] - p[i];
            if (qAbs(qRadiansToDegrees(std::atan2(u.x() * v.y() - u.y() * v.x(), QPointF::dotProduct(u, v)))) >= 50.0) {
                out << p[i];
            }
        }
        return out;
    }

    static qreal angleDeg(const QPointF& d) { return qRadiansToDegrees(std::atan2(d.y(), d.x())); }

    /// Angle between two lines, 0..90.
    static qreal between(const QPointF& u, const QPointF& v)
    {
        const qreal a = std::fmod(qAbs(angleDeg(u) - angleDeg(v)), 180.0);
        return qMin(a, 180.0 - a);
    }

    static QPointF turned(QPointF c, qreal x, qreal y, qreal deg)
    {
        const qreal r = qDegreesToRadians(deg);
        return c + QPointF(x * std::cos(r) - y * std::sin(r), x * std::sin(r) + y * std::cos(r));
    }

    static bool testExactShapes()
    {
        bool closed = false;
        // A rough circle becomes a circle: every point at one distance from its centre.
        QVector<QPointF> circle;
        for (int i = 0; i <= 64; ++i) {
            const qreal a = i * 2.0 * M_PI / 60.0;
            const qreal r = 80.0 + 4.0 * std::sin(i * 0.9) + 3.0 * std::cos(a);
            circle << QPointF(200 + r * std::cos(a), 200 + 0.94 * r * std::sin(a));
        }
        QVector<QPointF> c = ShapeCleaner::clean(circle, params(2, true), &closed);
        const QPointF mid = ShapeCleaner::centre(c);
        qreal lo = 1e9, hi = 0;
        for (const QPointF& q : c) {
            lo = qMin(lo, QLineF(q, mid).length());
            hi = qMax(hi, QLineF(q, mid).length());
        }
        if (!closed || c.first() != c.last() || hi - lo > 0.01 * hi || c.size() < 48) {
            printf("(radius %.2f..%.2f, %d points) ", lo, hi, int(c.size()));
            return fail("a rough circle must become an exact circle");
        }

        // A rough ellipse turned 30 degrees stays an ellipse turned 30 degrees.
        QVector<QPointF> oval;
        for (int i = 0; i <= 66; ++i) {
            const qreal a = i * 2.0 * M_PI / 62.0;
            oval << turned(QPointF(300, 300), (120 + 3 * std::sin(i * 1.3)) * std::cos(a), 55 * std::sin(a), 30);
        }
        c = ShapeCleaner::clean(oval, params(2, true), &closed);
        const QPointF oc = ShapeCleaner::centre(c);
        qreal farthest = 0, nearest = 1e9;
        QPointF farPt;
        for (const QPointF& q : c) {
            const qreal d = QLineF(q, oc).length();
            if (d > farthest) {
                farthest = d;
                farPt = q;
            }
            nearest = qMin(nearest, d);
        }
        const qreal axis = std::fmod(angleDeg(farPt - oc) + 360.0, 180.0);
        if (!closed || qAbs(axis - 30.0) > 3.0 || farthest / nearest < 1.9 || farthest / nearest > 2.5
            || sharpTurns(c, 25.0) != 0) {
            printf("(axis %.1f, ratio %.2f) ", axis, farthest / nearest);
            return fail("a tilted oval must become a tilted ellipse");
        }

        // A box turned 25 degrees: a rectangle turned 25 degrees, sharp corners.
        const QPointF bc(400, 300);
        c = ShapeCleaner::clean(handPolygon({turned(bc, -100, -45, 25), turned(bc, 100, -45, 25),
                                             turned(bc, 102, 47, 25), turned(bc, -98, 44, 25)}),
                                params(2, false), &closed);
        QVector<QPointF> k = cornersOf(c);
        if (!closed || k.size() != 4 || qAbs(between(k[1] - k[0], k[2] - k[1]) - 90.0) > 0.01
            || between(k[1] - k[0], k[3] - k[2]) > 0.01 || qAbs(between(k[1] - k[0], QPointF(1, 0)) - 25.0) > 2.0) {
            printf("(%d corners) ", int(k.size()));
            return fail("a tilted box must become a tilted rectangle");
        }
        // Nearly square and nearly level: an exact square on the page axes.
        c = ShapeCleaner::clean(handPolygon({turned(bc, -60, -58, 3), turned(bc, 60, -60, 3),
                                             turned(bc, 61, 60, 3), turned(bc, -58, 59, 3)}),
                                params(2, false), &closed);
        k = cornersOf(c);
        if (k.size() != 4 || qAbs(QLineF(k[0], k[1]).length() - QLineF(k[1], k[2]).length()) > 0.01
            || between(k[1] - k[0], QPointF(1, 0)) > 0.01) {
            return fail("a rough square near level must become a level square");
        }
        // A slanted box: a level parallelogram (opposite sides parallel, no right angle).
        c = ShapeCleaner::clean(handPolygon({{100, 100}, {300, 102}, {360, 220}, {158, 218}}),
                                params(2, false), &closed);
        k = cornersOf(c);
        if (k.size() != 4 || between(k[1] - k[0], k[3] - k[2]) > 0.01 || between(k[2] - k[1], k[0] - k[3]) > 0.01
            || qAbs(between(k[1] - k[0], k[2] - k[1]) - 90.0) < 20.0 || between(k[1] - k[0], QPointF(1, 0)) > 0.01) {
            return fail("a slanted box must become a level parallelogram");
        }
        // A diamond: a square standing on a corner.
        c = ShapeCleaner::clean(handPolygon({{200, 100}, {290, 192}, {201, 280}, {112, 190}}),
                                params(2, false), &closed);
        k = cornersOf(c);
        if (k.size() != 4 || qAbs(between(k[1] - k[0], QPointF(1, 0)) - 45.0) > 0.01
            || qAbs(QLineF(k[0], k[1]).length() - QLineF(k[1], k[2]).length()) > 0.01) {
            return fail("a rough diamond must become a square on its corner");
        }
        // A trapezoid gets two exactly parallel sides.
        c = ShapeCleaner::clean(handPolygon({{150, 100}, {250, 103}, {330, 220}, {70, 216}}),
                                params(2, false), &closed);
        k = cornersOf(c);
        if (k.size() != 4 || between(k[1] - k[0], k[3] - k[2]) > 0.01 || between(k[2] - k[1], k[0] - k[3]) < 20.0) {
            return fail("a rough trapezoid must get exactly parallel sides");
        }
        // A triangle and a pentagon: straight sides, sharp corners.
        c = ShapeCleaner::clean(handPolygon({{100, 300}, {200, 120}, {310, 298}}), params(2, false), &closed);
        k = cornersOf(c);
        if (!closed || k.size() != 3) {
            printf("(%d corners) ", int(k.size()));
            return fail("a rough triangle must become a triangle");
        }
        QVector<QPointF> five;
        for (int i = 0; i < 5; ++i) {
            five << turned(QPointF(300, 300), 0, -100, 72.0 * i + 10);
        }
        c = ShapeCleaner::clean(handPolygon(five), params(2, false), &closed);
        if (!closed || cornersOf(c).size() != 5) {
            printf("(%d corners) ", int(cornersOf(c).size()));
            return fail("a rough pentagon must keep 5 straight sides");
        }
        // 0 % keeps the drawn shape, only smoothed.
        ShapeCleaner::Params off = params(2, true);
        off.roughPercent = 0;
        c = ShapeCleaner::clean(circle, off, &closed);
        lo = 1e9;
        hi = 0;
        for (const QPointF& q : c) {
            lo = qMin(lo, QLineF(q, mid).length());
            hi = qMax(hi, QLineF(q, mid).length());
        }
        if (!closed || hi - lo < 0.02 * hi) {
            return fail("0 % must keep the drawn shape");
        }

        // An open arc becomes an exact arc through its ends.
        QVector<QPointF> arc;
        for (int i = 0; i <= 50; ++i) {
            const qreal a = qDegreesToRadians(200.0 + 120.0 * i / 50);
            const qreal r = 150.0 + 2.0 * std::sin(i * 0.7);
            arc << QPointF(500 + r * std::cos(a), 500 + r * std::sin(a));
        }
        c = ShapeCleaner::clean(arc, params(2, true), &closed);
        lo = 1e9;
        hi = 0;
        for (const QPointF& q : c) {
            lo = qMin(lo, QLineF(q, QPointF(500, 500)).length());
            hi = qMax(hi, QLineF(q, QPointF(500, 500)).length());
        }
        if (closed || hi - lo > 3.0 || QLineF(c.first(), arc.first()).length() > 4.0
            || QLineF(c.last(), arc.last()).length() > 4.0) {
            printf("(radius %.2f..%.2f) ", lo, hi);
            return fail("a rough arc must become an exact arc");
        }
        return true;
    }

    /// The tip index of the one-stroke arrow @p p, or -1.
    static int split(const QVector<QPointF>& p, int* arrow = nullptr)
    {
        int tip = -1;
        const ShapeCleaner::ArrowMatch m = ShapeCleaner::splitOneStrokeArrow(p, 2.0, 1.0, &tip);
        if (arrow) {
            *arrow = m.arrow;
        }
        return m.arrow == StrokeStyle::None ? -1 : tip;
    }

    static bool testQuickArrows()
    {
        // A fast pen gives few points: a slightly bent shaft, then sloppy heads as a second stroke.
        const QVector<QPointF> shaft{{0, 200}, {80, 160}, {170, 125}, {260, 100}};
        const auto head = [&](const QVector<QPointF>& h) {
            return ShapeCleaner::detectArrowHead(shaft, h, 2.0, 1.0);
        };
        struct Case {
            const char* name;
            QVector<QPointF> pts;
            int want;
        };
        const QVector<Case> heads{
            {"a quick V", {{238, 92}, {262, 99}, {244, 120}}, StrokeStyle::Open},
            {"a V that misses the tip", {{240, 86}, {255, 98}, {247, 117}}, StrokeStyle::Open},
            {"one barb from the tip", {{261, 101}, {240, 117}}, StrokeStyle::Open},
            {"one barb to the tip", {{243, 86}, {259, 99}}, StrokeStyle::Open},
            {"a hook", {{262, 99}, {249, 112}, {241, 113}, {238, 109}}, StrokeStyle::Open},
            {"a zigzag", {{240, 90}, {259, 101}, {238, 98}, {256, 103}, {242, 116}}, StrokeStyle::Open},
            {"a small closed triangle", {{261, 100}, {240, 89}, {244, 119}, {260, 101}}, StrokeStyle::Filled},
        };
        for (const Case& c : heads) {
            const ShapeCleaner::ArrowMatch m = head(c.pts);
            if (m.arrow != c.want || !m.atEnd) {
                printf("(%s: %d) ", c.name, m.arrow);
                return fail("a sloppy head at the end of a quick line must make an arrow");
            }
        }
        // Handwriting near a line is no head.
        if (ShapeCleaner::detectArrowHead({{0, 0}, {1, 30}, {0, 60}}, {{5, 44}, {9, 59}, {15, 43}}, 2.0, 1.0).arrow
            != StrokeStyle::None) {
            return fail("lv: a v beside the end of an l is no head");
        }
        if (head({{290, 80}, {310, 90}, {290, 100}}).arrow != StrokeStyle::None) {
            return fail("a > well past the end is no head");
        }
        if (head({{150, 150}, {170, 160}, {150, 170}}).arrow != StrokeStyle::None) {
            return fail("a > beside the middle of the line is no head");
        }
        if (head({{100, 0}, {300, 100}, {100, 200}}).arrow != StrokeStyle::None) {
            return fail("a head bigger than half the line is no head");
        }

        // One stroke: the line, then the head without lifting.
        int arrow = 0;
        const QVector<QPointF> line{{0, 300}, {150, 260}, {300, 240}};
        QVector<QPointF> p = line;
        p << QPointF(278, 233) << QPointF(299, 241) << QPointF(281, 255);   // back, tip, other barb
        if (split(p, &arrow) != 2 || arrow != StrokeStyle::Open) {
            return fail("a line with a back-and-forth V must split at its tip");
        }
        p = line;
        p << QPointF(276, 252);   // one barb back
        if (split(p) != 2) {
            return fail("a line with one barb back must be an arrow");
        }
        // The user's real gesture: a long narrow barb back along the line, back to the tip, a short one.
        p = line;
        p << QPointF(230, 240) << QPointF(298, 238) << QPointF(290, 250);
        if (split(p, &arrow) != 2 || arrow != StrokeStyle::Open) {
            return fail("a narrow back-and-forth head must make an open arrow");
        }
        p = line;
        p << QPointF(280, 228) << QPointF(282, 254) << QPointF(300, 241);   // closed triangle
        if (split(p, &arrow) != 2 || arrow != StrokeStyle::Filled) {
            return fail("a line ending in a closed triangle must make a filled arrow");
        }
        // Not arrows: a closed triangle, letters that turn back (N, W, h, n).
        if (split({{0, 100}, {50, 0}, {100, 100}, {2, 99}}) >= 0) {
            return fail("a closed triangle is no arrow");
        }
        if (split({{0, 100}, {0, 0}, {60, 100}, {60, 0}}) >= 0) {
            return fail("N is no arrow");
        }
        if (split({{0, 0}, {20, 100}, {40, 40}, {60, 100}, {80, 0}}) >= 0) {
            return fail("W is no arrow");
        }
        if (split({{0, 0}, {0, 100}, {0, 60}, {15, 50}, {30, 60}, {30, 100}}) >= 0) {
            return fail("h is no arrow");
        }
        if (split({{0, 40}, {0, 100}, {0, 55}, {15, 40}, {30, 55}, {30, 100}}) >= 0) {
            return fail("n is no arrow");
        }
        return true;
    }

    static bool testShapeObjects()
    {
        ShapeObject rect;
        rect.setBoundingRect(QRectF(10, 20, 100, 50));
        rect.lineWidth = 3;
        rect.lineStyle = StrokeStyle::Dashed;
        rect.roundness = 0;
        rect.color = QColor(0, 128, 0);
        const QJsonObject json = rect.toJson();
        if (json["kind"].toString() != "rect" || json["line"].toString() != "dash"
            || json["rounded"].toBool(true) || json.contains("arrowStart")) {
            return fail("a shape must save its kind, line style and sharp corners");
        }
        std::unique_ptr<InsertedObject> back = InsertedObject::fromJson(json);
        auto* loaded = dynamic_cast<ShapeObject*>(back.get());
        if (!loaded || loaded->kind != ShapeObject::Rect || loaded->lineStyle != StrokeStyle::Dashed
            || loaded->roundness != 0 || loaded->color != rect.color || loaded->lineWidth != 3
            || loaded->boundingRect() != rect.boundingRect()) {
            return fail("a shape must load back the same");
        }
        // A default shape writes no optional key.
        const QJsonObject plainJson = ShapeObject().toJson();
        if (plainJson.contains("line") || plainJson.contains("rounded") || plainJson.contains("roundness")
            || plainJson.contains("corners")) {
            return fail("a default shape must not write the optional keys");
        }

        // A large rounding and moved corners load back; a file without them reads as before.
        ShapeObject tri;
        tri.kind = ShapeObject::Triangle;
        tri.setBoundingRect(QRectF(0, 0, 100, 80));
        tri.rotation = 30;
        tri.roundness = 3;
        QVector<QPointF> c = tri.corners();
        const QPointF moved = c[1] + QPointF(40, 25);
        c[1] = moved;
        tri.setCorners(c);
        const QVector<QPointF> after = tri.corners();
        if (after.size() != 3 || !samePoint(after[1], moved) || !samePoint(after[0], c[0])
            || !samePoint(after[2], c[2])) {
            return fail("moving one corner of a turned triangle must move that corner only");
        }
        std::unique_ptr<InsertedObject> triBack = InsertedObject::fromJson(tri.toJson());
        auto* triLoaded = dynamic_cast<ShapeObject*>(triBack.get());
        if (!triLoaded || triLoaded->roundness != 3 || triLoaded->corners().size() != 3
            || !samePoint(triLoaded->corners()[1], moved)) {
            return fail("a shape must load back its rounding and its moved corners");
        }
        QJsonObject oldRound = ShapeObject().toJson();
        oldRound["rounded"] = false;
        std::unique_ptr<InsertedObject> oldBack = InsertedObject::fromJson(oldRound);
        auto* oldLoaded = dynamic_cast<ShapeObject*>(oldBack.get());
        if (!oldLoaded || oldLoaded->roundness != 0 || !oldLoaded->cornerPoints.isEmpty()
            || ShapeObject().roundness != 1) {
            return fail("an older shape must read its sharp or rounded corners as before");
        }

        // The outline is closed and stays on the box.
        const VectorStroke outline = rect.outlineStroke();
        if (outline.points.size() < 5 || !samePoint(outline.points.first().pos, outline.points.last().pos)) {
            return fail("a rectangle outline must be closed");
        }
        for (const StrokePoint& pt : outline.points) {
            if (!rect.boundingRect().adjusted(-0.01, -0.01, 0.01, 0.01).contains(pt.pos)) {
                return fail("a rectangle outline must stay on its box");
            }
        }
        for (int kind = ShapeObject::Rect; kind <= ShapeObject::RoundRect; ++kind) {
            ShapeObject s;
            s.kind = kind;
            s.setBoundingRect(QRectF(0, 0, 80, 60));
            if (s.outlineStroke().points.size() < 4
                || ShapeObject::kindFromName(ShapeObject::kindName(kind)) != kind) {
                return fail("every kind must have an outline and a name that reads back");
            }
        }

        // A turned shape turns its sides: the top faces right after a quarter turn.
        ShapeObject turned;
        turned.setBoundingRect(QRectF(0, 0, 100, 100));
        turned.rotation = 90;
        QPointF normal;
        const QPointF top = turned.anchor(ShapeObject::Top, &normal);
        if (!samePoint(top, QPointF(100, 50)) || ConnectorObject::sideOf(normal) != ShapeObject::Right) {
            return fail("the anchors must turn with the shape");
        }
        if (!turned.containsPoint(QPointF(95, 50)) || turned.containsPoint(QPointF(130, 50))) {
            return fail("the hit test must follow the turn");
        }

        // A page keeps both objects; a page without connectors says so.
        Page page;
        auto line = std::make_unique<ConnectorObject>();
        line->setEnds(QPointF(0, 0), QPointF(50, 50));
        page.addObject(std::make_unique<ShapeObject>(rect));
        if (page.hasConnectors) {
            return fail("a page with only a shape has no connector");
        }
        page.addObject(std::move(line));
        std::unique_ptr<Page> reloaded = Page::fromJson(page.toJson());
        if (!reloaded || reloaded->objects.size() != 2 || !reloaded->hasConnectors
            || reloaded->objects[0]->type() != "shape" || reloaded->objects[1]->type() != "connector") {
            return fail("a page must save and load its shapes and connectors");
        }
        const QString connectorId = reloaded->objects[1]->id;
        reloaded->removeObject(connectorId);
        if (reloaded->hasConnectors) {
            return fail("removing the last connector must clear hasConnectors");
        }
        return true;
    }

    static bool testConnectors()
    {
        // A free L: down first, then right. One bend, at (0, 80).
        ConnectorObject l;
        l.startDir = ShapeObject::Bottom;
        l.endDir = ShapeObject::Left;
        l.setEnds(QPointF(0, 0), QPointF(100, 80));
        const QVector<QPointF> r = l.route();
        if (!samePoint(r.first(), QPointF(0, 0)) || !samePoint(r.last(), QPointF(100, 80))
            || !samePoint(l.startPoint(), QPointF(0, 0)) || !samePoint(l.endPoint(), QPointF(100, 80))) {
            return fail("a connector must run from its start to its end");
        }
        if (!l.containsPoint(QPointF(0, 40)) || !l.containsPoint(QPointF(60, 80))
            || l.containsPoint(QPointF(50, 40))) {
            return fail("a right-angle connector must go down, then right");
        }

        // Move carries the ends.
        l.position += QPointF(10, 10);
        if (!samePoint(l.startPoint(), QPointF(10, 10)) || !samePoint(l.endPoint(), QPointF(110, 90))) {
            return fail("moving the connector must move its ends");
        }

        // Attached to a shape: it follows the shape.
        Page page;
        auto shape = std::make_unique<ShapeObject>();
        shape->setBoundingRect(QRectF(200, 0, 100, 100));
        ShapeObject* target = shape.get();
        page.addObject(std::move(shape));
        auto line = std::make_unique<ConnectorObject>();
        line->startDir = ShapeObject::Right;
        line->endId = target->id;
        line->endSide = ShapeObject::Left;
        line->endDir = ShapeObject::Left;
        line->arrowEnd = StrokeStyle::Filled;
        line->setEnds(QPointF(0, 50), QPointF(150, 50));
        ConnectorObject* c = line.get();
        page.addObject(std::move(line));
        ConnectorObject::resolve(page);
        // The filled head's tip stops on the outside of the shape's line (1 + 2 before it).
        if (!samePoint(c->endPoint(), QPointF(197, 50)) || !samePoint(c->startPoint(), QPointF(0, 50))) {
            return fail("an attached end must sit on its side");
        }
        target->moveBy(QPointF(0, 100));
        ConnectorObject::resolve(page);
        if (!samePoint(c->endPoint(), QPointF(197, 150)) || !samePoint(c->startPoint(), QPointF(0, 50))) {
            return fail("an attached end must follow the moved shape");
        }

        // The file keeps the attachment and the arrow; a curve has 25 points.
        std::unique_ptr<InsertedObject> back = InsertedObject::fromJson(c->toJson());
        auto* loaded = dynamic_cast<ConnectorObject*>(back.get());
        if (!loaded || loaded->endId != target->id || loaded->endSide != ShapeObject::Left
            || !loaded->startId.isEmpty() || loaded->arrowEnd != StrokeStyle::Filled
            || !samePoint(loaded->endPoint(), c->endPoint())) {
            return fail("a connector must load back the same");
        }
        loaded->kind = ConnectorObject::Curved;
        if (loaded->route().size() != 25) {
            return fail("a curved connector must have 25 points");
        }

        // A page copied to another notebook: the attached end follows its
        // shape's new id; an end whose shape did not come along is freed.
        auto src = Document::createNew("Connector source");
        Page* srcPage = src->page(0);
        auto box = std::make_unique<ShapeObject>();
        box->setBoundingRect(QRectF(200, 0, 100, 100));
        const QString boxId = box->id;
        srcPage->addObject(std::move(box));
        auto link = std::make_unique<ConnectorObject>();
        link->startId = boxId;
        link->startSide = ShapeObject::Right;
        link->endId = QStringLiteral("not-on-this-page");
        link->endSide = ShapeObject::Top;
        link->setEnds(QPointF(300, 50), QPointF(400, 50));
        const QString linkId = link->id;
        srcPage->addObject(std::move(link));
        auto dest = Document::createNew("Connector destination");
        const PageImportResult copy = dest->importPagesFrom(src.get(), {srcPage->uuid}, dest->pageCount());
        Page* copied = copy.destStartIndex >= 0 ? dest->page(copy.destStartIndex) : nullptr;
        auto* copiedLink = copied
            ? dynamic_cast<ConnectorObject*>(copied->objectById(copy.objectIdMap.value(linkId))) : nullptr;
        if (!copiedLink || copiedLink->startId.isEmpty() || copiedLink->startId != copy.objectIdMap.value(boxId)
            || !copiedLink->endId.isEmpty()) {
            return fail("a copied connector must follow its shape's new id and free a missing one");
        }
        return true;
    }

    /// @p route cuts the line of @p shape somewhere other than at its own ends.
    static bool cutsShape(const QVector<QPointF>& route, const ShapeObject& shape)
    {
        const QVector<StrokePoint>& o = shape.outline().points;
        for (int i = 1; i < route.size(); ++i) {
            for (int j = 1; j < o.size(); ++j) {
                const qreal t = ShapeObject::crossAt(route[i - 1], route[i], o[j - 1].pos, o[j].pos);
                const QPointF hit = route[i - 1] + (route[i] - route[i - 1]) * t;
                if (t >= 0 && QLineF(hit, route.first()).length() > 0.5 && QLineF(hit, route.last()).length() > 0.5) {
                    return true;
                }
            }
        }
        return false;
    }

    static bool testConnectorRoutes()
    {
        // A rounded rectangle and a diamond up to its right, as in the user's recording.
        Page page;
        auto box = std::make_unique<ShapeObject>();
        box->kind = ShapeObject::RoundRect;
        box->lineWidth = 4;
        box->setBoundingRect(QRectF(100, 200, 160, 96));
        ShapeObject* rect = box.get();
        page.addObject(std::move(box));
        auto gem = std::make_unique<ShapeObject>();
        gem->kind = ShapeObject::Diamond;
        gem->lineWidth = 4;
        gem->setBoundingRect(QRectF(400, 60, 110, 70));
        ShapeObject* diamond = gem.get();
        page.addObject(std::move(gem));

        // A diamond's side is a rounded corner: the anchor is on the drawn line, inside the box
        // (a true arc of radius 12 in a 65 degree corner: 10.35 inside).
        const QPointF tip = diamond->anchor(ShapeObject::Left);
        if (tip.x() <= 400.0 || tip.x() > 411.0 || qAbs(tip.y() - 95.0) > 0.01) {
            return fail("an anchor must sit on the drawn line");
        }

        const int sides[][2] = {{ShapeObject::Bottom, ShapeObject::Bottom}, {ShapeObject::Right, ShapeObject::Left},
                                {ShapeObject::Top, ShapeObject::Bottom}, {ShapeObject::Left, ShapeObject::Right},
                                {ShapeObject::Bottom, ShapeObject::Top}};
        for (int kind : {int(ConnectorObject::Elbow), int(ConnectorObject::Curved)}) {
            for (const auto& pair : sides) {
                auto link = std::make_unique<ConnectorObject>();
                link->kind = kind;
                link->startId = rect->id;
                link->startSide = pair[0];
                link->endId = diamond->id;
                link->endSide = pair[1];
                link->setEnds(QPointF(0, 0), QPointF(10, 10));
                ConnectorObject* c = link.get();
                page.addObject(std::move(link));
                ConnectorObject::resolve(page);
                const QVector<QPointF> r = c->route();
                if (!samePoint(c->startPoint(), rect->anchor(pair[0]))
                    || !samePoint(c->endPoint(), diamond->anchor(pair[1]))) {
                    return fail("an attached end must sit on the middle of its side");
                }
                if (cutsShape(r, *rect) || cutsShape(r, *diamond)) {
                    printf("(kind %d, sides %d-%d) ", kind, pair[0], pair[1]);
                    return fail("a connector must not run through the shapes it joins");
                }
                // It leaves each side outward.
                QPointF n;
                rect->anchor(pair[0], &n);
                if (QPointF::dotProduct(r[1] - r[0], n) <= 0) {
                    return fail("a connector must leave its shape outward");
                }
                page.removeObject(c->id);
            }
        }

        // Resizing the rectangle keeps its corner radius and its line.
        const VectorStroke small = rect->outlineStroke();
        rect->setBoundingRect(QRectF(100, 200, 320, 192));
        const VectorStroke big = rect->outlineStroke();
        if (big.baseThickness != small.baseThickness) {
            return fail("a resize must keep the line width");
        }
        const auto cornerDepth = [](const VectorStroke& s, const QPointF& corner) {
            qreal d = 1e9;
            for (const StrokePoint& pt : s.points) d = qMin(d, QLineF(pt.pos, corner).length());
            return d;
        };
        if (qAbs(cornerDepth(small, QPointF(100, 200)) - cornerDepth(big, QPointF(100, 200))) > 0.01) {
            return fail("a resize must keep the corner radius");
        }

        // A straight line is told from a curve.
        VectorStroke straight;
        for (int i = 0; i <= 6; ++i) {
            StrokePoint sp;
            sp.pos = QPointF(200, 50) * (i / 6.0);
            straight.points << sp;
        }
        VectorStroke bent = straight;
        bent.points[bent.points.size() / 2].pos += QPointF(0, 20);
        if (!straight.isStraightLine() || bent.isStraightLine()) {
            return fail("isStraightLine must tell a line from a bent stroke");
        }
        return true;
    }
};
