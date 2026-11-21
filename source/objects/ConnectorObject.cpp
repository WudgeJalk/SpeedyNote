#include "ConnectorObject.h"
#include "ShapeObject.h"
#include "../core/Page.h"
#include "../layers/VectorLayer.h"
#include "../strokes/ShapeCleaner.h"

#include <QPolygonF>
#include <QtMath>

#include <cmath>
#include <limits>

namespace {
QPointF dirOf(int side)
{
    switch (side) {
    case ShapeObject::Top:    return QPointF(0, -1);
    case ShapeObject::Right:  return QPointF(1, 0);
    case ShapeObject::Bottom: return QPointF(0, 1);
    default:                  return QPointF(-1, 0);
    }
}

qreal distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const qreal len2 = QPointF::dotProduct(ab, ab);
    const qreal t = len2 > 0 ? qBound(0.0, QPointF::dotProduct(p - a, ab) / len2, 1.0) : 0.0;
    return QLineF(p, a + ab * t).length();
}
} // namespace

int ConnectorObject::sideOf(const QPointF& v)
{
    if (qAbs(v.x()) >= qAbs(v.y())) return v.x() >= 0 ? ShapeObject::Right : ShapeObject::Left;
    return v.y() >= 0 ? ShapeObject::Bottom : ShapeObject::Top;
}

QPointF ConnectorObject::fromRel(const QPointF& rel) const
{
    const QPointF local = position + QPointF(rel.x() * size.width(), rel.y() * size.height());
    if (qAbs(rotation) < 0.01) return local;
    const qreal rad = qDegreesToRadians(rotation);
    const QPointF d = local - center();
    return center() + QPointF(d.x() * qCos(rad) - d.y() * qSin(rad), d.x() * qSin(rad) + d.y() * qCos(rad));
}

QPointF ConnectorObject::startPoint() const { return fromRel(startRel); }
QPointF ConnectorObject::endPoint() const { return fromRel(endRel); }
QVector<QPointF> ConnectorObject::route() const { return routeBetween(startPoint(), endPoint()); }

int ConnectorObject::crossings(const QVector<QPointF>& path, int from, int to) const
{
    int n = 0;
    for (const QPolygonF* shape : {&m_startShape, &m_endShape}) {
        for (int k = from + 1; k <= to && k < path.size(); ++k) {
            for (int j = 1; j < shape->size(); ++j) {
                n += ShapeObject::crossAt(path[k - 1], path[k], (*shape)[j - 1], (*shape)[j]) >= 0.0 ? 1 : 0;
            }
        }
    }
    return n;
}

QVector<QPointF> ConnectorObject::elbowCorners(const QPointF& s, const QPointF& e) const
{
    const QPointF ds = dirOf(startDir);
    const QPointF de = dirOf(endDir);
    // A stub out of each attached side (long enough for a straight arrow head).
    const qreal stub = qMax(12.0, 3 * lineWidth);
    const auto stubOf = [this, stub](quint8 arrow) {
        return arrow == StrokeStyle::None ? stub : StrokeStyle::arrowLength(lineWidth) + stub;
    };
    const QPointF a = s + ds * (startId.isEmpty() ? 0.0 : stubOf(arrowStart));
    const QPointF b = e + de * (endId.isEmpty() ? 0.0 : stubOf(arrowEnd));

    // Candidates: one or two bends, through the middle, in line with an end,
    // or around everything. The shortest that cuts no attached shape wins.
    const QRectF around = QRectF(a, b).normalized().united(m_startShape.boundingRect())
                              .united(m_endShape.boundingRect()).adjusted(-stub, -stub, stub, stub);
    const qreal xs[] = {(a.x() + b.x()) / 2, a.x(), b.x(), around.left(), around.right()};
    const qreal ys[] = {(a.y() + b.y()) / 2, a.y(), b.y(), around.top(), around.bottom()};
    QVector<QPointF> best;
    qreal bestScore = std::numeric_limits<qreal>::max();
    for (int i = 0; i < 10; ++i) {
        const QVector<QPointF> mid = i < 5
            ? QVector<QPointF>{a, QPointF(xs[i], a.y()), QPointF(xs[i], b.y()), b}
            : QVector<QPointF>{a, QPointF(a.x(), ys[i - 5]), QPointF(b.x(), ys[i - 5]), b};
        QVector<QPointF> c{s};
        for (const QPointF& p : mid + QVector<QPointF>{e}) {
            if (QLineF(c.last(), p).length() > 0.01) c << p;
        }
        if (c.size() < 2) c << e;
        qreal score = 1e7 * crossings(mid, 0, mid.size() - 1);
        for (int k = 1; k < c.size(); ++k) {
            const qreal len = QLineF(c[k - 1], c[k]).length();
            score += len;
            if (k + 1 < c.size()) {
                const qreal dot = QPointF::dotProduct(c[k] - c[k - 1], c[k + 1] - c[k]);
                score += dot < 0 ? 1e5                                                       // doubles back
                       : dot < 0.999 * len * QLineF(c[k], c[k + 1]).length() ? stub : 0.0;  // a bend
            }
        }
        // A free end leaves, and comes in, the way the hand drew it.
        const int n = c.size();
        if (startId.isEmpty() && QPointF::dotProduct(c[1] - c[0], ds) < 0.99 * QLineF(c[0], c[1]).length()) {
            score += 1e4;
        }
        if (endId.isEmpty() && QPointF::dotProduct(c[n - 2] - c[n - 1], de) < 0.99 * QLineF(c[n - 2], c[n - 1]).length()) {
            score += 1e4;
        }
        if (score < bestScore) {
            bestScore = score;
            best = c;
        }
    }
    return best;
}

