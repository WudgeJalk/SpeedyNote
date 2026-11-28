// DocumentViewport members for the end handles of lines, kept out of
// DocumentViewport.cpp. A straight line selected with the lasso, or a selected
// connector, shows a round handle on each end instead of a box. Dragging one
// moves that end only; the other end stays. One undo step per drag. Also: the
// connectors attached to a shape follow it while it is dragged or resized.

#include "DocumentViewport.h"
#include "DrawingSettings.h"
#include "PenHaptics.h"
#include "../compat/qt_compat.h"
#include "../objects/ConnectorObject.h"

#include <QDateTime>
#include <QPainter>
#include <QTouchEvent>
#include <QTransform>

namespace {
constexpr qreal kEndHandleRadius = 7.0;   ///< Drawn radius, viewport pixels
}

bool DocumentViewport::lassoLineEnds(QPointF* a, QPointF* b) const
{
    if (m_lassoSelection.selectedStrokes.size() != 1
        || !m_lassoSelection.selectedStrokes.first().isStraightLine()) {
        return false;
    }
    const VectorStroke& line = m_lassoSelection.selectedStrokes.first();
    const QTransform t = buildSelectionTransform();
    if (a) *a = t.map(line.points.first().pos);
    if (b) *b = t.map(line.points.last().pos);
    return true;
}

QPointF DocumentViewport::selectionToViewport(const QPointF& p) const
{
    return documentToViewport(m_document->isEdgeless() ? p : p + pagePosition(m_lassoSelection.sourcePageIndex));
}

DocumentViewport::HandleHit DocumentViewport::lineHandleAt(const QPointF& viewportPos, const QPointF& a,
                                                           const QPointF& b) const
{
    // The ends first, the nearer one when both are in reach (a short line).
    const qreal da = QLineF(viewportPos, a).length();
    const qreal db = QLineF(viewportPos, b).length();
    if (qMin(da, db) <= HANDLE_HIT_SIZE * 0.75) {
        return da <= db ? HandleHit::LineStart : HandleHit::LineEnd;
    }
    const QPointF ab = b - a;
    const qreal len2 = QPointF::dotProduct(ab, ab);
    const qreal t = len2 > 0 ? qBound(0.0, QPointF::dotProduct(viewportPos - a, ab) / len2, 1.0) : 0.0;
    return QLineF(viewportPos, a + ab * t).length() <= HANDLE_HIT_SIZE / 2.0 ? HandleHit::Inside : HandleHit::None;
}

void DocumentViewport::drawLineEndHandles(QPainter& painter, const QPointF& a, const QPointF& b)
{
    drawRoundHandles(painter, {a, b});
}

void DocumentViewport::drawRoundHandles(QPainter& painter, const QVector<QPointF>& at)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0, 120, 215), 2));
    painter.setBrush(Qt::white);
    for (const QPointF& p : at) {
        painter.drawEllipse(p, kEndHandleRadius, kEndHandleRadius);
    }
    painter.restore();
}

void DocumentViewport::beginLassoLineEnd()
{
    // A line keeps no box: bake the pending turn and scale into its points.
    bakeLassoTransform();
    const VectorStroke& line = m_lassoSelection.selectedStrokes.first();
    m_lineEndBase = line.points;
    const bool start = m_transformHandle == HandleHit::LineStart;
    m_lineEndFixed = (start ? line.points.last() : line.points.first()).pos;
    m_lineEndMoving = (start ? line.points.first() : line.points.last()).pos;
}

void DocumentViewport::moveLassoLineEnd(const QPointF& p)
{
    // Turn and stretch the line about its fixed end, so the dragged end lands on p.
    const QLineF from(m_lineEndFixed, m_lineEndMoving);
    const QLineF to(m_lineEndFixed, p);
    if (from.length() < 1e-6 || to.length() < 1.0 || m_lassoSelection.selectedStrokes.size() != 1) {
        return;
    }
    QTransform t;
    t.translate(m_lineEndFixed.x(), m_lineEndFixed.y());
    t.rotate(from.angle() - to.angle());   // QLineF angles are counter-clockwise, QTransform is clockwise
    t.scale(to.length() / from.length(), to.length() / from.length());
    t.translate(-m_lineEndFixed.x(), -m_lineEndFixed.y());
    VectorStroke& line = m_lassoSelection.selectedStrokes.first();
    for (int i = 0; i < line.points.size() && i < m_lineEndBase.size(); ++i) {
        line.points[i].pos = t.map(m_lineEndBase[i].pos);
    }
    line.updateBoundingBox();
    m_lassoSelection.boundingBox = line.boundingBox;
    m_lassoSelection.transformOrigin = line.boundingBox.center();
    m_lassoSelection.baked = true;
    invalidateSelectionCache();
    update();
}

