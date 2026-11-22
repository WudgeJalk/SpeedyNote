#pragma once

// ============================================================================
// ShapeCleaner - turns a hand-drawn line into a clean one
// ============================================================================
// Used by draw and hold, the free shape mode and hand-drawn arrows.
// Pure geometry: no widgets, no settings. All lengths are in the units of
// the points (page or document units).
// ============================================================================

#include <QPointF>
#include <QPolygonF>
#include <QVector>

namespace ShapeCleaner {

struct Params {
    qreal width = 2.0;            ///< Drawn width of the line
    qreal cornerAngleDeg = 35.0;  ///< A turn at least this sharp is a corner
    qreal closePercent = 12.0;    ///< Ends this close (% of the box diagonal) close the shape...
    qreal closeMinDist = 12.0;    ///< ...or at least this close
    bool rounded = true;          ///< Round corners (radius 3 x width)
    const QVector<QPolygonF>* snapTargets = nullptr;  ///< Lines the ends of an open result snap to
    qreal snapDist = 0.0;
    /// A line this close to an ellipse, a polygon or an arc (mean distance,
    /// % of its size) becomes exactly that shape; 0 = never.
    qreal roughPercent = 12.0;
    qreal equalPercent = 12.0;    ///< Lengths this close (% of the longer) are made equal: circle, square, rhombus
    qreal angleSnapDeg = 12.0;    ///< Sides this close to parallel or to square are made so
    /// Four sides with one parallel pair: the other pair must differ by more
    /// than this for a trapezoid, else it is a box (rectangle, parallelogram).
    qreal trapezoidDeg = 25.0;
    qreal levelDeg = 5.0;         ///< A clean shape this close to the page axes is made level
};

/**
 * @brief Points of straight edges through @p corners, with sharp or round corners.
 *
 * The points are spaced so the renderer's Catmull-Rom smoothing keeps the
 * edges straight and the corners where they are. A round corner is a circular
 * arc tangent to both edges. A closed outline starts and ends in the middle of
 * an edge, so its round caps meet on a straight part.
 * @param joints Out: for each corner, the index of its first and last point
 *        (the same one for a sharp corner). See jointCorners().
 */
QVector<QPointF> polyline(const QVector<QPointF>& corners, bool closed,
                          qreal cornerRadius, qreal width, QVector<int>* joints = nullptr);

/// Corner radius of a rounding step: 0 sharp, 1 small (the "Rounded corners"
/// of a stroke preset), 2 medium, 3 large. Fixed lengths: a resize keeps them.
qreal stepRadius(int step, qreal width);

/**
 * @brief The corners of a closed polyline() from its joints: a sharp corner is
 *        its point, a round one is where its two straight sides meet. Turning
 *        or stretching the points keeps this true. Empty when @p joints do not
 *        fit @p pts (an old file, or points changed since).
 */
QVector<QPointF> jointCorners(const QVector<QPointF>& pts, const QVector<int>& joints);

/// The corner radius that gives the corners of @p pts (the largest of its
/// corners; 0 sharp); -1 without valid joints.
qreal cornerRadius(const QVector<QPointF>& pts, const QVector<int>& joints, qreal width);

/**
 * @brief The cleaned line. A closed line becomes a perfect ellipse (a circle
 *        when its axes are nearly equal) or a polygon with straight sides (a
 *        square, rectangle, rhombus, parallelogram or trapezoid when it is
 *        nearly one). An open line becomes a straight line, an exact arc, or
 *        straight parts with corners and smooth curves. Closes only when the
 *        end is near the start.
 * @param closed Out: whether the result is a closed outline.
 * @param joints Out: the corners of an exact polygon (see polyline()); else empty.
 */
QVector<QPointF> clean(const QVector<QPointF>& raw, const Params& params, bool* closed = nullptr,
                       QVector<int>* joints = nullptr);

/// The centre of a closed outline (its area centroid; the box centre if it has no area).
QPointF centre(const QVector<QPointF>& loop);

/**
 * @brief Move each end of @p pts onto the nearest corner of @p targets within
 *        @p dist, else onto the nearest point of their lines. The move fades
 *        out over the points next to the end, so the line keeps its shape.
 */
void snapEnds(QVector<QPointF>& pts, const QVector<QPolygonF>& targets, qreal dist);

struct ArrowMatch {
    int arrow = 0;      ///< StrokeStyle::Arrow: None, Open or Filled
    bool atEnd = true;  ///< The head sits on the shaft's last point (else its first)
};

/**
 * @brief Is @p head a quick arrow head drawn at one end of @p shaft?
 *
 * A head is a small scribble at the end, mostly behind it: a V, a >, a hook,
 * one barb, a zigzag (Open), or a closed loop or triangle (Filled). It is
 * small next to the shaft. One barb counts only when it starts at the end.
 * @param width Drawn width of the shaft.
 * @param px    One screen pixel in the points' units (the slack of a quick hand).
 */
ArrowMatch detectArrowHead(const QVector<QPointF>& shaft, const QVector<QPointF>& head,
                           qreal width, qreal px);

/**
 * @brief A line that turns back at its end to draw its own head, in one stroke.
 * @param tip Out: index in @p raw of the tip; raw[0..tip] is the shaft.
 * @return The head (atEnd), or None when @p raw is not such an arrow.
 */
ArrowMatch splitOneStrokeArrow(const QVector<QPointF>& raw, qreal width, qreal px, int* tip);

} // namespace ShapeCleaner