QVector<QPointF> ConnectorObject::routeBetween(const QPointF& s, const QPointF& e) const
{
    if (kind == Curved) {
        const qreal k = qMax(24.0, 0.4 * QLineF(s, e).length());
        const QPointF c1 = s + dirOf(startDir) * k;
        const QPointF c2 = e + dirOf(endDir) * k;
        QVector<QPointF> pts;
        for (int i = 0; i <= 24; ++i) {
            const qreal t = i / 24.0;
            const qreal u = 1 - t;
            pts << u * u * u * s + 3 * u * u * t * c1 + 3 * u * t * t * c2 + t * t * t * e;
        }
        // A curve that would cut through its shapes takes the right-angle way, rounded off.
        if (crossings(pts, 1, pts.size() - 2) == 0) return pts;
        return ShapeCleaner::polyline(elbowCorners(s, e), false, 1e9, lineWidth);
    }
    return ShapeCleaner::polyline(elbowCorners(s, e), false, rounded ? qMax(8.0, 3 * lineWidth) : 0.0, lineWidth);
}

void ConnectorObject::setEnds(const QPointF& start, const QPointF& end)
{
    rotation = 0.0;
    QRectF box = QPolygonF(routeBetween(start, end)).boundingRect();
    // A straight line has no height (or width): give the box some, so both
    // ends keep a place inside it.
    if (box.width() < 1.0) box.adjust(-0.5, 0, 0.5, 0);
    if (box.height() < 1.0) box.adjust(0, -0.5, 0, 0.5);
    position = box.topLeft();
    size = box.size();
    const auto rel = [&box](const QPointF& p) {
        return QPointF((p.x() - box.left()) / box.width(), (p.y() - box.top()) / box.height());
    };
    startRel = rel(start);
    endRel = rel(end);
}

VectorStroke ConnectorObject::outlineStroke() const
{
    VectorStroke stroke;
    stroke.color = color;
    stroke.baseThickness = lineWidth;
    stroke.lineStyle = lineStyle;
    stroke.arrowStart = arrowStart;
    stroke.arrowEnd = arrowEnd;
    const QVector<QPointF> pts = route();
    stroke.points.reserve(pts.size());
    for (const QPointF& p : pts) {
        StrokePoint sp;
        sp.pos = p;
        stroke.points.append(sp);
    }
    stroke.updateBoundingBox();
    return stroke;
}

bool ConnectorObject::containsPoint(const QPointF& pt) const
{
    const qreal reach = lineWidth / 2 + 6;
    const QVector<QPointF> r = route();
    for (int i = 1; i < r.size(); ++i) {
        if (distanceToSegment(pt, r[i - 1], r[i]) <= reach) return true;
    }
    return false;
}

