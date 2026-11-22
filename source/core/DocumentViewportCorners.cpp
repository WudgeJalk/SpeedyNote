// DocumentViewport members for the corners of a shape, kept out of
// DocumentViewport.cpp. "Edit points" in the selection panel shows a round
// handle on each corner of a selected cleaned polygon (lasso) or fixed shape
// (object) instead of the box. Dragging one moves that corner only; the two
// sides that meet there follow. The panel's roundness slider rebuilds the shape
// from its corners while it moves. One undo step each.

#include "DocumentViewport.h"
#include "../objects/ShapeObject.h"
#include "../strokes/ShapeCleaner.h"

#include <QLineF>
#include <QTransform>

#include <cmath>

namespace {

QVector<QPointF> positions(const VectorStroke& s)
{
    QVector<QPointF> p;
    p.reserve(s.points.size());
    for (const StrokePoint& pt : s.points) {
        p << pt.pos;
    }
    return p;
}

/// A cleaned shape has one width all along.
qreal shapeWidth(const VectorStroke& s)
{
    return s.widthAt(s.points.isEmpty() ? 1.0 : s.points.first().pressure);
}

/// A fixed shape with corners (not an ellipse).
ShapeObject* polygonShape(InsertedObject* obj)
{
    auto* shape = dynamic_cast<ShapeObject*>(obj);
    return shape && shape->kind != ShapeObject::Ellipse ? shape : nullptr;
}

/// The one selected fixed shape whose corners the slider rounds (a rounded
/// rectangle keeps its own).
ShapeObject* roundableShape(const QList<InsertedObject*>& selected)
{
    ShapeObject* shape = selected.size() == 1 ? polygonShape(selected.first()) : nullptr;
    return shape && shape->kind != ShapeObject::RoundRect ? shape : nullptr;
}

/// The radius at the slider's far end: half the shortest side.
qreal largestRadius(const QVector<QPointF>& c)
{
    qreal shortest = 0.0;
    for (int i = 0; i < c.size(); ++i) {
        const qreal side = QLineF(c[i], c[(i + 1) % c.size()]).length();
        shortest = i == 0 ? side : qMin(shortest, side);
    }
    return 0.5 * shortest;
}

/// The slider's place (0..1) for @p radius; squared back, so small radii get more of the slider.
qreal fractionOf(qreal radius, const QVector<QPointF>& corners)
{
    const qreal most = largestRadius(corners);
    return most > 0.0 ? std::sqrt(qBound(0.0, radius / most, 1.0)) : 0.0;
}

/// Dragging corner @p i onto a neighbour would leave a side of no length.
bool collapses(const QVector<QPointF>& c, int i, const QPointF& p)
{
    const int k = c.size();
    return QLineF(p, c[(i + k - 1) % k]).length() < 1.0 || QLineF(p, c[(i + 1) % k]).length() < 1.0;
}

} // namespace

bool DocumentViewport::lassoShapeCorners(QVector<QPointF>* corners) const
{
    if (m_lassoSelection.selectedStrokes.size() != 1
        || m_lassoSelection.selectedStrokes.first().cornerJoints.isEmpty()) {
        return false;
    }
    const VectorStroke& s = m_lassoSelection.selectedStrokes.first();
    QVector<QPointF> c = ShapeCleaner::jointCorners(positions(s), s.cornerJoints);
    if (c.isEmpty()) {
        return false;
    }
    if (corners) {
        const QTransform t = buildSelectionTransform();
        for (QPointF& p : c) {
            p = t.map(p);
        }
        *corners = c;
    }
    return true;
}

QVector<QPointF> DocumentViewport::objectCornersInViewport(const InsertedObject* obj) const
{
    const ShapeObject* shape = polygonShape(const_cast<InsertedObject*>(obj));
    if (!shape) {
        return {};
    }
    // Page coordinates to the viewport, as connectorEnds does.
    const QRectF box = objectBoundsInViewport(const_cast<InsertedObject*>(obj));
    QVector<QPointF> c = shape->corners();
    for (QPointF& p : c) {
        p = box.topLeft() + (p - obj->position) * m_zoomLevel;
    }
    return c;
}

