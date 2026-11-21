#include "LaserPointer.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTransform>
#include <QtGlobal>

namespace {

// QRectF::united() skips 0 x 0 boxes, and a dot or a straight line has one.
void grow(QRectF& box, const QPointF& p)
{
    box.setLeft(qMin(box.left(), p.x()));
    box.setRight(qMax(box.right(), p.x()));
    box.setTop(qMin(box.top(), p.y()));
    box.setBottom(qMax(box.bottom(), p.y()));
}

} // namespace

LaserPointer::LaserPointer(QObject* parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &LaserPointer::onTimer);
}

bool LaserPointer::press(const QPointF& docPt, Mode mode)
{
    // A new contact brings the marks back to full strength; a dot or a change
    // of mode replaces what was there.
    const bool replace = mode == Point || mode != m_mode;
    const bool restyled = !m_lines.isEmpty() && (m_opacity < 1.0 || replace);
    m_timer.stop();
    m_fading = false;
    m_opacity = 1.0;
    m_mode = mode;
    if (replace) {
        m_lines.clear();
        m_lineBounds.clear();
    }
    m_lines.append(QPolygonF() << docPt);
    m_lineBounds.append(QRectF(docPt, docPt));
    return restyled;
}

bool LaserPointer::move(const QPointF& docPt, qreal minStep, QRectF* changed)
{
    if (m_lines.isEmpty()) return false;
    QPolygonF& line = m_lines.last();
    const QPointF last = line.last();
    const QPointF d = docPt - last;
    if (d.x() * d.x() + d.y() * d.y() < minStep * minStep) return false;

    QRectF step(last, last);
    grow(step, docPt);
    if (m_mode == Point) {
        line.last() = docPt;
        m_lineBounds.last() = QRectF(docPt, docPt);
    } else {
        line.append(docPt);
        grow(m_lineBounds.last(), docPt);
    }
    if (changed) *changed = step;
    return true;
}

void LaserPointer::release(int holdMs, int fadeMs)
{
    if (m_lines.isEmpty()) return;
    m_fadeMs = qMax(0, fadeMs);
    m_timer.setSingleShot(true);
    m_timer.start(m_mode == Point ? 0 : qMax(0, holdMs));
}

void LaserPointer::clear()
{
    m_timer.stop();
    m_fading = false;
    m_opacity = 1.0;
    m_lines.clear();
    m_lineBounds.clear();
}

int LaserPointer::pointCount() const
{
    int count = 0;
    for (const QPolygonF& line : m_lines) count += static_cast<int>(line.size());
    return count;
}

QRectF LaserPointer::bounds() const
{
    if (m_lineBounds.isEmpty()) return QRectF();
    QRectF box = m_lineBounds.first();
    for (const QRectF& b : m_lineBounds) {
        grow(box, b.topLeft());
        grow(box, b.bottomRight());
    }
    return box;
}

void LaserPointer::onTimer()
{
    if (!m_fading) {   // The wait is over: fade, about 30 frames a second
        m_fading = true;
        m_fadeClock.start();
        m_timer.setSingleShot(false);
        m_timer.start(33);
    }
    const QRectF area = bounds();
    const qreal t = m_fadeMs > 0 ? m_fadeClock.elapsed() / qreal(m_fadeMs) : 1.0;
    if (t >= 1.0) {
        clear();
    } else {
        m_opacity = 1.0 - t;
    }
    emit changed(area);
}

void LaserPointer::paint(QPainter& painter, const QTransform& docToView, const QRectF& viewClip,
                         const QColor& color) const
{
    if (m_lines.isEmpty()) return;

    // A soft wide glow, the line in the full colour, and a light centre so it
    // reads as light rather than ink.
    const qreal dot = m_mode == Point ? 1.5 : 1.0;
    QColor glow = color;
    glow.setAlpha(70);
    const QColor centre((color.red() + 3 * 255) / 4, (color.green() + 3 * 255) / 4,
                        (color.blue() + 3 * 255) / 4);
    const QPen glowPen(glow, GLOW_PX * dot, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    const QPen linePen(color, 5.0 * dot, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    const QPen centrePen(centre, 1.8 * dot, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    // Each pen event repaints a few pixels: stroke only the segments near
    // them, so a long trail costs a scan, not a full re-stroke. One path per
    // trail, so a loop through the area does not double the glow where it
    // crosses itself. The cut ends lie outside the area by more than a cap.
    const QRectF viewArea = viewClip.adjusted(-MARGIN_PX, -MARGIN_PX, MARGIN_PX, MARGIN_PX);
    const QRectF docArea = docToView.inverted().mapRect(viewArea);
    const auto reaches = [&docArea](const QPointF& a, const QPointF& b) {
        return qMax(a.x(), b.x()) >= docArea.left() && qMin(a.x(), b.x()) <= docArea.right()
            && qMax(a.y(), b.y()) >= docArea.top() && qMin(a.y(), b.y()) <= docArea.bottom();
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(m_opacity);
    for (int i = 0; i < m_lines.size(); ++i) {
        if (!docToView.mapRect(m_lineBounds[i]).adjusted(-MARGIN_PX, -MARGIN_PX, MARGIN_PX, MARGIN_PX)
                 .intersects(viewClip)) {
            continue;
        }
        const QPolygonF& line = m_lines[i];
        if (line.size() == 1) {   // A dot: a disc as wide as each pen
            const QPointF at = docToView.map(line.first());
            painter.setPen(Qt::NoPen);
            for (const QPen& pen : {glowPen, linePen, centrePen}) {
                const qreal r = pen.widthF() / 2;
                painter.setBrush(pen.color());
                painter.drawEllipse(at, r, r);
            }
            continue;
        }
        QPainterPath path;
        bool open = false;
        for (int k = 1; k < line.size(); ++k) {
            if (!reaches(line[k - 1], line[k])) {
                open = false;
                continue;
            }
            if (!open) {
                path.moveTo(docToView.map(line[k - 1]));
                open = true;
            }
            path.lineTo(docToView.map(line[k]));
        }
        if (path.isEmpty()) continue;
        painter.setBrush(Qt::NoBrush);
        for (const QPen& pen : {glowPen, linePen, centrePen}) {
            painter.strokePath(path, pen);
        }
    }
    painter.restore();
}