void ConnectorObject::render(QPainter& painter, qreal zoom) const
{
    Q_UNUSED(zoom);  // The painter is already scaled to page coordinates.
    const OutlineKey key{position, size, rotation, kind, color.rgba(), lineWidth, lineStyle,
                         arrowStart, arrowEnd, rounded, startRel, endRel, startDir, endDir,
                         startId.isEmpty(), endId.isEmpty(), StrokeStyle::generation(),
                         m_startShape.boundingRect(), m_endShape.boundingRect()};
    m_outline.render(painter, key, [this]() { return outlineStroke(); });
}

QPointF ConnectorObject::pointOn(bool start, const ShapeObject& shape, int side, int* dir) const
{
    QPointF normal;
    const QPointF a = shape.anchor(side, &normal);
    if (dir) *dir = sideOf(normal);
    // An arrow's tip stops on the outside of the shape's line, not in it.
    const quint8 arrow = start ? arrowStart : arrowEnd;
    const qreal len = std::hypot(normal.x(), normal.y());
    if (arrow == StrokeStyle::None || len <= 0.0) return a;
    return a + normal / len * (shape.lineWidth / 2 + (arrow == StrokeStyle::Filled ? lineWidth : lineWidth / 2));
}

namespace {
QPolygonF linePolygon(const ShapeObject& shape, const QPointF& origin)
{
    QPolygonF poly;
    for (const StrokePoint& pt : shape.outline().points) poly << pt.pos + origin;
    return poly;
}
} // namespace

bool ConnectorObject::follow(bool start, Page& page, QPointF& p)
{
    const QString& id = start ? startId : endId;
    QPolygonF& outline = start ? m_startShape : m_endShape;
    const QRectF before = outline.boundingRect();
    auto* shape = id.isEmpty() ? nullptr : dynamic_cast<ShapeObject*>(page.objectById(id));
    if (!shape) {   // A free end, or its shape was deleted: the end stays where it was
        outline.clear();
        return !before.isNull();
    }
    int& dir = start ? startDir : endDir;
    int d = dir;
    const QPointF a = pointOn(start, *shape, start ? startSide : endSide, &d);
    outline = linePolygon(*shape, QPointF());
    const bool changed = QLineF(a, p).length() > 0.01 || d != dir || outline.boundingRect() != before;
    p = a;
    dir = d;
    return changed;
}

void ConnectorObject::resolve(Page& page)
{
    for (const auto& owned : page.objects) {
        auto* c = dynamic_cast<ConnectorObject*>(owned.get());
        if (!c || (c->startId.isEmpty() && c->endId.isEmpty() && c->m_startShape.isEmpty()
                   && c->m_endShape.isEmpty())) {
            continue;
        }
        QPointF s = c->startPoint();
        QPointF e = c->endPoint();
        const bool startMoved = c->follow(true, page, s);
        const bool endMoved = c->follow(false, page, e);
        if (startMoved || endMoved) c->setEnds(s, e);
    }
}

