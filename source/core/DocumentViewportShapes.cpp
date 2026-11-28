// DocumentViewport members for cleaned strokes, kept out of DocumentViewport.cpp:
// draw and hold, the free shape mode and hand-drawn arrows. The geometry is
// in strokes/ShapeCleaner; this file only feeds it and commits the result.

#include "DocumentViewport.h"
#include "DrawingSettings.h"
#include "PenHaptics.h"
#include "../layers/VectorLayer.h"
#include "../strokes/ShapeCleaner.h"
#include "../objects/ConnectorObject.h"
#include "../objects/ShapeObject.h"

#include <QUuid>

#include <limits>
#include <QTransform>

namespace {
/// Hold and the free shape clean the lines of these tools.
bool cleansLines(ToolType tool)
{
    return tool == ToolType::Pen || tool == ToolType::Marker;
}

/// The head a hand-drawn arrow gets: the one drawn (None stays None), or the
/// one Settings > Drawing > Shapes asks for.
quint8 handHead(int drawn)
{
    const int choice = DrawingSettings::get().handArrowHead;
    return drawn == StrokeStyle::None ? StrokeStyle::None
         : choice == 1 ? StrokeStyle::Open
         : choice == 2 ? StrokeStyle::Filled
         : static_cast<quint8>(drawn);
}
} // namespace

void DocumentViewport::beginHold(const QPointF& viewportPos)
{
    m_strokeCleaned = false;
    m_cleanBase.clear();
    m_lastHoldViewport = viewportPos;
    m_rawStroke.points.clear();
    const DrawingSettings& settings = DrawingSettings::get();
    m_holdArmed = settings.drawAndHold && cleansLines(m_currentTool);
    if (!m_holdArmed) {
        return;
    }
    if (!m_holdTimer) {
        m_holdTimer = new QTimer(this);
        m_holdTimer->setSingleShot(true);
        connect(m_holdTimer, &QTimer::timeout, this, &DocumentViewport::onHoldTimeout);
    }
    m_holdAnchor = viewportPos;
    m_holdClock.start();
    m_holdAnchorMs = 0;
    m_holdTimer->start(settings.holdMs);
}

bool DocumentViewport::trackHold(const QPointF& viewportPos)
{
    if (m_strokeCleaned) {
        adjustCleanedStroke(viewportPos);
        return false;
    }
    m_lastHoldViewport = viewportPos;
    // O(1) per event: the timer is not restarted on each move, it re-arms itself.
    if (m_holdArmed && QLineF(viewportPos, m_holdAnchor).length() > DrawingSettings::get().holdStillPx) {
        m_holdAnchor = viewportPos;
        m_holdAnchorMs = m_holdClock.elapsed();
        if (!m_holdTimer->isActive()) {
            m_holdTimer->start(DrawingSettings::get().holdMs);
        }
    }
    return true;
}

// The pen is still down on a cleaned stroke: the far end stays, the pen end follows the pen.
void DocumentViewport::adjustCleanedStroke(const QPointF& viewportPos)
{
    if (m_cleanBase.isEmpty()) {
        return;
    }
    const QPointF pen = m_cleanHandle + (viewportPos - m_cleanViewport) / m_zoomLevel;
    const QLineF from(m_cleanAnchor, m_cleanHandle);
    const QLineF to(m_cleanAnchor, pen);
    if (from.length() < 1e-6 || to.length() < 1e-6) {
        return;
    }
    QTransform t;
    t.translate(m_cleanAnchor.x(), m_cleanAnchor.y());
    t.rotate(from.angle() - to.angle());   // QLineF angles are counter-clockwise, QTransform is clockwise
    t.scale(to.length() / from.length(), to.length() / from.length());
    t.translate(-m_cleanAnchor.x(), -m_cleanAnchor.y());
    for (int i = 0; i < m_cleanBase.size(); ++i) {
        m_currentStroke.points[i].pos = t.map(m_cleanBase[i].pos);
    }
    resetCurrentStrokeCache();
    update();
}

void DocumentViewport::onHoldTimeout()
{
    if (!m_holdArmed || !m_isDrawing || m_strokeCleaned || !cleansLines(m_currentTool)) {
        return;
    }
    const DrawingSettings& settings = DrawingSettings::get();
    const qint64 still = m_holdClock.elapsed() - m_holdAnchorMs;
    if (still < settings.holdMs) {
        m_holdTimer->start(static_cast<int>(settings.holdMs - still));
        return;
    }
    // A short line is a dot or the start of a letter: the pen is only resting.
    const qreal lengthPx = VectorStroke::pathLength(m_currentStroke.points, m_currentStroke.points.size())
                           * m_zoomLevel;
    // A head for the line before it is not cleaned: the lift makes the arrow.
    if (lengthPx < settings.holdMinLength || (settings.handArrows && makeHandArrow(false))
        || !cleanCurrentStroke(true)) {
        return;
    }
    resetCurrentStrokeCache();
    update();
}