void DocumentViewport::commitLassoLine()
{
    if (!m_lassoSelection.hasTransform() || !m_document) {
        update();   // A tap on an end: nothing to write
        return;
    }
    const QString oldId = m_lassoSelection.selectedStrokes.first().id;
    const int layerIndex = m_lassoSelection.sourceLayerIndex;
    const bool editPoints = m_editPoints;
    applySelectionTransform();   // One undo step; it also clears the selection
    if (m_undoStack.isEmpty()) {
        return;
    }
    const UndoAction& top = m_undoStack.top();
    if (top.type != UndoAction::TransformSelection || top.addedSegments.size() != 1
        || top.removedSegments.isEmpty() || top.removedSegments.first().stroke.id != oldId) {
        return;   // Split across tiles: the selection ends here
    }
    // Keep the line selected, with its handles, for the next drag.
    const UndoAction::StrokeSegment& seg = top.addedSegments.first();
    VectorStroke line = seg.stroke;
    if (m_document->isEdgeless()) {
        const QPointF origin = QPointF(seg.tileCoord.first, seg.tileCoord.second) * Document::EDGELESS_TILE_SIZE;
        for (StrokePoint& pt : line.points) {
            pt.pos += origin;
        }
        line.updateBoundingBox();
        m_lassoSelection.sourceTileCoord = seg.tileCoord;
    } else {
        m_lassoSelection.sourcePageIndex = seg.pageIndex;
    }
    m_lassoSelection.sourceLayerIndex = layerIndex;
    m_lassoSelection.selectedStrokes = {line};
    m_lassoSelection.boundingBox = line.boundingBox;
    m_lassoSelection.transformOrigin = line.boundingBox.center();
    m_editPoints = editPoints;   // A shape whose corners are being edited keeps its handles
    invalidateSelectionCache();
    emit lassoSelectionChanged(true);
    update();
}

bool DocumentViewport::connectorEnds(const InsertedObject* obj, QPointF* a, QPointF* b) const
{
    const auto* line = dynamic_cast<const ConnectorObject*>(obj);
    if (!line) {
        return false;
    }
    const QRectF box = objectBoundsInViewport(const_cast<InsertedObject*>(obj));
    *a = box.topLeft() + (line->startPoint() - obj->position) * m_zoomLevel;
    *b = box.topLeft() + (line->endPoint() - obj->position) * m_zoomLevel;
    return true;
}

void DocumentViewport::dragConnectorEnd(InsertedObject* obj, const QPointF& docPos)
{
    auto* line = dynamic_cast<ConnectorObject*>(obj);
    int pageIndex = -1;
    Document::TileCoord tile{0, 0};
    if (!line || !m_document || !locateObject(obj, pageIndex, tile)) {
        return;
    }
    const bool edgeless = m_document->isEdgeless();
    Page* container = edgeless ? m_document->getTile(tile.first, tile.second) : m_document->page(pageIndex);
    if (!container) {
        return;
    }
    const QPointF origin = edgeless ? QPointF(tile.first, tile.second) * Document::EDGELESS_TILE_SIZE
                                    : pagePosition(pageIndex);
    const bool start = m_objectResizeHandle == HandleHit::LineStart;
    const QPointF other = start ? line->endPoint() : line->startPoint();
    QPointF p = docPos - origin;
    const qreal reach = qMax(1, DrawingSettings::get().snapPx) / m_zoomLevel;
    const bool wasFree = (start ? line->startId : line->endId).isEmpty();
    line->attachEnd(start, *container, QPointF(), reach, other, p);
    if (wasFree && !(start ? line->startId : line->endId).isEmpty()) {
        PenHaptics::click(PenHaptics::Snap);   // The end just attached to a shape
    }
    if ((start ? line->startId : line->endId).isEmpty()) {
        (start ? line->startDir : line->endDir) = ConnectorObject::sideOf(other - p);   // A free end leaves toward the other
    }
    line->setEnds(start ? p : other, start ? other : p);
}

void DocumentViewport::finishConnectorEndDrag(InsertedObject* obj)
{
    UndoAction undo;
    undo.type = UndoAction::ObjectStateChange;
    undo.objectData = m_lineEndBefore;
    m_lineEndBefore = QJsonObject();
    if (!obj || !m_document || !locateObject(obj, undo.objectPageIndex, undo.objectTileCoord)) {
        return;
    }
    undo.objectId = obj->id;
    undo.objectNewData = obj->toJson();
    if (undo.objectNewData == undo.objectData) {
        return;
    }
    pushUndoAction(undo);
    if (m_document->isEdgeless()) {
        m_document->markTileDirty(undo.objectTileCoord);
    } else {
        m_document->markPageDirty(undo.objectPageIndex);
        emit pageModified(undo.objectPageIndex);
    }
    emit documentModified();
}

