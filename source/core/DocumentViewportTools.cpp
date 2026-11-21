// DocumentViewport members for the tools of the new toolbar, kept out of
// DocumentViewport.cpp: clear the page and auto deselect of the eraser, the
// laser pointer, and the ruler.

#include "DocumentViewport.h"
#include "DrawingSettings.h"
#include "LaserPointer.h"
#include "PenHaptics.h"
#include "../layers/VectorLayer.h"
#include "../ui/widgets/LengthSpinBox.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>

void DocumentViewport::clearCurrentPage()
{
    if (!m_document) return;

    // Paged: the active layer of the current page. Edgeless: every tile in view.
    const QRectF view = visibleRect();
    QVector<EraserTarget> targets = resolveEraserTargets(view, m_currentPageIndex);
    // resolveEraserTargets() also reaches tiles just outside the view.
    const qreal tileSize = Document::EDGELESS_TILE_SIZE;
    targets.erase(std::remove_if(targets.begin(), targets.end(), [&](const EraserTarget& t) {
        return t.edgeless && !QRectF(t.localToDoc, QSizeF(tileSize, tileSize)).intersects(view);
    }), targets.end());

    const DrawingSettings& drawing = DrawingSettings::get();
    m_eraserGestureSnapshots.clear();
    QVector<EraserTarget> cleared;
    for (const EraserTarget& target : targets) {
        VectorLayer* layer = target.container->layer(target.layerIndex);
        if (!layer || layer->locked || layer->isEmpty()) continue;

        snapshotLayerForEraserGesture(target.container, target.layerIndex,
                                      target.pageIndex, target.tile, target.edgeless);
        QVector<VectorStroke>& strokes = layer->strokes();
        QRectF removedBounds;
        bool removed = false;
        for (int i = static_cast<int>(strokes.size()) - 1; i >= 0; --i) {
            if (!drawing.eraserMayTouch(strokes[i])) continue;
            removedBounds = removed ? removedBounds.united(strokes[i].boundingBox)
                                    : strokes[i].boundingBox;
            removed = true;
            strokes.removeAt(i);
        }
        if (!removed) continue;
        layer->patchCacheAfterRemovals(removedBounds);
        cleared.append(target);
    }

    const QVector<EraserLayerSnapshot> snapshots = std::move(m_eraserGestureSnapshots);
    m_eraserGestureSnapshots.clear();
    if (cleared.isEmpty()) return;

    finishEraserCuts(cleared);
    // The partial eraser's undo record puts every line back in its draw order.
    const UndoAction undoAction = buildErasePartialUndo(snapshots);
    if (!undoAction.removedSegments.isEmpty()) {
        pushUndoAction(undoAction);
    }
    emit strokesChanged();
    emit documentModified();
    update();
}

void DocumentViewport::returnFromEraser()
{
    if (DrawingSettings::get().eraserAutoDeselect && m_toolBeforeEraser != ToolType::Eraser) {
        setCurrentTool(m_toolBeforeEraser);
    }
}

// ===== Laser pointer =====

void DocumentViewport::laserPress(const PointerEvent& pe)
{
    if (!m_laser) {
        m_laser = new LaserPointer(this);
        connect(m_laser, &LaserPointer::changed, this, &DocumentViewport::updateLaserArea);
    }
    const QRectF before = m_laser->bounds();
    const QPointF docPt = viewportToDocument(pe.viewportPos);
    const auto mode = static_cast<LaserPointer::Mode>(DrawingSettings::get().laserMode);
    if (m_laser->press(docPt, mode)) updateLaserArea(before);
    updateLaserArea(QRectF(docPt, docPt));
}

void DocumentViewport::laserMove(const PointerEvent& pe)
{
    if (!m_laser) return;
    // Points closer than 2 screen px add nothing a glow that wide can show.
    QRectF step;
    if (m_laser->move(viewportToDocument(pe.viewportPos), 2.0 / qMax(m_zoomLevel, 0.01), &step)) {
        updateLaserArea(step);
    }
}

void DocumentViewport::laserRelease()
{
    if (!m_laser) return;
    const DrawingSettings& settings = DrawingSettings::get();
    m_laser->release(settings.laserHoldMs, settings.laserFadeMs);
}

void DocumentViewport::clearLaser()
{
    if (!m_laser || m_laser->isEmpty()) return;
    const QRectF area = m_laser->bounds();
    m_laser->clear();
    updateLaserArea(area);
}

