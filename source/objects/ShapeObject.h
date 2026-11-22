#pragma once

// ============================================================================
// ShapeObject - a fixed shape placed by the shape tool (type "shape")
// ============================================================================
// Rectangle, ellipse, triangle, diamond or rounded rectangle. Move, resize,
// rotate and undo come from the object system. The outline is an ordinary
// stroke drawn by VectorLayer::renderStroke, so line styles look the same as
// on the pen.
// ============================================================================

#include "InsertedObject.h"
#include "OutlineCache.h"
#include "../strokes/VectorStroke.h"

#include <QColor>
#include <tuple>

class ShapeObject : public InsertedObject {
public:
    enum Kind : int { Rect = 0, Ellipse, Triangle, Diamond, RoundRect };
    /// Sides, also the four stub directions of a connector end.
    enum Side : int { Top = 0, Right, Bottom, Left };

    int kind = Rect;
    QColor color = Qt::black;
    qreal lineWidth = 2.0;
    quint8 lineStyle = StrokeStyle::Solid;
    /// Corners: 0 sharp, 1 small (the stroke preset's "Rounded corners"), 2 medium,
    /// 3 large (ShapeCleaner::stepRadius). A rounded rectangle has its own.
    int roundness = 1;
    /// Corner radius set with the panel's slider (page units); -1 = the step
    /// above. Saved as "cornerRadius" only when set; "roundness" keeps the
    /// nearest step, which is what older versions read.
    qreal cornerRadius = -1.0;
    /// Set cornerRadius, and roundness to its nearest step.
    void setCornerRadius(qreal radius);
    /// The radius the corners are drawn with (not for a rounded rectangle).
    qreal radius() const;
    /// Corners moved one by one ("Edit points"), as fractions of the box; empty
    /// = the corners of the kind. Never for an ellipse.
    QVector<QPointF> cornerPoints;

    void render(QPainter& painter, qreal zoom) const override;
    QString type() const override { return QStringLiteral("shape"); }
    QJsonObject toJson() const override;
    void loadFromJson(const QJsonObject& obj) override;
    bool containsPoint(const QPointF& pt) const override;

    /// The outline, in page coordinates with the rotation applied.
    VectorStroke outlineStroke() const;
    /// outlineStroke(), built once per change.
    const VectorStroke& outline() const;
    /// Middle of a side, on the drawn line (page coordinates); @p normal gets
    /// its outward direction, rotated with the shape.
    QPointF anchor(int side, QPointF* normal = nullptr) const;
    /// A point of the unrotated box, turned with the shape.
    QPointF toPage(const QPointF& local) const;
    /// The corners in page coordinates (rotation applied); empty for an ellipse.
    QVector<QPointF> corners() const;
    /// Move the corners (page coordinates, as many as corners() gives); the
    /// box is fitted again around them, in the shape's turned frame.
    void setCorners(const QVector<QPointF>& pageCorners);

    static QString kindName(int kind);
    static int kindFromName(const QString& name);
    /// Where segment ab meets segment cd, as a fraction of ab; -1 when they do not meet.
    static qreal crossAt(const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& d);

private:
    /// Everything outlineStroke() and its drawing depend on.
    using OutlineKey = std::tuple<QPointF, QSizeF, qreal, int, QRgb, qreal, quint8, int, QVector<QPointF>, int, qreal>;
    /// The corners in the unrotated box.
    QVector<QPointF> localCorners() const;
    OutlineKey outlineKey() const;
    mutable OutlineCache<OutlineKey> m_outline;
};