bool DocumentViewport::fingerOnHandle(QTouchEvent* event)
{
    const auto points = SN_TOUCH_POINTS(event);
    if (event->type() == QEvent::TouchBegin) {
        m_fingerOnHandle = false;
        m_rulerTwoFingers = false;
        // The ruler lies on top of everything: a finger on it moves or turns it.
        if (!points.isEmpty() && m_rulerVisible && m_document
            && m_ruler.hitTest(SN_TP_POS(points.first()) - documentToViewport(m_ruler.centre)) != Ruler::None) {
            m_fingerOnHandle = true;
        } else if (points.size() == 1 && m_document) {
            const QPointF pos = SN_TP_POS(points.first());
            if (m_currentTool == ToolType::Lasso && m_lassoSelection.isValid()) {
                // Inside a box a finger still pans the page; on a line it moves the line.
                const HandleHit hit = hitTestSelectionHandles(pos);
                m_fingerOnHandle = hit != HandleHit::None
                                   && (hit != HandleHit::Inside || lassoLineEnds(nullptr, nullptr));
            } else if ((m_currentTool == ToolType::ObjectSelect || m_currentTool == ToolType::Shape)
                       && m_selectedObjects.size() == 1) {
                // A finger on a handle resizes; on the body of the selected
                // object it moves it (the user, 2026-11-23), unless the object
                // is fixed to the page. Anywhere else it pans the page.
                InsertedObject* selected = m_selectedObjects.first();
                m_fingerOnHandle = objectHandleAtPoint(pos) != HandleHit::None
                    || (selected && !selected->locked
                        // Only where the tool's press moves the object: the picture tool
                        // in Select mode; the shape tool unless it would start a connector.
                        && (m_currentTool == ToolType::ObjectSelect
                                ? m_objectActionMode == ObjectActionMode::Select
                                : (selected->type() == QLatin1String("connector")
                                   || !shapeToolDrawsConnector()))
                        && objectAtPoint(viewportToDocument(pos)) == selected);
            }
        }
    }
    if (!m_fingerOnHandle) {
        return false;
    }
    if (m_rulerDrag != Ruler::None && event->type() == QEvent::TouchUpdate) {
        QVector<QPointF> down;   // Fingers still on the glass
        for (const auto& point : points) {
            if (point.state() != SN_TP_RELEASED) down << SN_TP_POS(point);
        }
        if (down.size() >= 2) {
            rulerTwoFingers(down[0], down[1]);
            event->accept();
            return true;
        }
        if (m_rulerTwoFingers) {
            // Back to one finger: it drags the ruler from where it is.
            m_rulerTwoFingers = false;
            m_rulerDrag = Ruler::Body;
            if (!down.isEmpty()) m_rulerDragLast = down.first();
            event->accept();
            return true;
        }
    }
    PointerEvent pe;
    pe.source = PointerEvent::Touch;
    pe.viewportPos = points.isEmpty() ? m_lastPointerPos : SN_TP_POS(points.first());
    pe.pageHit = viewportToPage(pe.viewportPos);
    pe.button = Qt::LeftButton;
    pe.buttons = Qt::LeftButton;
    pe.timestamp = QDateTime::currentMSecsSinceEpoch();
    if (event->type() == QEvent::TouchBegin) {
        pe.type = PointerEvent::Press;
    } else if (event->type() == QEvent::TouchUpdate) {
        pe.type = PointerEvent::Move;
    } else {
        pe.type = PointerEvent::Release;
        pe.buttons = Qt::NoButton;
        m_fingerOnHandle = false;
    }
    handlePointerEvent(pe);
    event->accept();
    return true;
}

void DocumentViewport::collectDragFollowers()
{
    m_dragFollowerIds.clear();
    m_dragFollowerPages.clear();
    if (!m_document) {
        return;
    }
    const bool edgeless = m_document->isEdgeless();
    for (InsertedObject* obj : m_selectedObjects) {
        int pageIndex = -1;
        Document::TileCoord tile{0, 0};
        if (!obj || obj->type() != QLatin1String("shape") || !locateObject(obj, pageIndex, tile)) {
            continue;
        }
        Page* container = edgeless ? m_document->getTile(tile.first, tile.second) : m_document->page(pageIndex);
        if (!container || !container->hasConnectors) {
            continue;
        }
        bool any = false;
        for (const auto& owned : container->objects) {
            const auto* line = dynamic_cast<const ConnectorObject*>(owned.get());
            if (line && (line->startId == obj->id || line->endId == obj->id)
                && !m_selectedObjects.contains(owned.get())) {
                m_dragFollowerIds.insert(line->id);
                any = true;
            }
        }
        const QPointF origin = edgeless ? QPointF(tile.first, tile.second) * Document::EDGELESS_TILE_SIZE
                                        : pagePosition(pageIndex);
        if (any && !m_dragFollowerPages.contains(qMakePair(container, origin))) {
            m_dragFollowerPages.append(qMakePair(container, origin));
        }
    }
}

void DocumentViewport::renderDragFollowers(QPainter& painter)
{
    for (const auto& entry : m_dragFollowerPages) {
        ConnectorObject::resolve(*entry.first);   // The ends move with the dragged shape
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(documentToViewport(entry.second));
        painter.scale(m_zoomLevel, m_zoomLevel);
        for (const auto& owned : entry.first->objects) {
            if (m_dragFollowerIds.contains(owned->id)) {
                owned->render(painter, 1.0);
            }
        }
        painter.restore();
    }
}
