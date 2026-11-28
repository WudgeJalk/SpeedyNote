// DocumentViewport members kept out of DocumentViewport.cpp: a finger tap
// selects a line or a shape (whatever the tool), and the box the selection
// menu sits next to.

#include "DocumentViewport.h"
#include "DrawingSettings.h"
#include "../compat/qt_compat.h"
#include "../layers/VectorLayer.h"

#include <QTouchEvent>
#include <QTransform>

namespace {

/// What a finger tap may select: straight lines, arrows and cleaned closed
/// shapes. Handwriting never: a resting finger must not grab a word.
bool tapSelectable(const VectorStroke& s)
{
    if (s.isStraightLine() || s.arrowStart != StrokeStyle::None || s.arrowEnd != StrokeStyle::None) {
        return true;
    }
    // A cleaned shape is closed exactly and has one width all along.
    if (s.points.size() < 4 || QLineF(s.points.first().pos, s.points.last().pos).length() > 0.5) {
        return false;
    }
    for (const StrokePoint& pt : s.points) {
        if (pt.pressure != s.points.first().pressure) {
            return false;
        }
    }
    return true;
}

} // namespace

bool DocumentViewport::fingerTapEnded(QTouchEvent* event)
{
    const auto points = SN_TOUCH_POINTS(event);
    const DrawingSettings& settings = DrawingSettings::get();
    switch (event->type()) {
    case QEvent::TouchBegin:
        m_fingerTap = settings.fingerTapSelects && points.size() == 1 && m_document;
        if (m_fingerTap) {
            m_fingerTapPos = SN_TP_POS(points.first());
            m_fingerTapClock.start();
        }
        return false;
    case QEvent::TouchUpdate:
        if (m_fingerTap && (points.size() != 1
                            || QLineF(SN_TP_POS(points.first()), m_fingerTapPos).length() > settings.tapSlopPx)) {
            m_fingerTap = false;   // A drag or a second finger: the page pans or zooms
        }
        return false;
    case QEvent::TouchEnd: {
        const bool tap = m_fingerTap && m_fingerTapClock.elapsed() <= settings.tapMs;
        m_fingerTap = false;
        return tap;
    }
    default:
        m_fingerTap = false;
        return false;
    }
}

void DocumentViewport::selectByFingerTap(const QPointF& viewportPos)
{
    if (!m_document || m_pointerActive) {
        return;   // The pen is busy
    }
    const ToolType back = m_fingerTapSwitched ? m_toolBeforeFingerTap : m_currentTool;
    // Let go of the selection first (a moved one is written back), so the tap
    // sees the page as it is now.
    if (m_lassoSelection.isValid()) {
        if (m_lassoSelection.hasTransform()) {
            applySelectionTransform();
        } else {
            clearLassoSelection();
        }
    }
    if (!m_selectedObjects.isEmpty()) {
        deselectAllObjects();
    }

    InsertedObject* hit = objectAtPoint(viewportToDocument(viewportPos));
    if (hit && (hit->type() == QLatin1String("shape") || hit->type() == QLatin1String("connector"))) {
        switchForFingerTap(ToolType::Shape, back);
        selectObject(hit, false);
        update();
        return;
    }
    // A finger tap on a picture selects it, in any tool; then a finger moves it.
    // Not a picture fixed to the page: it is background, the picture tool frees it.
    if (hit && hit->type() == QLatin1String("image") && !hit->locked) {
        switchForFingerTap(ToolType::ObjectSelect, back);
        setObjectActionMode(ObjectActionMode::Select);
        selectObject(hit, false);
        update();
        return;
    }
    if (selectStrokeByTap(viewportPos, back)) {
        return;
    }
    if (m_fingerTapSwitched) {
        returnFromFingerTap();   // Empty paper
    }
    update();
}