bool DocumentViewport::cleanCurrentStroke(bool snap)
{
    const int n = static_cast<int>(m_currentStroke.points.size());
    if (!m_isDrawing || !m_document || n < 3) {
        return false;
    }
    QVector<QPointF> raw;
    raw.reserve(n);
    qreal pressure = 0.0;
    for (const StrokePoint& pt : m_currentStroke.points) {
        raw << pt.pos;
        pressure += pt.pressure;
    }
    pressure /= n;

    const DrawingSettings& settings = DrawingSettings::get();
    ShapeCleaner::Params params;
    params.width = m_currentStroke.widthAt(pressure);
    params.cornerAngleDeg = settings.cornerAngle;
    params.closePercent = settings.closePercent;
    params.closeMinDist = 12.0 / m_zoomLevel;   // screen px
    // The marker has no stroke preset, and its wide line already rounds a corner:
    // its shapes start sharp (the selection panel rounds them).
    params.rounded = m_currentTool == ToolType::Shape ? m_shapeStyle.rounded
                   : m_currentTool != ToolType::Marker && m_penStyle.rounded;
    params.roughPercent = settings.roughPercent;
    params.equalPercent = settings.equalPercent;
    params.angleSnapDeg = settings.angleSnap;
    params.trapezoidDeg = settings.trapezoidAngle;
    params.levelDeg = settings.levelAngle;
    // A line that draws its own head: the shaft is cleaned and gets the head.
    const QPointF penAt = raw.last();
    int tip = -1;
    const int arrow = cleansLines(m_currentTool) && settings.handArrows
        ? handHead(ShapeCleaner::splitOneStrokeArrow(raw, params.width, 1.0 / m_zoomLevel, &tip).arrow)
        : StrokeStyle::None;
    if (arrow != StrokeStyle::None) {
        raw.resize(tip + 1);
    }
    QVector<QPolygonF> targets;
    if (snap && settings.snapPx > 0) {
        params.snapDist = settings.snapPx / m_zoomLevel;
        targets = shapeSnapTargets(raw.first(), raw.last(), params.snapDist);
        params.snapTargets = &targets;
    }
    bool closed = false;
    QVector<int> joints;
    const QVector<QPointF> cleaned = ShapeCleaner::clean(raw, params, &closed, &joints);
    if (cleaned.size() < 2) {
        return false;
    }

    // One width along the whole line (the mean pressure); times spread evenly.
    const qint64 t0 = m_currentStroke.points.first().timestamp;
    const qint64 t1 = m_currentStroke.points.last().timestamp;
    QVector<StrokePoint> points(cleaned.size());
    for (int i = 0; i < cleaned.size(); ++i) {
        points[i].pos = cleaned[i];
        points[i].pressure = pressure;
        points[i].timestamp = t0 + (t1 - t0) * i / (cleaned.size() - 1);
    }
    // A pen or marker stroke was handwriting a moment ago: undo gives it back (keepRawInUndo).
    if (cleansLines(m_currentTool)) {
        m_rawStroke = m_currentStroke;
    }
    m_currentStroke.points = points;
    m_currentStroke.cornerJoints = joints;   // Its corners can be moved later (Edit points)
    if (arrow != StrokeStyle::None) {
        m_currentStroke.arrowEnd = static_cast<quint8>(arrow);
    }
    m_strokeCleaned = true;
    // For adjustCleanedStroke: a closed shape turns and scales about its
    // centre; an open line about its point farthest from the pen.
    m_cleanBase = points;
    m_cleanHandle = penAt;
    m_cleanViewport = m_lastHoldViewport;
    m_cleanAnchor = closed ? ShapeCleaner::centre(cleaned) : points.first().pos;
    for (const StrokePoint& pt : points) {
        if (!closed && QLineF(pt.pos, m_cleanHandle).length() > QLineF(m_cleanAnchor, m_cleanHandle).length()) {
            m_cleanAnchor = pt.pos;
        }
    }
    PenHaptics::click(PenHaptics::Snap);   // Held line, free shape, ends snapped onto a shape
    return true;
}

