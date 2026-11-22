#include "ShapeObject.h"
#include "../layers/VectorLayer.h"
#include "../strokes/ShapeCleaner.h"

#include <QJsonArray>
#include <QPolygonF>
#include <QtMath>

namespace {
const char* const kKindNames[] = {"rect", "ellipse", "triangle", "diamond", "roundRect"};
}

QString ShapeObject::kindName(int kind)
{
    return QLatin1String(kKindNames[qBound(0, kind, int(RoundRect))]);
}

int ShapeObject::kindFromName(const QString& name)
{
    for (int i = 0; i <= RoundRect; ++i) {
        if (name == QLatin1String(kKindNames[i])) return i;
    }
    return Rect;
}

QPointF ShapeObject::toPage(const QPointF& local) const
{
    const QPointF c = center();
    if (qAbs(rotation) < 0.01) return local;
    const qreal rad = qDegreesToRadians(rotation);
    const QPointF d = local - c;
    // Same turn as the selection handles (DocumentViewport::objectHandleAtPoint).
    return c + QPointF(d.x() * qCos(rad) - d.y() * qSin(rad), d.x() * qSin(rad) + d.y() * qCos(rad));
}

QVector<QPointF> ShapeObject::localCorners() const
{
    const QRectF r = boundingRect();
    QVector<QPointF> c;
    if (kind == Ellipse) {
        return c;
    }
    if (!cornerPoints.isEmpty()) {
        for (const QPointF& f : cornerPoints) {
            c << r.topLeft() + QPointF(f.x() * r.width(), f.y() * r.height());
        }
        return c;
    }
    switch (kind) {
    case Triangle:
        c << QPointF(r.center().x(), r.top()) << r.bottomRight() << r.bottomLeft();
        break;
    case Diamond:
        c << QPointF(r.center().x(), r.top()) << QPointF(r.right(), r.center().y())
          << QPointF(r.center().x(), r.bottom()) << QPointF(r.left(), r.center().y());
        break;
    default:
        c << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
        break;
    }
    return c;
}

QVector<QPointF> ShapeObject::corners() const
{
    QVector<QPointF> c = localCorners();
    for (QPointF& p : c) p = toPage(p);
    return c;
}

void ShapeObject::setCorners(const QVector<QPointF>& pageCorners)
{
    if (kind == Ellipse || pageCorners.size() < 3) return;
    // Into the turned frame (about the present centre), fit the box there,
    // then place it so each corner stays where it was asked to be.
    const QPointF c0 = center();
    const qreal rad = qDegreesToRadians(rotation);
    const qreal cs = qCos(rad);
    const qreal sn = qSin(rad);
    QVector<QPointF> local;
    for (const QPointF& p : pageCorners) {
        const QPointF d = p - c0;
        local << c0 + QPointF(d.x() * cs + d.y() * sn, -d.x() * sn + d.y() * cs);
    }
    const QRectF box = QPolygonF(local).boundingRect();
    const QSizeF fitted(qMax(box.width(), 1.0), qMax(box.height(), 1.0));
    const QPointF d = box.center() - c0;
    const QPointF centre = c0 + QPointF(d.x() * cs - d.y() * sn, d.x() * sn + d.y() * cs);
    position = centre - QPointF(fitted.width() / 2, fitted.height() / 2);
    size = fitted;
    cornerPoints.clear();
    for (const QPointF& p : local) {
        cornerPoints << QPointF((p.x() - box.center().x()) / fitted.width() + 0.5,
                                (p.y() - box.center().y()) / fitted.height() + 0.5);
    }
}

VectorStroke ShapeObject::outlineStroke() const
{
    const QRectF r = boundingRect();
    QVector<QPointF> pts;
    if (kind == Ellipse) {
        for (int i = 0; i <= 48; ++i) {
            const qreal a = 2 * M_PI * (i % 48) / 48.0;
            pts << toPage(r.center() + QPointF(r.width() / 2 * qCos(a), r.height() / 2 * qSin(a)));
        }
    } else {
        // A fixed radius: a resize must not change how round the corners are.
        pts = ShapeCleaner::polyline(corners(), true, kind == RoundRect ? qMax(16.0, 4 * lineWidth) : radius(),
                                     lineWidth);
    }

    VectorStroke stroke;
    stroke.color = color;
    stroke.baseThickness = lineWidth;
    stroke.lineStyle = lineStyle;
    stroke.points.reserve(pts.size());
    for (const QPointF& p : pts) {
        StrokePoint sp;
        sp.pos = p;
        stroke.points.append(sp);
    }
    stroke.updateBoundingBox();
    return stroke;
}

qreal ShapeObject::radius() const
{
    return cornerRadius >= 0.0 ? cornerRadius : ShapeCleaner::stepRadius(roundness, lineWidth);
}