bool DocumentViewport::selectStrokeByTap(const QPointF& viewportPos, ToolType back)
{
    const qreal reach = DrawingSettings::get().tapReachPx / qMax(m_zoomLevel, 0.01);
    const auto pick = [reach](const QVector<VectorStroke>& strokes, const QPointF& local) {
        for (int i = static_cast<int>(strokes.size()) - 1; i >= 0; --i) {   // The line on top first
            if (tapSelectable(strokes[i]) && strokes[i].containsPoint(local, reach)) {
                return i;
            }
        }
        return -1;
    };
    // Same selection as a lasso around that one line (finalizeLassoSelection).
    const auto select = [this](const VectorStroke& stroke, int index) {
        m_lassoSelection.selectedStrokes.append(stroke);
        m_lassoSelection.originalIndices.append(index);
        m_lassoSelection.boundingBox = calculateSelectionBoundingBox();
        m_lassoSelection.transformOrigin = m_lassoSelection.boundingBox.center();
        invalidateSelectionCache();
        m_selectionBackgroundSnapshot = QPixmap();
        emit lassoSelectionChanged(true);
        update();
    };

    if (m_document->isEdgeless()) {
        const QPointF doc = viewportToDocument(viewportPos);
        const qreal m = reach + EDGELESS_STROKE_MARGIN;
        for (const auto& coord : m_document->tilesInRect(QRectF(doc.x() - m, doc.y() - m, 2 * m, 2 * m))) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile || m_edgelessActiveLayerIndex >= tile->layerCount()) continue;
            VectorLayer* layer = tile->layer(m_edgelessActiveLayerIndex);
            if (!layer) continue;
            const QPointF origin(coord.first * Document::EDGELESS_TILE_SIZE,
                                 coord.second * Document::EDGELESS_TILE_SIZE);
            const int index = pick(layer->strokes(), doc - origin);
            if (index < 0) continue;
            VectorStroke stroke = layer->strokes()[index];   // Document coordinates while selected
            for (StrokePoint& pt : stroke.points) {
                pt.pos += origin;
            }
            stroke.updateBoundingBox();
            switchForFingerTap(ToolType::Lasso, back);
            m_lassoSelection.clear();
            m_lassoSelection.sourceLayerIndex = m_edgelessActiveLayerIndex;
            m_lassoSelection.sourceTileCoord = coord;
            select(stroke, index);
            return true;
        }
        return false;
    }

    const PageHit hit = viewportToPage(viewportPos);
    Page* page = hit.valid() ? m_document->page(hit.pageIndex) : nullptr;
    VectorLayer* layer = page ? page->activeLayer() : nullptr;
    const int index = layer ? pick(layer->strokes(), hit.pagePoint) : -1;
    if (index < 0) {
        return false;
    }
    switchForFingerTap(ToolType::Lasso, back);
    m_lassoSelection.clear();
    m_lassoSelection.sourcePageIndex = hit.pageIndex;
    m_lassoSelection.sourceLayerIndex = page->activeLayerIndex;
    select(layer->strokes()[index], index);
    return true;
}

void DocumentViewport::switchForFingerTap(ToolType tool, ToolType back)
{
    const bool straight = m_fingerTapSwitched ? m_straightBeforeFingerTap : m_straightLineMode;
    setCurrentTool(tool);   // Clears m_fingerTapSwitched when the tool changes
    m_toolBeforeFingerTap = back;
    m_straightBeforeFingerTap = straight;
    m_fingerTapSwitched = tool != back;
}

void DocumentViewport::returnFromFingerTap()
{
    const bool straight = m_straightBeforeFingerTap;
    setCurrentTool(m_toolBeforeFingerTap);   // Also lets go of the selection
    if (straight) {
        setStraightLineMode(true);   // The lasso turned it off
    }
}

bool DocumentViewport::pressOnSelection(const QPointF& viewportPos) const
{
    if (m_lassoSelection.isValid() && hitTestSelectionHandles(viewportPos) != HandleHit::None) {
        return true;
    }
    return !m_selectedObjects.isEmpty()
           && (objectHandleAtPoint(viewportPos) != HandleHit::None
               || m_selectedObjects.contains(objectAtPoint(viewportToDocument(viewportPos))));
}