bool DocumentViewport::liftShapeStroke()
{
    if (m_holdTimer) {
        m_holdTimer->stop();
    }
    m_holdArmed = false;
    if ((m_freeShapeMode || m_currentTool == ToolType::Shape) && !m_strokeCleaned) {
        cleanCurrentStroke(true);
    }
    if (!cleansLines(m_currentTool) || !DrawingSettings::get().handArrows) {
        return false;
    }
    if (makeHandArrow()) {
        return true;
    }
    // A line that drew its own head and was not held: it keeps its ink and gets the head.
    if (!m_strokeCleaned && m_currentStroke.points.size() >= 3) {
        QVector<QPointF> pts;
        pts.reserve(m_currentStroke.points.size());
        for (const StrokePoint& pt : m_currentStroke.points) {
            pts << pt.pos;
        }
        int tip = -1;
        const int arrow = handHead(ShapeCleaner::splitOneStrokeArrow(pts, m_currentStroke.widthAt(1.0),
                                                                     1.0 / m_zoomLevel, &tip).arrow);
        if (arrow != StrokeStyle::None) {
            m_rawStroke = m_currentStroke;   // undo gives back the ink as drawn (keepRawInUndo)
            m_currentStroke.points.resize(tip + 1);
            m_currentStroke.arrowEnd = static_cast<quint8>(arrow);
            PenHaptics::click(PenHaptics::Snap);
        }
    }
    return false;
}

void DocumentViewport::rememberShaft(int pageIndex, Document::TileCoord tile, int layerIndex,
                                     const QString& id)
{
    m_lastShaft = ShaftRef();
    if (cleansLines(m_currentTool) && !id.isEmpty()) {
        m_lastShaft = {id, pageIndex, tile, layerIndex};
    }
}

bool DocumentViewport::makeHandArrow(bool apply)
{
    if (!DrawingSettings::get().handArrowTwoStrokes || m_lastShaft.id.isEmpty() || !m_document || m_currentStroke.points.size() < 2) {
        return false;
    }
    const bool edgeless = m_document->isEdgeless();
    const Document::TileCoord tile = m_lastShaft.tile;
    Page* container = edgeless ? m_document->getTile(tile.first, tile.second)
                    : m_activeDrawingPage == m_lastShaft.pageIndex ? m_document->page(m_lastShaft.pageIndex)
                    : nullptr;
    VectorLayer* layer = container ? container->layer(m_lastShaft.layerIndex) : nullptr;
    // Only right after the line: it must still be the last stroke of its layer.
    if (!layer || layer->isEmpty() || layer->strokes().last().id != m_lastShaft.id) {
        return false;
    }
    const VectorStroke shaft = layer->strokes().last();
    // A marker head does not finish a pen line, nor the other way round.
    if (shaft.isMarker() != m_currentStroke.isMarker()) {
        return false;
    }

    // The head is in page coordinates (edgeless: document); bring the shaft there.
    const QPointF origin = edgeless ? QPointF(tile.first, tile.second) * Document::EDGELESS_TILE_SIZE
                                    : QPointF();
    QVector<QPointF> shaftPts;
    QVector<QPointF> headPts;
    for (const StrokePoint& pt : shaft.points) {
        shaftPts << pt.pos + origin;
    }
    // The head as drawn, even if a free shape mode cleaned it.
    for (const StrokePoint& pt : (m_rawStroke.points.isEmpty() ? m_currentStroke : m_rawStroke).points) {
        headPts << pt.pos;
    }
    const ShapeCleaner::ArrowMatch match =
        ShapeCleaner::detectArrowHead(shaftPts, headPts, shaft.widthAt(1.0), 1.0 / m_zoomLevel);
    if (match.arrow == StrokeStyle::None || !apply) {
        return match.arrow != StrokeStyle::None;
    }

    // The head as drawn, in the shaft's coordinates, for undo.
    VectorStroke head = m_rawStroke.points.isEmpty() ? m_currentStroke : m_rawStroke;
    const QRectF tileRect(0, 0, Document::EDGELESS_TILE_SIZE, Document::EDGELESS_TILE_SIZE);
    for (StrokePoint& pt : head.points) {
        pt.pos -= origin;
        if (edgeless && !tileRect.contains(pt.pos)) {
            return false;   // shortcut: a head across a tile edge stays ink; split it per tile if users ask
        }
    }
    head.updateBoundingBox();

    // The line keeps its curve and gets the head. One undo entry gives back
    // exactly what was drawn: the line, then the head on top of it.
    VectorStroke arrow = shaft;
    arrow.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    (match.atEnd ? arrow.arrowEnd : arrow.arrowStart) = handHead(match.arrow);
    arrow.updateBoundingBox();

    UndoAction undo;
    undo.type = UndoAction::ErasePartial;
    undo.layerIndex = m_lastShaft.layerIndex;
    UndoAction::StrokeSegment seg;
    seg.pageIndex = m_lastShaft.pageIndex;
    seg.tileCoord = tile;
    seg.layerIndex = m_lastShaft.layerIndex;
    seg.zIndex = static_cast<int>(layer->strokes().size()) - 1;
    seg.stroke = arrow;
    undo.addedSegments.append(seg);
    seg.stroke = shaft;
    undo.removedSegments.append(seg);
    seg.stroke = head;
    seg.zIndex += 1;
    undo.removedSegments.append(seg);

    layer->replaceStroke(shaft.id, {arrow});
    pushUndoAction(undo);
    if (edgeless) {
        m_document->markTileDirty(tile);
        markOcrDirtyTiles(undo);
    } else {
        m_document->markPageDirty(m_lastShaft.pageIndex);
        m_ocrDirtyPages.insert(m_lastShaft.pageIndex);
        emit pageModified(m_lastShaft.pageIndex);
    }
    m_lastShaft.id = arrow.id;   // a head at the other end makes a double arrow
    PenHaptics::click(PenHaptics::Snap);

    // The head is now part of the arrow (and of the undo entry).
    m_rawStroke.points.clear();
    m_currentStroke = VectorStroke();
    m_isDrawing = false;
    m_lastRenderedPointIndex = 0;
    emit strokesChanged();
    emit documentModified();
    update();
    return true;
}