void DocumentViewport::renderLaser(QPainter& painter, const QRect& dirtyRect)
{
    if (!m_laser || m_laser->isEmpty()) return;
    const QTransform docToView(m_zoomLevel, 0, 0, m_zoomLevel,
                               -m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
    m_laser->paint(painter, docToView, dirtyRect, DrawingSettings::get().laserColor);
}

void DocumentViewport::updateLaserArea(const QRectF& docRect)
{
    const qreal m = LaserPointer::MARGIN_PX;
    const QRectF view = QRectF(documentToViewport(docRect.topLeft()),
                               documentToViewport(docRect.bottomRight())).normalized();
    update(view.adjusted(-m, -m, m, m).toAlignedRect());
}

// ===== Ruler =====

void DocumentViewport::setRulerVisible(bool visible)
{
    if (visible == m_rulerVisible) return;
    const QRectF before = m_ruler.viewBounds(documentToViewport(m_ruler.centre));
    m_rulerVisible = visible;
    m_rulerDrag = Ruler::None;
    m_rulerTurning = false;
    // First shown, or the view moved away from it: put it in the middle of the view.
    if (visible && (!m_rulerPlaced || !rect().contains(documentToViewport(m_ruler.centre).toPoint()))) {
        m_ruler.centre = viewportToDocument(QPointF(rect().center()));
        m_rulerPlaced = true;
    }
    updateRulerArea(before);
    emit rulerVisibleChanged(visible);
}

void DocumentViewport::setRulerAngle(int degrees)
{
    const QRectF before = m_ruler.viewBounds(documentToViewport(m_ruler.centre));
    m_ruler.angleDeg = Ruler::normalizedAngle(degrees);
    updateRulerArea(before);
}

void DocumentViewport::updateRulerArea(const QRectF& before)
{
    update(before.united(m_ruler.viewBounds(documentToViewport(m_ruler.centre))).toAlignedRect());
}

bool DocumentViewport::rulerPress(const PointerEvent& pe)
{
    if (!m_rulerVisible) return false;
    const Ruler::Hit hit = m_ruler.hitTest(pe.viewportPos - documentToViewport(m_ruler.centre));
    if (hit == Ruler::None) return false;
    m_rulerDrag = hit;
    m_rulerDragStart = pe.viewportPos;
    m_rulerDragLast = pe.viewportPos;
    m_rulerTurning = false;
    return true;
}

bool DocumentViewport::rulerMove(const PointerEvent& pe)
{
    if (m_rulerDrag == Ruler::None) return false;
    const QRectF before = m_ruler.viewBounds(documentToViewport(m_ruler.centre));
    if (m_rulerDrag == Ruler::Body) {
        m_ruler.centre += (pe.viewportPos - m_rulerDragLast) / m_zoomLevel;
    } else if (m_rulerDrag == Ruler::Handle) {
        // The ruler turns as much as the pointer turns around the centre. Near
        // the centre the direction means nothing, so the turn starts once the
        // pointer has left the handle.
        const QPointF v = pe.viewportPos - documentToViewport(m_ruler.centre);
        if (v.x() * v.x() + v.y() * v.y() >= Ruler::HANDLE_PX * Ruler::HANDLE_PX) {
            const qreal direction = qRadiansToDegrees(std::atan2(-v.y(), v.x()));
            if (!m_rulerTurning) {
                m_rulerTurning = true;
                m_rulerTurnFrom = direction;
                m_rulerAngleAtTurn = m_ruler.angleDeg;
            }
            m_ruler.angleDeg = Ruler::normalizedAngle(m_rulerAngleAtTurn + qRound(direction - m_rulerTurnFrom));
        }
    }
    m_rulerDragLast = pe.viewportPos;
    updateRulerArea(before);
    return true;
}

bool DocumentViewport::rulerRelease(const PointerEvent& pe)
{
    if (m_rulerDrag == Ruler::None) return false;
    const bool labelTap = m_rulerDrag == Ruler::Label
                          && QLineF(pe.viewportPos, m_rulerDragStart).length() <= 10;
    m_rulerDrag = Ruler::None;
    m_rulerTurning = false;
    m_rulerTwoFingers = false;
    m_pointerActive = false;
    m_activeSource = PointerEvent::Unknown;
    m_activeDrawingPage = -1;
    m_hardwareEraserActive = false;
    if (labelTap) {
        // After the event: the menu runs its own event loop.
        const QPoint at = pe.viewportPos.toPoint();
        QTimer::singleShot(0, this, [this, at]() { showRulerMenu(at); });
    }
    return true;
}

void DocumentViewport::rulerTwoFingers(const QPointF& a, const QPointF& b)
{
    const QRectF before = m_ruler.viewBounds(documentToViewport(m_ruler.centre));
    const QPointF middle = (a + b) / 2;
    const qreal direction = qRadiansToDegrees(std::atan2(-(b.y() - a.y()), b.x() - a.x()));
    if (!m_rulerTwoFingers) {
        m_rulerTwoFingers = true;
        m_rulerTurning = false;
        m_rulerTurnFrom = direction;
        m_rulerAngleAtTurn = m_ruler.angleDeg;
        m_rulerDragLast = middle;
        m_rulerDragStart = QPointF(-1e9, -1e9);   // Never a tap on the label
    }
    m_ruler.centre += (middle - m_rulerDragLast) / m_zoomLevel;
    // A half turn looks the same, so a wrap of the direction past 180 degrees is harmless.
    m_ruler.angleDeg = Ruler::normalizedAngle(m_rulerAngleAtTurn + qRound(direction - m_rulerTurnFrom));
    m_rulerDragLast = middle;
    updateRulerArea(before);
}

QPointF DocumentViewport::beginRulerStroke(const QPointF& viewportPos, const QPointF& strokeOrigin,
                                           const QPointF& firstPoint)
{
    m_rulerFollow = false;
    if (!m_rulerVisible) return firstPoint;
    const int side = m_ruler.edgeNear(viewportPos - documentToViewport(m_ruler.centre),
                                      DrawingSettings::get().rulerSnapPx);
    if (side == 0) return firstPoint;
    // The ink runs along the outside of the edge and touches it, like a pen against a ruler.
    const qreal out = Ruler::WIDTH_PX / 2 / m_zoomLevel + m_currentStroke.widthAt(1.0) / 2;
    m_rulerEdgeOrigin = m_ruler.centre + m_ruler.across() * (side * out) - strokeOrigin;
    m_rulerEdgeDir = m_ruler.along();
    m_rulerFollow = true;
    m_holdArmed = false;   // Already straight: draw and hold has nothing to clean
    PenHaptics::click(PenHaptics::Snap);   // The line follows the edge from here
    return rulerSnap(firstPoint);
}

QPointF DocumentViewport::gestureMap(const QPointF& p) const
{
    if (!m_gesture.isActive() || m_gesture.startZoom <= 0) return p;
    const QPointF pan = (m_gesture.targetPan - m_gesture.startPan) * -m_gesture.startZoom;
    if (m_gesture.activeType == ViewportGestureState::Pan) return p + pan;
    if (m_gesture.activeType != ViewportGestureState::Zoom) return p;
    const qreal s = m_gesture.targetZoom / m_gesture.startZoom;   // As the zoom fast path scales the frame
    const QPointF c = m_gesture.zoomCenter;
    return c - c * s + p * s + (m_gesture.initialCentroidSet ? pan * s : QPointF());
}

void DocumentViewport::renderRuler(QPainter& painter, const QRect& dirtyRect)
{
    if (!m_rulerVisible || m_grabbingFrame) return;
    const QPointF centre = gestureMap(documentToViewport(m_ruler.centre));
    if (!m_ruler.viewBounds(centre).intersects(dirtyRect)) return;
    m_ruler.paint(painter, centre, m_zoomLevel, DrawingSettings::get().rulerRadians);
}

void DocumentViewport::showRulerMenu(const QPoint& viewportPos)
{
    if (!m_rulerVisible) return;
    QMenu menu(this);
    QAction* angle = menu.addAction(tr("Set the angle..."));
    QAction* position = menu.addAction(tr("Set the position..."));
    QAction* hide = menu.addAction(tr("Hide"));
    menu.addSeparator();
    QAction* options = menu.addAction(tr("Options..."));
    QAction* chosen = menu.exec(mapToGlobal(viewportPos));
    if (chosen == angle) askRulerAngle();
    else if (chosen == position) askRulerPosition();
    else if (chosen == hide) setRulerVisible(false);
    else if (chosen == options) emit rulerOptionsRequested();
}

void DocumentViewport::askRulerAngle()
{
    // Set to the whole degree, in the unit the ruler shows.
    const bool radians = DrawingSettings::get().rulerRadians;
    bool ok = false;
    const double value = radians
        ? QInputDialog::getDouble(this, tr("Ruler angle"), tr("Angle in radians:"),
                                  qDegreesToRadians(double(m_ruler.angleDeg)),
                                  -qDegreesToRadians(360.0), qDegreesToRadians(360.0), 3, &ok)
        : QInputDialog::getDouble(this, tr("Ruler angle"), tr("Angle in degrees:"),
                                  m_ruler.angleDeg, -360, 360, 0, &ok);
    if (ok) setRulerAngle(qRound(radians ? qRadiansToDegrees(value) : value));
}

void DocumentViewport::askRulerPosition()
{
    if (!m_document) return;
    // The centre of the ruler, from the top-left corner of the current page
    // (edgeless: from the origin of the canvas).
    const QPointF origin = m_document->isEdgeless() ? QPointF() : pagePosition(m_currentPageIndex);
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Ruler position"));
    auto* form = new QFormLayout(&dialog);
    auto* x = new LengthSpinBox(&dialog);
    auto* y = new LengthSpinBox(&dialog);
    for (LengthSpinBox* box : {x, y}) box->setRange(-1000000, 1000000);
    x->setValue(qRound(m_ruler.centre.x() - origin.x()));
    y->setValue(qRound(m_ruler.centre.y() - origin.y()));
    form->addRow(tr("Centre, from the left edge of the page:"), x);
    form->addRow(tr("Centre, from the top edge of the page:"), y);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    const QRectF before = m_ruler.viewBounds(documentToViewport(m_ruler.centre));
    m_ruler.centre = origin + QPointF(x->value(), y->value());
    updateRulerArea(before);
}