QRect DocumentViewport::selectionMenuAnchor() const
{
    if (!m_document) {
        return QRect();
    }
    QPolygonF pts;   // Viewport points the menu must leave uncovered: the box and its handles
    if (m_lassoSelection.isValid()) {
        QPointF a, b;
        if (lassoLineEnds(&a, &b)) {
            pts << selectionToViewport(a) << selectionToViewport(b);
        } else {
            const QTransform t = buildSelectionTransform();
            for (const QPointF& p : getHandlePositions()) {
                pts << selectionToViewport(t.map(p));
            }
        }
    }
    for (InsertedObject* obj : m_selectedObjects) {
        QPointF a, b;
        if (connectorEnds(obj, &a, &b)) {
            pts << a << b;
            continue;
        }
        const QRectF box = objectBoundsInViewport(obj);
        if (box.isEmpty()) continue;
        QTransform turn;   // The handles turn with the object, the rotate handle above its top
        turn.translate(box.center().x(), box.center().y());
        turn.rotate(obj->rotation);
        turn.translate(-box.center().x(), -box.center().y());
        pts << turn.map(box.topLeft()) << turn.map(box.topRight())
            << turn.map(box.bottomLeft()) << turn.map(box.bottomRight())
            << turn.map(QPointF(box.center().x(), box.top() - ROTATE_HANDLE_OFFSET));
    }
    if (pts.isEmpty()) {
        return QRect();
    }
    const qreal pad = HANDLE_HIT_SIZE / 2.0;
    const QRectF r = pts.boundingRect().adjusted(-pad, -pad, pad, pad);
    // During a pan or pinch the page is a moved picture: follow it.
    return QRectF(gestureMap(r.topLeft()), gestureMap(r.bottomRight())).toAlignedRect();
}

void DocumentViewport::trackSelectionMenuAnchor()
{
    if (m_menuAnchor.isNull() && !m_lassoSelection.isValid() && m_selectedObjects.isEmpty()) {
        return;
    }
    const QRect anchor = selectionMenuAnchor();
    if (anchor != m_menuAnchor) {
        m_menuAnchor = anchor;
        emit selectionMenuAnchorChanged();
    }
}

// ---------------------------------------------------------------------------
// Add or remove space. The lines under the press become a lasso selection,
// so the drag is as smooth as a lasso move (one cached picture), and the
// lift is the lasso's own commit and undo step.
// ---------------------------------------------------------------------------

void DocumentViewport::armSpaceTool()
{
    m_spaceArmed = true;
    setCursor(Qt::SizeVerCursor);
}

bool DocumentViewport::spacePress(const PointerEvent& pe)
{
    if (!m_spaceArmed) return false;
    m_spaceArmed = false;
    if (m_lassoSelection.isValid()) {   // let go of what was selected
        if (m_lassoSelection.hasTransform()) applySelectionTransform();
        else clearLassoSelection();
    }
    QRectF below;
    m_spacePage = -1;
    m_spacePressY = pe.viewportPos.y();
    if (m_document->isEdgeless()) {
        // shortcut: only the columns in view move, down to the last tile that
        // exists; take every column if users ask.
        const QPointF at = viewportToDocument(pe.viewportPos);
        const QPointF left = viewportToDocument(QPointF(0, 0));
        const QPointF right = viewportToDocument(QPointF(width(), height()));
        qreal bottom = right.y();
        for (const Document::TileCoord& coord : m_document->allTileCoords()) {
            bottom = qMax(bottom, (coord.second + 1) * qreal(Document::EDGELESS_TILE_SIZE));
        }
        below = QRectF(left.x(), at.y(), right.x() - left.x(), bottom - at.y());
    } else if (pe.pageHit.valid() && m_document->page(pe.pageHit.pageIndex)) {
        const QSizeF size = m_document->page(pe.pageHit.pageIndex)->size;
        m_lassoSelection.sourcePageIndex = pe.pageHit.pageIndex;
        m_spacePage = pe.pageHit.pageIndex;
        // Wider and taller than the page: lines that hang over its edge count too.
        below = QRectF(-size.width(), pe.pageHit.pagePoint.y(), 3 * size.width(), 2 * size.height());
    }
    m_spaceBelow = below;
    if (below.isValid()) {
        m_lassoPath = QPolygonF(below);
        const QSignalBlocker quiet(this);   // no selection menu for this selection
        finalizeLassoSelection();
    }
    if (m_lassoSelection.isValid()) {
        startSelectionTransform(HandleHit::Inside, pe.viewportPos);
    }
    m_spaceDrag = below.isValid();   // also with pictures only under the press
    if (!m_spaceDrag) {
        updateHighlighterCursor();   // nothing under the press: the tool's cursor is back
    }
    return true;
}