void DocumentViewport::keepRawInUndo()
{
    // The layer got the cleaned stroke. Make the top AddStroke add the stroke
    // as drawn, then record the swap to the cleaned one: the first undo gives
    // back the handwriting, the second removes it.
    if (m_rawStroke.points.isEmpty() || !m_document || m_undoStack.isEmpty()
        || m_undoStack.top().type != UndoAction::AddStroke || m_undoStack.top().segments.isEmpty()) {
        m_rawStroke.points.clear();
        return;
    }
    UndoAction add = m_undoStack.pop();
    UndoAction swap;
    swap.type = UndoAction::ErasePartial;
    swap.layerIndex = add.layerIndex;
    swap.addedSegments = add.segments;

    UndoAction::StrokeSegment seg = add.segments.first();
    add.segments.clear();
    seg.stroke = m_rawStroke;
    seg.stroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (m_document->isEdgeless()) {
        // Split per tile like finishStrokeEdgeless.
        for (const TileSegment& piece : splitStrokeIntoTileSegments(m_rawStroke.points)) {
            const QPointF origin = QPointF(piece.coord.first, piece.coord.second) * Document::EDGELESS_TILE_SIZE;
            UndoAction::StrokeSegment local = seg;
            local.tileCoord = piece.coord;
            local.stroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            local.stroke.points = piece.points;
            for (StrokePoint& pt : local.stroke.points) {
                pt.pos -= origin;
            }
            local.stroke.keepStyleAsSlice(m_rawStroke.points, piece.first, piece.points.size());
            local.stroke.updateBoundingBox();
            add.segments.append(local);
        }
    } else {
        seg.stroke.updateBoundingBox();
        add.segments.append(seg);
    }
    m_rawStroke.points.clear();

    // Either version takes the slots the cleaned pieces have now, the newest
    // of their page or tile, even when a hand-drawn head was put back above.
    swap.removedSegments = add.segments;
    const QVector<UndoAction::StrokeSegment> cleanedPieces = swap.addedSegments;
    const auto giveSlots = [this, &swap, &cleanedPieces](QVector<UndoAction::StrokeSegment>& pieces) {
        for (int i = 0; i < pieces.size(); ++i) {
            const Document::TileCoord coord = pieces[i].tileCoord;
            Page* c = m_document->isEdgeless() ? m_document->getTile(coord.first, coord.second)
                                               : m_document->page(pieces[i].pageIndex);
            const VectorLayer* layer = c ? c->layer(swap.layerIndex) : nullptr;
            if (!layer) {
                continue;   // A tile only the raw stroke reaches: on top (zIndex -1)
            }
            int slot = layer->strokeCount();
            for (const UndoAction::StrokeSegment& cleaned : cleanedPieces) {
                slot -= cleaned.tileCoord == coord ? 1 : 0;
            }
            for (int k = 0; k < i; ++k) {
                slot += pieces[k].tileCoord == coord ? 1 : 0;
            }
            pieces[i].zIndex = slot;
        }
    };
    giveSlots(swap.removedSegments);
    giveSlots(swap.addedSegments);
    m_undoStack.push(add);
    pushUndoAction(swap);
}