int DocumentViewport::cornerIndexAt(const QVector<QPointF>& corners, const QPointF& viewportPos)
{
    int best = -1;
    qreal bestDist = HANDLE_HIT_SIZE * 0.75;
    for (int i = 0; i < corners.size(); ++i) {
        const qreal d = QLineF(corners[i], viewportPos).length();
        if (d <= bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

QVector<QPointF> DocumentViewport::lassoCornersInViewport() const
{
    QVector<QPointF> c;
    if (!m_editPoints || !lassoShapeCorners(&c)) {
        return {};
    }
    for (QPointF& p : c) {
        p = selectionToViewport(p);
    }
    return c;
}

DocumentViewport::HandleHit DocumentViewport::lassoCornerHandleAt(const QPointF& viewportPos) const
{
    const QVector<QPointF> c = lassoCornersInViewport();
    if (cornerIndexAt(c, viewportPos) >= 0) {
        return HandleHit::Corner;
    }
    return QPolygonF(c).boundingRect().contains(viewportPos) ? HandleHit::Inside : HandleHit::None;
}

bool DocumentViewport::canEditPoints() const
{
    if (m_lassoSelection.isValid()) {
        return lassoShapeCorners(nullptr);
    }
    return m_selectedObjects.size() == 1 && polygonShape(m_selectedObjects.first());
}

void DocumentViewport::setEditPoints(bool on)
{
    m_editPoints = on;
    update();
}

qreal DocumentViewport::cornerFraction() const
{
    if (m_lassoSelection.isValid()) {
        if (!lassoShapeCorners(nullptr)) {
            return -1.0;
        }
        const VectorStroke& s = m_lassoSelection.selectedStrokes.first();
        const QVector<QPointF> pts = positions(s);
        return fractionOf(ShapeCleaner::cornerRadius(pts, s.cornerJoints, shapeWidth(s)),
                          ShapeCleaner::jointCorners(pts, s.cornerJoints));
    }
    const ShapeObject* shape = roundableShape(m_selectedObjects);
    return shape ? fractionOf(shape->radius(), shape->corners()) : -1.0;
}

void DocumentViewport::beginCornerRadius()
{
    m_cornerBase.clear();
    m_cornerRadiusShown = -1.0;
    m_lineEndBefore = QJsonObject();
    if (!m_document) {
        return;
    }
    if (m_lassoSelection.isValid()) {
        if (lassoShapeCorners(nullptr)) {
            bakeLassoTransform();
            const VectorStroke& s = m_lassoSelection.selectedStrokes.first();
            const QVector<QPointF> pts = positions(s);
            m_cornerBase = ShapeCleaner::jointCorners(pts, s.cornerJoints);
            m_cornerRadius = ShapeCleaner::cornerRadius(pts, s.cornerJoints, shapeWidth(s));
            m_cornerStrokeBefore = s;   // put back when the slider ends where it began
            m_cornerBakedBefore = m_lassoSelection.baked;
        }
        return;
    }
    if (ShapeObject* shape = roundableShape(m_selectedObjects)) {
        m_lineEndBefore = shape->toJson();   // For the one undo step
    }
}

void DocumentViewport::previewCornerRadius(qreal fraction)
{
    const qreal f = qBound(0.0, fraction, 1.0);
    if (!m_cornerBase.isEmpty()) {
        if (m_lassoSelection.selectedStrokes.size() == 1) {
            m_cornerRadiusShown = f * f * largestRadius(m_cornerBase);
            rebuildLassoShape(m_cornerBase, m_cornerRadiusShown);
        }
        return;
    }
    ShapeObject* shape = roundableShape(m_selectedObjects);
    if (shape && !m_lineEndBefore.isEmpty()) {
        shape->setCornerRadius(f * f * largestRadius(shape->corners()));
        update();
    }
}

void DocumentViewport::commitCornerRadius()
{
    if (!m_cornerBase.isEmpty()) {
        const bool moved = m_cornerRadiusShown >= 0.0;
        const bool changed = moved && qAbs(m_cornerRadiusShown - m_cornerRadius) > 1e-6;
        m_cornerBase.clear();
        m_cornerRadiusShown = -1.0;
        if (changed && m_lassoSelection.isValid()) {
            commitLassoLine();   // One undo step; the shape stays selected
        } else if (moved && m_lassoSelection.selectedStrokes.size() == 1) {
            // Back where it began: no undo step, and no pending change either
            // (a preview marked the selection baked; undo would leave it floating).
            m_lassoSelection.selectedStrokes.first() = m_cornerStrokeBefore;
            m_lassoSelection.baked = m_cornerBakedBefore;
            m_lassoSelection.boundingBox = calculateSelectionBoundingBox();
            m_lassoSelection.transformOrigin = m_lassoSelection.boundingBox.center();
            invalidateSelectionCache();
            update();
        }
        return;
    }
    if (!m_lineEndBefore.isEmpty()) {
        finishConnectorEndDrag(roundableShape(m_selectedObjects));   // One ObjectStateChange, none if unchanged
        update();
    }
}

void DocumentViewport::bakeLassoTransform()
{
    const bool pending = m_lassoSelection.hasTransform();
    const QTransform t = buildSelectionTransform();
    for (VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
        for (StrokePoint& pt : stroke.points) {
            pt.pos = t.map(pt.pos);
        }
        stroke.updateBoundingBox();
    }
    m_lassoSelection.rotation = 0.0;
    m_lassoSelection.scaleX = m_lassoSelection.scaleY = 1.0;
    m_lassoSelection.offset = QPointF();
    m_lassoSelection.baked = pending;
    m_lassoSelection.boundingBox = calculateSelectionBoundingBox();
    m_lassoSelection.transformOrigin = m_lassoSelection.boundingBox.center();
    invalidateSelectionCache();
}

void DocumentViewport::rebuildLassoShape(const QVector<QPointF>& corners, qreal radius)
{
    VectorStroke& s = m_lassoSelection.selectedStrokes.first();
    if (corners.size() < 3 || s.points.isEmpty()) {
        return;
    }
    const StrokePoint model = s.points.first();   // Pressure and time of a cleaned stroke
    QVector<int> joints;
    const QVector<QPointF> pts = ShapeCleaner::polyline(corners, true, radius, shapeWidth(s), &joints);
    s.points.clear();
    s.points.reserve(pts.size());
    for (const QPointF& p : pts) {
        StrokePoint pt = model;
        pt.pos = p;
        s.points.append(pt);
    }
    s.cornerJoints = joints;
    s.updateBoundingBox();
    m_lassoSelection.boundingBox = s.boundingBox;
    m_lassoSelection.transformOrigin = s.boundingBox.center();
    m_lassoSelection.baked = true;
    invalidateSelectionCache();
    update();
}

void DocumentViewport::beginLassoCorner(const QPointF& viewportPos)
{
    bakeLassoTransform();
    m_cornerDrag = cornerIndexAt(lassoCornersInViewport(), viewportPos);
    m_cornerBase.clear();
    lassoShapeCorners(&m_cornerBase);
    const VectorStroke& s = m_lassoSelection.selectedStrokes.first();
    // The rounding it has, at its own radius, all through the drag.
    m_cornerRadius = qMax(0.0, ShapeCleaner::cornerRadius(positions(s), s.cornerJoints, shapeWidth(s)));
}

void DocumentViewport::moveLassoCorner(const QPointF& p)
{
    if (m_cornerDrag < 0 || m_cornerDrag >= m_cornerBase.size() || m_lassoSelection.selectedStrokes.size() != 1
        || collapses(m_cornerBase, m_cornerDrag, p)) {
        return;
    }
    QVector<QPointF> c = m_cornerBase;
    c[m_cornerDrag] = p;
    rebuildLassoShape(c, m_cornerRadius);
}

void DocumentViewport::dragShapeCorner(InsertedObject* obj, const QPointF& docPos)
{
    ShapeObject* shape = polygonShape(obj);
    int pageIndex = -1;
    Document::TileCoord tile{0, 0};
    if (!shape || !m_document || !locateObject(obj, pageIndex, tile)) {
        return;
    }
    const QPointF origin = m_document->isEdgeless()
        ? QPointF(tile.first, tile.second) * Document::EDGELESS_TILE_SIZE
        : pagePosition(pageIndex);
    QVector<QPointF> c = shape->corners();
    const QPointF p = docPos - origin;
    if (m_cornerDrag < 0 || m_cornerDrag >= c.size() || collapses(c, m_cornerDrag, p)) {
        return;
    }
    c[m_cornerDrag] = p;
    shape->setCorners(c);
    update();
}