void ConnectorObject::attachEnd(bool start, const Page& container, const QPointF& origin, qreal reach,
                                const QPointF& toward, QPointF& p)
{
    QString& id = start ? startId : endId;
    int& side = start ? startSide : endSide;
    QPolygonF& outline = start ? m_startShape : m_endShape;
    id.clear();
    outline.clear();
    const QPointF local = p - origin;
    const QPointF towardLocal = toward - origin;
    const ShapeObject* best = nullptr;
    int bestSide = 0;
    qreal bestScore = std::numeric_limits<qreal>::max();
    QPointF onLine;
    qreal onLineDist = reach;
    for (const auto& owned : container.objects) {
        if (const auto* shape = dynamic_cast<const ShapeObject*>(owned.get())) {
            int nearSide = 0;
            int facingSide = 0;
            qreal nearDist = std::numeric_limits<qreal>::max();
            qreal facing = -std::numeric_limits<qreal>::max();
            for (int s = ShapeObject::Top; s <= ShapeObject::Left; ++s) {
                QPointF normal;
                const qreal d = QLineF(shape->anchor(s, &normal), local).length();
                if (d < nearDist) {
                    nearDist = d;
                    nearSide = s;
                }
                const qreal f = QPointF::dotProduct(normal, towardLocal - shape->center());
                if (f > facing) {
                    facing = f;
                    facingSide = s;
                }
            }
            // On the shape: the side under the pen, or, deep inside, the side
            // facing the other end. Next to it: within reach.
            const bool inside = shape->containsPoint(local);
            if (!inside && nearDist > reach) continue;
            const qreal score = inside ? nearDist - 1e9 : nearDist;
            if (score < bestScore) {
                bestScore = score;
                best = shape;
                bestSide = inside && nearDist > reach ? facingSide : nearSide;
            }
        } else if (const auto* other = dynamic_cast<const ConnectorObject*>(owned.get())) {
            if (other == this) continue;
            const QVector<QPointF> r = other->route();
            for (int i = 1; i < r.size(); ++i) {
                const QPointF ab = r[i] - r[i - 1];
                const qreal len2 = QPointF::dotProduct(ab, ab);
                const qreal t = len2 > 0 ? qBound(0.0, QPointF::dotProduct(local - r[i - 1], ab) / len2, 1.0) : 0.0;
                const QPointF q = r[i - 1] + ab * t;
                const qreal d = QLineF(q, local).length();
                if (d < onLineDist) {
                    onLineDist = d;
                    onLine = q;
                }
            }
        }
    }
    if (best) {
        id = best->id;
        side = bestSide;
        p = pointOn(start, *best, bestSide, start ? &startDir : &endDir) + origin;
        outline = linePolygon(*best, origin);
    } else if (onLineDist < reach) {
        // shortcut: an end on another connector snaps onto it but does not follow it; attach if users ask.
        p = onLine + origin;
    }
}

QJsonObject ConnectorObject::toJson() const
{
    QJsonObject obj = InsertedObject::toJson();
    obj["kind"] = kind == Curved ? QStringLiteral("curved") : QStringLiteral("elbow");
    obj["color"] = color.name(QColor::HexArgb);
    obj["lineWidth"] = lineWidth;
    if (lineStyle != StrokeStyle::Solid) obj["line"] = StrokeStyle::lineStyleName(lineStyle);
    if (arrowStart != StrokeStyle::None) obj["arrowStart"] = StrokeStyle::arrowName(arrowStart);
    if (arrowEnd != StrokeStyle::None) obj["arrowEnd"] = StrokeStyle::arrowName(arrowEnd);
    if (!rounded) obj["rounded"] = false;
    obj["startX"] = startRel.x();
    obj["startY"] = startRel.y();
    obj["endX"] = endRel.x();
    obj["endY"] = endRel.y();
    obj["startDir"] = startDir;
    obj["endDir"] = endDir;
    if (!startId.isEmpty()) {
        obj["startId"] = startId;
        obj["startSide"] = startSide;
    }
    if (!endId.isEmpty()) {
        obj["endId"] = endId;
        obj["endSide"] = endSide;
    }
    return obj;
}

void ConnectorObject::loadFromJson(const QJsonObject& obj)
{
    InsertedObject::loadFromJson(obj);
    kind = obj["kind"].toString() == QLatin1String("curved") ? Curved : Elbow;
    color = QColor(obj["color"].toString(QStringLiteral("#ff000000")));
    lineWidth = qMax(0.1, obj["lineWidth"].toDouble(2.0));
    lineStyle = StrokeStyle::lineStyleFromName(obj["line"].toString());
    arrowStart = StrokeStyle::arrowFromName(obj["arrowStart"].toString());
    arrowEnd = StrokeStyle::arrowFromName(obj["arrowEnd"].toString());
    rounded = obj["rounded"].toBool(true);
    startRel = QPointF(obj["startX"].toDouble(0.0), obj["startY"].toDouble(0.0));
    endRel = QPointF(obj["endX"].toDouble(1.0), obj["endY"].toDouble(1.0));
    startDir = qBound(0, obj["startDir"].toInt(ShapeObject::Bottom), 3);
    endDir = qBound(0, obj["endDir"].toInt(ShapeObject::Top), 3);
    startId = obj["startId"].toString();
    endId = obj["endId"].toString();
    startSide = qBound(0, obj["startSide"].toInt(0), 3);
    endSide = qBound(0, obj["endSide"].toInt(0), 3);
}