QVector<QPolygonF> DocumentViewport::shapeSnapTargets(const QPointF& a, const QPointF& b, qreal dist) const
{
    QVector<QPolygonF> targets;
    const QRectF nearA(a - QPointF(dist, dist), QSizeF(2 * dist, 2 * dist));
    const QRectF nearB(b - QPointF(dist, dist), QSizeF(2 * dist, 2 * dist));
    const auto collect = [&](const VectorLayer* layer, const QPointF& origin) {
        if (!layer) {
            return;
        }
        for (const VectorStroke& stroke : layer->strokes()) {
            const QRectF box = stroke.boundingBox.translated(origin);
            if (!box.intersects(nearA) && !box.intersects(nearB)) {
                continue;
            }
            QPolygonF line;
            line.reserve(stroke.points.size());
            for (const StrokePoint& pt : stroke.points) {
                line << pt.pos + origin;
            }
            targets << line;
        }
    };
    // The outlines of fixed shapes too, so a free shape can join them.
    const auto collectShapes = [&](const Page* container, const QPointF& origin) {
        for (const auto& owned : container->objects) {
            const auto* shape = dynamic_cast<const ShapeObject*>(owned.get());
            if (!shape) {
                continue;
            }
            const VectorStroke outline = shape->outlineStroke();
            const QRectF box = outline.boundingBox.translated(origin);
            if (!box.intersects(nearA) && !box.intersects(nearB)) {
                continue;
            }
            QPolygonF line;
            for (const StrokePoint& pt : outline.points) {
                line << pt.pos + origin;
            }
            targets << line;
        }
    };

    if (!m_document->isEdgeless()) {
        Page* page = m_document->page(m_activeDrawingPage);
        if (page) {
            collect(page->activeLayer(), QPointF());
            collectShapes(page, QPointF());
        }
        return targets;
    }
    // Edgeless: the tiles around both ends, each once.
    QVector<Document::TileCoord> seen;
    for (const QRectF& r : {nearA, nearB}) {
        for (const QPointF& corner : {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight()}) {
            const Document::TileCoord coord = m_document->tileCoordForPoint(corner);
            if (seen.contains(coord)) {
                continue;
            }
            seen << coord;
            if (Page* tile = m_document->getTile(coord.first, coord.second)) {
                const QPointF origin = QPointF(coord.first, coord.second) * Document::EDGELESS_TILE_SIZE;
                collect(tile->layer(m_edgelessActiveLayerIndex), origin);
                collectShapes(tile, origin);
            }
        }
    }
    return targets;
}

// ===== Shape tool: fixed shapes and connectors =====
// Kinds of the shape bar (ShapeSubToolbar::Kind), kept as plain numbers so
// the core does not depend on the UI.

namespace {
constexpr int kFreeShape = 0;
constexpr int kCurvedConnector = 2;
constexpr int kFirstFixedShape = 3;

bool isShapeOrConnector(const InsertedObject* obj)
{
    return obj && (obj->type() == QLatin1String("shape") || obj->type() == QLatin1String("connector"));
}

VectorStroke outlineOf(const InsertedObject* obj)
{
    if (const auto* shape = dynamic_cast<const ShapeObject*>(obj)) return shape->outlineStroke();
    if (const auto* line = dynamic_cast<const ConnectorObject*>(obj)) return line->outlineStroke();
    return VectorStroke();
}

} // namespace

bool DocumentViewport::shapePress(const PointerEvent& pe)
{
    // Handles, the selected shape, or another shape's line: move, resize or
    // rotate it with the object tool. In connector mode a press on a shape or
    // a connector starts a new connector there instead.
    const QPointF doc = viewportToDocument(pe.viewportPos);
    const HandleHit handle = m_selectedObjects.size() == 1 ? objectHandleAtPoint(pe.viewportPos) : HandleHit::None;
    const bool connectorMode = m_shapeKind > kFreeShape && m_shapeKind < kFirstFixedShape;
    InsertedObject* hit = objectAtPoint(doc);
    if (!isShapeOrConnector(hit)) {
        hit = nullptr;
    }
    const bool isLine = hit && hit->type() == QLatin1String("connector");
    // A connector starts where a shape's side handles sit: those only count for a connector.
    const bool lineSelected = m_selectedObjects.size() == 1
                              && m_selectedObjects.first()->type() == QLatin1String("connector");
    const bool edit = (handle != HandleHit::None && handle != HandleHit::Inside && (!connectorMode || lineSelected))
        || (hit && m_selectedObjects.contains(hit) && (!connectorMode || isLine))
        || (hit && !connectorMode && (isLine || nearShapeLine(hit, doc)));
    if (edit) {
        m_shapeDraft.editing = true;
        const ObjectActionMode mode = m_objectActionMode;
        m_objectActionMode = ObjectActionMode::Select;   // Never insert an image or link from here
        handlePointerPress_ObjectSelect(pe);
        m_objectActionMode = mode;
        return true;
    }
    if (!m_selectedObjects.isEmpty()) {
        deselectAllObjects();
    }
    if (m_shapeKind == kFreeShape) {
        return false;   // Drawn like the pen, cleaned when the pen lifts
    }
    if (m_document->isEdgeless()) {
        m_shapeDraft.start = doc;
        m_shapeDraft.pageIndex = -1;
    } else if (pe.pageHit.valid()) {
        m_shapeDraft.start = pe.pageHit.pagePoint;
        m_shapeDraft.pageIndex = pe.pageHit.pageIndex;
    } else {
        return true;
    }
    m_shapeDraft.end = m_shapeDraft.dirAnchor = m_shapeDraft.start;
    m_shapeDraft.firstDir = m_shapeDraft.lastDir = -1;
    m_shapeDraft.attached = 0;
    m_shapeDraft.preview = VectorStroke();
    m_shapeDraft.previewArea = QRectF();
    m_shapeDraft.active = true;
    m_pointerActive = true;
    return true;
}

