#pragma once

// ============================================================================
// StrokeStyleGeometry - the filled shapes of dashed and dotted lines and of
// arrow heads. Built only when a stroke cache is rebuilt, never per pen event.
// ============================================================================

#include "VectorStroke.h"

#include <QPointF>
#include <QPolygonF>
#include <QVector>

/**
 * @brief Filled outline of one stroke, shared by QPainter and the PDF export.
 * (Known in the code as VectorLayer::StrokePolygonResult.)
 *
 * Everything in it is filled with the stroke colour as one shape.
 */
struct StrokePolygonResult {
    QPolygonF polygon;              ///< The filled polygon outline (empty for dashed and dotted lines)
    bool isSinglePoint = false;     ///< True if stroke is just a dot
    bool hasRoundCaps = false;      ///< True if round end caps should be drawn
    QPointF startCapCenter;         ///< Center of start cap ellipse
    qreal startCapRadius = 0;       ///< Radius of start cap
    QPointF endCapCenter;           ///< Center of end cap ellipse
    qreal endCapRadius = 0;         ///< Radius of end cap
    QVector<QPolygonF> pieces;      ///< Dashes and arrow heads
    QVector<QPointF> discCenters;   ///< Dots, round dash ends and arrow joints
    QVector<qreal> discRadii;       ///< Radius of each disc in discCenters
};

namespace StrokeStyleGeometry {

/// True when the stroke is not a plain solid line (it needs addStyle).
inline bool isStyled(const VectorStroke& s)
{
    return s.lineStyle != StrokeStyle::Solid || s.arrowStart != StrokeStyle::None
           || s.arrowEnd != StrokeStyle::None;
}

/**
 * @brief Turn the solid ribbon into dashes or dots, and add the arrow heads.
 *
 * @param pts The smoothed points the ribbon was built from.
 * @param left, right Ribbon edges at each smoothed point.
 * @param halfWidths Ribbon half-width at each smoothed point.
 *
 * The pattern position is measured on the stored points (not the smoothed
 * ones), so a slice of a stroke with the right dashOffset gets exactly the
 * dashes of the whole stroke.
 */
void addStyle(const VectorStroke& stroke, const QVector<StrokePoint>& pts,
              const QVector<QPointF>& left, const QVector<QPointF>& right,
              const QVector<qreal>& halfWidths, StrokePolygonResult& result);

/// Only the arrow heads of a stroke (pieces and discs). The stroke being drawn
/// uses this to draw its heads each frame, outside the live cache.
StrokePolygonResult arrowHeads(const VectorStroke& stroke);

} // namespace StrokeStyleGeometry