void ShapeObject::setCornerRadius(qreal r)
{
    cornerRadius = qMax(0.0, r);
    roundness = 0;
    for (int step = 1; step <= 3; ++step) {
        if (qAbs(ShapeCleaner::stepRadius(step, lineWidth) - cornerRadius)
            < qAbs(ShapeCleaner::stepRadius(roundness, lineWidth) - cornerRadius)) {
            roundness = step;
        }
    }
}

qreal ShapeObject::crossAt(const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& d)
{
    const QPointF r = b - a;
    const QPointF s = d - c;
    const QPointF ac = c - a;
    const qreal den = r.x() * s.y() - r.y() * s.x();
    if (qAbs(den) < 1e-12) return -1.0;
    const qreal t = (ac.x() * s.y() - ac.y() * s.x()) / den;
    const qreal u = (ac.x() * r.y() - ac.y() * r.x()) / den;
    return t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0 ? t : -1.0;
}

QPointF ShapeObject::anchor(int side, QPointF* normal) const
{
    const QRectF r = boundingRect();
    QPointF n;
    qreal half = r.height() / 2;
    switch (side) {
    case Top:    n = QPointF(0, -1); break;
    case Right:  n = QPointF(1, 0);  half = r.width() / 2; break;
    case Bottom: n = QPointF(0, 1);  break;
    default:     n = QPointF(-1, 0); half = r.width() / 2; break;
    }
    const QPointF c = center();
    n = toPage(c + n) - c;
    if (normal) *normal = n;
    // Where a ray from the centre through the side's middle meets the drawn
    // line: a rounded corner (a diamond's, a triangle's top) is inside the box.
    const QPointF far = c + n * (r.width() + r.height() + 1.0);
    qreal best = -1.0;
    const QVector<StrokePoint>& pts = outline().points;
    for (int i = 1; i < pts.size(); ++i) {
        best = qMax(best, crossAt(c, far, pts[i - 1].pos, pts[i].pos));
    }
    return best >= 0.0 ? c + (far - c) * best : c + n * half;
}

bool ShapeObject::containsPoint(const QPointF& pt) const
{
    // Undo the turn, then test the box (with a margin for the line).
    const qreal rad = qDegreesToRadians(-rotation);
    const QPointF d = pt - center();
    const QPointF local = center() + QPointF(d.x() * qCos(rad) - d.y() * qSin(rad),
                                             d.x() * qSin(rad) + d.y() * qCos(rad));
    const qreal m = lineWidth / 2 + 4;
    return boundingRect().adjusted(-m, -m, m, m).contains(local);
}

ShapeObject::OutlineKey ShapeObject::outlineKey() const
{
    return OutlineKey{position, size, rotation, kind, color.rgba(), lineWidth, lineStyle, roundness,
                      cornerPoints, StrokeStyle::generation(), cornerRadius};
}

const VectorStroke& ShapeObject::outline() const
{
    return m_outline.get(outlineKey(), [this]() { return outlineStroke(); });
}

void ShapeObject::render(QPainter& painter, qreal zoom) const
{
    Q_UNUSED(zoom);  // The painter is already scaled to page coordinates.
    if (size.isEmpty()) return;
    m_outline.render(painter, outlineKey(), [this]() { return outlineStroke(); });
}

QJsonObject ShapeObject::toJson() const
{
    QJsonObject obj = InsertedObject::toJson();
    obj["kind"] = kindName(kind);
    obj["color"] = color.name(QColor::HexArgb);
    obj["lineWidth"] = lineWidth;
    if (lineStyle != StrokeStyle::Solid) obj["line"] = StrokeStyle::lineStyleName(lineStyle);
    // "rounded": false is what builds before the steps read as sharp.
    if (roundness == 0) obj["rounded"] = false;
    else if (roundness != 1) obj["roundness"] = roundness;
    if (cornerRadius >= 0.0) obj["cornerRadius"] = cornerRadius;
    if (!cornerPoints.isEmpty()) {
        QJsonArray points;
        for (const QPointF& p : cornerPoints) points << p.x() << p.y();
        obj["corners"] = points;
    }
    return obj;
}

void ShapeObject::loadFromJson(const QJsonObject& obj)
{
    InsertedObject::loadFromJson(obj);
    kind = kindFromName(obj["kind"].toString());
    color = QColor(obj["color"].toString(QStringLiteral("#ff000000")));
    lineWidth = qMax(0.1, obj["lineWidth"].toDouble(2.0));
    lineStyle = StrokeStyle::lineStyleFromName(obj["line"].toString());
    roundness = obj.contains("roundness") ? qBound(0, obj["roundness"].toInt(1), 3)
                                          : (obj["rounded"].toBool(true) ? 1 : 0);
    cornerRadius = obj.contains("cornerRadius") ? qMax(0.0, obj["cornerRadius"].toDouble()) : -1.0;
    cornerPoints.clear();
    const QJsonArray points = obj["corners"].toArray();
    for (int i = 0; i + 1 < points.size(); i += 2) {
        cornerPoints << QPointF(points[i].toDouble(), points[i + 1].toDouble());
    }
    if (cornerPoints.size() < 3 || kind == Ellipse) cornerPoints.clear();
}