bool DocumentViewport::shapeToolDrawsConnector() const
{
    return m_shapeKind > kFreeShape && m_shapeKind < kFirstFixedShape;
}

bool DocumentViewport::shapeMove(const PointerEvent& pe)
{
    if (m_shapeDraft.editing) {
        handlePointerMove_ObjectSelect(pe);
        return true;
    }
    if (!m_shapeDraft.active) {
        return false;
    }
    QPointF p = viewportToDocument(pe.viewportPos);
    if (!m_document->isEdgeless()) {
        p -= pagePosition(m_shapeDraft.pageIndex);
    }
    m_shapeDraft.end = p;
    // How the drag leaves the start and comes into the end. A rough L drawn
    // in connector mode gives a clean right-angle connector.
    const qreal step = 8.0 / m_zoomLevel;
    if (m_shapeDraft.firstDir < 0 && QLineF(m_shapeDraft.start, p).length() > step) {
        m_shapeDraft.firstDir = ConnectorObject::sideOf(p - m_shapeDraft.start);
    }
    if (QLineF(m_shapeDraft.dirAnchor, p).length() > step) {
        m_shapeDraft.lastDir = ConnectorObject::sideOf(p - m_shapeDraft.dirAnchor);
        m_shapeDraft.dirAnchor = p;
    }
    // Built (and attached to shapes) once per move, not on every paint, and
    // only the old and the new outline are repainted.
    const std::unique_ptr<InsertedObject> draft = shapeFromDraft();
    m_shapeDraft.preview = outlineOf(draft.get());
    if (const auto* line = dynamic_cast<const ConnectorObject*>(draft.get())) {
        const int attached = !line->startId.isEmpty() + !line->endId.isEmpty();
        if (attached > m_shapeDraft.attached) {
            PenHaptics::click(PenHaptics::Snap);   // An end just attached to a shape
        }
        m_shapeDraft.attached = attached;
    }
    const QRectF before = m_shapeDraft.previewArea;
    m_shapeDraft.previewArea = QRectF();
    if (!m_shapeDraft.preview.points.isEmpty()) {
        const QPointF origin = m_document->isEdgeless() ? QPointF() : pagePosition(m_shapeDraft.pageIndex);
        const QRectF box = m_shapeDraft.preview.boundingBox.translated(origin);
        m_shapeDraft.previewArea = QRectF(documentToViewport(box.topLeft()), documentToViewport(box.bottomRight()))
                                       .normalized().adjusted(-3, -3, 3, 3);
    }
    update(before.united(m_shapeDraft.previewArea).toAlignedRect());
    return true;
}

bool DocumentViewport::shapeRelease(const PointerEvent& pe)
{
    if (m_shapeDraft.editing) {
        m_shapeDraft.editing = false;
        handlePointerRelease_ObjectSelect(pe);
        return true;
    }
    if (!m_shapeDraft.active) {
        return false;
    }
    shapeMove(pe);
    m_shapeDraft.active = false;
    m_pointerActive = false;
    m_activeDrawingPage = -1;
    m_activeSource = PointerEvent::Unknown;
    m_hardwareEraserActive = false;
    insertShapeDraft();
    update();
    return true;
}