bool DocumentViewport::spaceMove(const PointerEvent& pe)
{
    if (!m_spaceDrag) return false;
    if (m_isTransformingSelection) {
        updateSelectionTransform(QPointF(m_transformStartPos.x(), pe.viewportPos.y()));   // up and down only
    }
    return true;
}

bool DocumentViewport::spaceRelease(const PointerEvent& pe)
{
    if (!m_spaceDrag) return false;
    if (m_isTransformingSelection) finalizeSelectionTransform();
    m_spaceDrag = false;
    m_undoGroup = ++m_lastUndoGroup;   // the lines and the pictures: one undo step
    if (m_lassoSelection.hasTransform()) applySelectionTransform();   // commits and lets go
    else if (m_lassoSelection.isValid()) clearLassoSelection();

    // Pictures, text boxes and shapes that start under the press move too.
    // shortcut: they jump when the pen lifts (the lines move live); move them
    // in the lasso's picture if users ask.
    const qreal dy = (pe.viewportPos.y() - m_spacePressY) / m_zoomLevel;
    const bool edgeless = m_document->isEdgeless();
    const auto origin = [](Document::TileCoord t) {
        return QPointF(t.first, t.second) * qreal(Document::EDGELESS_TILE_SIZE);
    };
    struct Under { InsertedObject* obj; Document::TileCoord tile; };
    QVector<Under> under;
    if (!qFuzzyIsNull(dy) && edgeless) {
        for (const Document::TileCoord& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            for (int i = 0; tile && i < static_cast<int>(tile->objects.size()); ++i) {
                InsertedObject* obj = tile->objects[i].get();
                if (obj && !obj->locked && m_spaceBelow.contains(origin(coord) + obj->position)) under.append({obj, coord});
            }
        }
    } else if (Page* page = !qFuzzyIsNull(dy) && m_spacePage >= 0 ? m_document->page(m_spacePage) : nullptr) {
        for (const auto& owned : page->objects) {
            if (owned && !owned->locked && owned->position.y() >= m_spaceBelow.top()) under.append({owned.get(), {0, 0}});
        }
    }
    for (const Under& item : under) {
        const QPointF old = item.obj->position;
        Document::TileCoord to = item.tile;
        if (edgeless) {
            // A picture lives in the tile of its top left corner (as after a drag).
            const QPointF doc = origin(item.tile) + old + QPointF(0, dy);
            to = m_document->tileCoordForPoint(doc);
            if (to != item.tile) {
                Page* from = m_document->getTile(item.tile.first, item.tile.second);
                Page* dest = m_document->getOrCreateTile(to.first, to.second);
                std::unique_ptr<InsertedObject> taken = from && dest ? from->extractObject(item.obj->id) : nullptr;
                if (!taken) continue;
                taken->position = doc - origin(to);
                dest->addObject(std::move(taken));
            } else {
                item.obj->position = doc - origin(to);
            }
            m_document->markTileDirty(item.tile);
            m_document->markTileDirty(to);
        } else {
            item.obj->position.ry() += dy;
        }
        pushObjectMoveUndo(item.obj, old, m_spacePage, item.tile, to);
    }
    m_undoGroup = 0;
    if (!under.isEmpty()) {
        if (!edgeless) {
            m_document->markPageDirty(m_spacePage);
            emit pageModified(m_spacePage);
        }
        emit documentModified();
    }
    m_pointerActive = false;
    m_activeSource = PointerEvent::Unknown;
    m_activeDrawingPage = -1;
    updateHighlighterCursor();
    update();
    return true;
}
