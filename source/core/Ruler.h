#pragma once

// ============================================================================
// Ruler - a straight edge on the page that the pen follows
// ============================================================================
// Kept in memory only: never saved in the note, no undo.
// The centre is in document coordinates, so the ruler stays on the page while
// the view scrolls. Its size is in screen pixels, so it keeps its size when the
// view zooms; the ticks follow the zoom and the user's unit (LengthUnit).
// Offsets below are screen pixels from the ruler's centre on screen.
// ============================================================================

#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QString>

class QPainter;

class Ruler {
public:
    static constexpr qreal LENGTH_PX = 1600.0;
    static constexpr qreal WIDTH_PX = 72.0;
    static constexpr qreal HANDLE_PX = 16.0;   ///< Radius of the rotate handle in the centre
    static constexpr qreal LABEL_W = 76.0;     ///< The angle label, just right of the handle
    static constexpr qreal LABEL_H = 22.0;

    enum Hit { None, Body, Handle, Label };

    QPointF centre;     ///< Document coordinates
    int angleDeg = 0;   ///< Whole degrees in -89..90; positive turns the right end up

    /// @p degrees brought into -89..90: the ruler looks the same turned half a turn.
    static int normalizedAngle(int degrees);
    /// Unit vector along the ruler (screen and document axes, y down).
    QPointF along() const;
    /// Unit vector across the ruler, towards its lower edge at 0 degrees.
    QPointF across() const;

    /// What a press at @p offset touches.
    Hit hitTest(const QPointF& offset) const;
    /// The long edge a line starting at @p offset follows: outside the ruler,
    /// within @p snapPx of an edge and alongside the ruler.
    /// @return +1 the edge on the across() side, -1 the other edge, 0 none.
    int edgeNear(const QPointF& offset, qreal snapPx) const;
    /// Box of the ruler on screen when its centre is at @p centreView.
    QRectF viewBounds(const QPointF& centreView) const;
    /// The angle as shown on the ruler: "30°" or "0.524 rad".
    QString angleText(bool radians) const;

    /// Draw the ruler with its centre at @p centreView; @p zoom spaces the ticks.
    void paint(QPainter& painter, const QPointF& centreView, qreal zoom, bool radians);

private:
    /// The body and its ticks, drawn once per zoom, unit and screen density.
    void rebuildCache(qreal zoom, qreal dpr);

    QPixmap m_cache;
    qreal m_cacheZoom = 0;
    qreal m_cacheDpr = 0;
    bool m_cacheMm = true;
};