std::unique_ptr<InsertedObject> DocumentViewport::shapeFromDraft() const
{
    const ShapeDraft& d = m_shapeDraft;
    if (m_shapeKind >= kFirstFixedShape) {
        auto shape = std::make_unique<ShapeObject>();
        shape->kind = m_shapeKind - kFirstFixedShape;
        shape->setBoundingRect(QRectF(d.start, d.end).normalized());
        shape->color = m_shapeColor;
        shape->lineWidth = m_shapeThickness;
        shape->lineStyle = m_shapeStyle.lineStyle;
        shape->roundness = m_shapeStyle.rounded ? 1 : 0;
        return shape;
    }

    auto line = std::make_unique<ConnectorObject>();
    line->kind = m_shapeKind == kCurvedConnector ? ConnectorObject::Curved : ConnectorObject::Elbow;
    line->color = m_shapeColor;
    line->lineWidth = m_shapeThickness;
    line->lineStyle = m_shapeStyle.lineStyle;
    line->arrowStart = m_shapeStyle.arrowStart;
    line->arrowEnd = m_shapeStyle.arrowEnd;
    line->rounded = m_shapeStyle.rounded;
    QPointF s = d.start;
    QPointF e = d.end;
    line->startDir = d.firstDir >= 0 ? d.firstDir : ConnectorObject::sideOf(e - s);
    line->endDir = d.lastDir >= 0 ? (d.lastDir + 2) % 4 : (line->startDir + 2) % 4;

    // Ends attach to shapes of the same page (edgeless: of the start's tile).
    Page* container = nullptr;
    QPointF origin;
    if (m_document->isEdgeless()) {
        const Document::TileCoord coord = m_document->tileCoordForPoint(d.start);
        container = m_document->getTile(coord.first, coord.second);
        origin = QPointF(coord.first, coord.second) * Document::EDGELESS_TILE_SIZE;
    } else {
        container = m_document->page(d.pageIndex);
    }
    if (container) {
        const qreal reach = qMax(1, DrawingSettings::get().snapPx) / m_zoomLevel;
        line->attachEnd(true, *container, origin, reach, e, s);
        line->attachEnd(false, *container, origin, reach, s, e);
    }
    line->setEnds(s, e);
    return line;
}

void DocumentViewport::insertShapeDraft()
{
    const ShapeDraft& d = m_shapeDraft;
    if (!m_document || QLineF(d.start, d.end).length() * m_zoomLevel < 6.0) {
        return;   // A tap places nothing (it only cleared the selection)
    }
    std::unique_ptr<InsertedObject> obj = shapeFromDraft();
    InsertedObject* raw = obj.get();
    if (m_document->isEdgeless()) {
        const Document::TileCoord coord = m_document->tileCoordForPoint(d.start);
        Page* tile = m_document->getOrCreateTile(coord.first, coord.second);
        if (!tile) {
            return;
        }
        obj->position -= QPointF(coord.first, coord.second) * Document::EDGELESS_TILE_SIZE;
        const int affinity = m_edgelessActiveLayerIndex - 1;   // Tied to the active layer
        obj->setLayerAffinity(affinity);
        obj->zOrder = getNextZOrderForAffinity(tile, affinity);
        tile->addObject(std::move(obj));
        m_document->markTileDirty(coord);
        m_document->updateMaxObjectExtent(raw);
        pushObjectInsertUndo(raw, -1, coord);
    } else {
        Page* page = m_document->page(d.pageIndex);
        if (!page) {
            return;
        }
        const int affinity = page->activeLayerIndex - 1;
        obj->setLayerAffinity(affinity);
        obj->zOrder = getNextZOrderForAffinity(page, affinity);
        page->addObject(std::move(obj));
        m_document->markPageDirty(d.pageIndex);
        m_document->updateMaxObjectExtent(raw);
        pushObjectInsertUndo(raw, d.pageIndex, {});
    }
    // It stays selected, with its handles, as in GoodNotes.
    selectObject(raw, false);
    emit documentModified();
}

void DocumentViewport::renderShapePreview(QPainter& painter)
{
    if (!m_shapeDraft.active || !m_document) {
        return;
    }
    VectorStroke stroke = m_shapeDraft.preview;   // Built in shapeMove
    if (stroke.points.isEmpty()) {
        return;
    }
    const QPointF origin = m_document->isEdgeless() ? QPointF() : pagePosition(m_shapeDraft.pageIndex);
    for (StrokePoint& pt : stroke.points) {
        pt.pos = documentToViewport(pt.pos + origin);
    }
    stroke.baseThickness *= m_zoomLevel;
    stroke.updateBoundingBox();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    VectorLayer::renderStroke(painter, stroke);
    painter.restore();
}

