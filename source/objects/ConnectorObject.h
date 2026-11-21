#pragma once

// ============================================================================
// ConnectorObject - a line between two points or shape sides (type "connector")
// ============================================================================
// Two kinds: right angles with rounded corners (Elbow) and Curved. Each end is
// free or attached to a side of a ShapeObject on the same page; resolve()
// moves attached ends onto their sides, so the line follows a moved shape.
// An attached end sits on the middle of its side and the line leaves it
// outward, then goes around both shapes.
//
// The ends are stored as fractions of the box (position, size), so the
// object system's move and resize carry them without knowing about them.
// ============================================================================

#include "InsertedObject.h"
#include "OutlineCache.h"
#include "../strokes/VectorStroke.h"

#include <QColor>
#include <QPolygonF>
#include <tuple>

class Page;
class ShapeObject;

class ConnectorObject : public InsertedObject {
public:
    enum Kind : int { Elbow = 0, Curved };

    int kind = Elbow;
    QColor color = Qt::black;
    qreal lineWidth = 2.0;
    quint8 lineStyle = StrokeStyle::Solid;
    quint8 arrowStart = StrokeStyle::None;
    quint8 arrowEnd = StrokeStyle::None;
    bool rounded = true;

    QPointF startRel{0, 0}, endRel{1, 1};  ///< Ends as fractions of the box
    QString startId, endId;                ///< Attached shape (empty: a free end)
    int startSide = 0, endSide = 0;        ///< The attached shape's side (ShapeObject::Side)
    int startDir = 2, endDir = 0;          ///< Direction the line leaves each end in (a Side)

    void render(QPainter& painter, qreal zoom) const override;
    QString type() const override { return QStringLiteral("connector"); }
    QJsonObject toJson() const override;
    void loadFromJson(const QJsonObject& obj) override;
    bool containsPoint(const QPointF& pt) const override;

    QPointF startPoint() const;
    QPointF endPoint() const;
    /// Put the ends at @p start and @p end (page coordinates) and fit the box to the route.
    void setEnds(const QPointF& start, const QPointF& end);
    /// The drawn line, in page coordinates.
    QVector<QPointF> route() const;
    VectorStroke outlineStroke() const;

    /// The Side whose outward direction is nearest to @p v.
    static int sideOf(const QPointF& v);
    /// Move the attached ends of every connector of @p page onto their shapes.
    static void resolve(Page& page);
    /// Attach the start (@p start) or the end at @p p (@p container's coordinates
    /// plus @p origin) to a shape of @p container it is on or within @p reach of:
    /// the side nearest @p p, or, deep inside the shape, the side facing @p toward
    /// (the other end). Else snap @p p onto another connector's line. @p p
    /// becomes the end's point (on the shape's line).
    void attachEnd(bool start, const Page& container, const QPointF& origin, qreal reach,
                   const QPointF& toward, QPointF& p);

private:
    QPointF fromRel(const QPointF& rel) const;
    QVector<QPointF> routeBetween(const QPointF& s, const QPointF& e) const;
    /// Corners of the right-angle way from @p s to @p e around the attached shapes.
    QVector<QPointF> elbowCorners(const QPointF& s, const QPointF& e) const;
    /// How many times path[from..to] cuts the attached shapes' lines.
    int crossings(const QVector<QPointF>& path, int from, int to) const;
    /// Where an end attached to @p side of @p shape sits; @p dir gets the way it leaves.
    QPointF pointOn(bool start, const ShapeObject& shape, int side, int* dir) const;
    /// Put an attached end back on its shape. True when the end or the shape changed.
    bool follow(bool start, Page& page, QPointF& p);

    /// Lines of the attached shapes (page coordinates, not saved): the route goes around them.
    QPolygonF m_startShape, m_endShape;

    /// Everything outlineStroke() and its drawing depend on (an attached end adds a stub).
    using OutlineKey = std::tuple<QPointF, QSizeF, qreal, int, QRgb, qreal, quint8, quint8, quint8, bool,
                                  QPointF, QPointF, int, int, bool, bool, int, QRectF, QRectF>;
    mutable OutlineCache<OutlineKey> m_outline;
};