bool DocumentViewport::nearShapeLine(const InsertedObject* obj, const QPointF& docPt) const
{
    // Inside a hollow shape the press draws; only its line picks it up.
    int pageIndex = -1;
    Document::TileCoord tile{0, 0};
    if (!locateObject(obj, pageIndex, tile)) {
        return false;
    }
    const QPointF local = docPt - (m_document->isEdgeless()
                                   ? QPointF(tile.first, tile.second) * Document::EDGELESS_TILE_SIZE
                                   : pagePosition(pageIndex));
    const VectorStroke outline = outlineOf(obj);
    const qreal reach = outline.baseThickness / 2 + 8.0 / m_zoomLevel;
    for (int i = 1; i < outline.points.size(); ++i) {
        const QPointF a = outline.points[i - 1].pos;
        const QPointF ab = outline.points[i].pos - a;
        const qreal len2 = QPointF::dotProduct(ab, ab);
        const qreal t = len2 > 0 ? qBound(0.0, QPointF::dotProduct(local - a, ab) / len2, 1.0) : 0.0;
        if (QLineF(a + ab * t, local).length() <= reach) {
            return true;
        }
    }
    return false;
}

void DocumentViewport::leaveShapeTool()
{
    m_shapeDraft = ShapeDraft();
    clearObjectSelection();
}

bool DocumentViewport::locateObject(const InsertedObject* obj, int& pageIndex, Document::TileCoord& tile) const
{
    if (m_document->isEdgeless()) {
        for (const Document::TileCoord& coord : m_document->allLoadedTileCoords()) {
            Page* t = m_document->getTile(coord.first, coord.second);
            if (t && t->objectById(obj->id) == obj) {
                tile = coord;
                return true;
            }
        }
        return false;
    }
    for (int i : m_document->loadedPageIndices()) {
        Page* page = m_document->page(i);
        if (page && page->objectById(obj->id) == obj) {
            pageIndex = i;
            return true;
        }
    }
    return false;
}

void DocumentViewport::restyleSelectedShape()
{
    if (m_currentTool != ToolType::Shape || m_selectedObjects.size() != 1 || !m_document) {
        return;
    }
    InsertedObject* obj = m_selectedObjects.first();
    UndoAction undo;
    undo.type = UndoAction::ObjectStateChange;
    if (!isShapeOrConnector(obj) || !locateObject(obj, undo.objectPageIndex, undo.objectTileCoord)) {
        return;
    }
    undo.objectId = obj->id;
    undo.objectData = obj->toJson();
    if (auto* shape = dynamic_cast<ShapeObject*>(obj)) {
        if (m_shapeKind >= kFirstFixedShape && shape->kind != m_shapeKind - kFirstFixedShape) {
            shape->kind = m_shapeKind - kFirstFixedShape;   // The way to change a shape's type
            shape->cornerPoints.clear();                    // Its own corners
        }
        shape->color = m_shapeColor;
        shape->lineWidth = m_shapeThickness;
        shape->lineStyle = m_shapeStyle.lineStyle;
        if ((shape->roundness > 0) != m_shapeStyle.rounded) {
            shape->roundness = m_shapeStyle.rounded ? 1 : 0;   // A rounding chosen in the panel stays
            shape->cornerRadius = -1.0;
        }
    } else if (auto* line = dynamic_cast<ConnectorObject*>(obj)) {
        if (m_shapeKind > kFreeShape && m_shapeKind < kFirstFixedShape) {
            line->kind = m_shapeKind == kCurvedConnector ? ConnectorObject::Curved : ConnectorObject::Elbow;
        }
        line->color = m_shapeColor;
        line->lineWidth = m_shapeThickness;
        line->lineStyle = m_shapeStyle.lineStyle;
        line->arrowStart = m_shapeStyle.arrowStart;
        line->arrowEnd = m_shapeStyle.arrowEnd;
        line->rounded = m_shapeStyle.rounded;
        line->setEnds(line->startPoint(), line->endPoint());   // Fit the box to the new route
    }
    undo.objectNewData = obj->toJson();
    if (undo.objectNewData == undo.objectData) {
        return;
    }
    pushUndoAction(undo);
    if (m_document->isEdgeless()) {
        m_document->markTileDirty(undo.objectTileCoord);
    } else {
        m_document->markPageDirty(undo.objectPageIndex);
    }
    emit documentModified();
    update();
}

void DocumentViewport::applyObjectStateChange(const UndoAction& action, bool undo)
{
    Page* container = m_document->isEdgeless()
        ? m_document->getTile(action.objectTileCoord.first, action.objectTileCoord.second)
        : m_document->page(action.objectPageIndex);
    if (InsertedObject* obj = container ? container->objectById(action.objectId) : nullptr) {
        obj->loadFromJson(undo ? action.objectData : action.objectNewData);
        if (m_document->isEdgeless()) {
            m_document->markTileDirty(action.objectTileCoord);
        } else {
            m_document->markPageDirty(action.objectPageIndex);
        }
    }
    (undo ? m_redoStack : m_undoStack).push(action);
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    update();
}
