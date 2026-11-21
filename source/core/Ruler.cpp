#include "Ruler.h"

#include "../ui/widgets/LengthSpinBox.h"

#include <QPainter>
#include <QPolygonF>
#include <QVector>
#include <QtMath>

namespace {
constexpr qreal TICK_MARGIN = 12.0;   ///< The 0 tick sits this far from the left end
const QColor kBody(250, 250, 245, 205);
const QColor kInk(40, 40, 40);
}

int Ruler::normalizedAngle(int degrees)
{
    int a = ((degrees % 180) + 180) % 180;   // 0..179
    return a > 90 ? a - 180 : a;
}

QPointF Ruler::along() const
{
    const qreal r = qDegreesToRadians(qreal(angleDeg));
    return QPointF(qCos(r), -qSin(r));
}

QPointF Ruler::across() const
{
    const qreal r = qDegreesToRadians(qreal(angleDeg));
    return QPointF(qSin(r), qCos(r));
}

Ruler::Hit Ruler::hitTest(const QPointF& offset) const
{
    const qreal s = QPointF::dotProduct(offset, along());
    const qreal t = QPointF::dotProduct(offset, across());
    if (s * s + t * t <= (HANDLE_PX + 4) * (HANDLE_PX + 4)) return Handle;
    if (s >= HANDLE_PX + 6 && s <= HANDLE_PX + 6 + LABEL_W && qAbs(t) <= LABEL_H / 2) return Label;
    if (qAbs(s) <= LENGTH_PX / 2 && qAbs(t) <= WIDTH_PX / 2) return Body;
    return None;
}

int Ruler::edgeNear(const QPointF& offset, qreal snapPx) const
{
    const qreal s = QPointF::dotProduct(offset, along());
    const qreal t = QPointF::dotProduct(offset, across());
    const qreal out = qAbs(t) - WIDTH_PX / 2;
    if (qAbs(s) > LENGTH_PX / 2 || out <= 0 || out > snapPx) return 0;
    return t > 0 ? 1 : -1;
}

QRectF Ruler::viewBounds(const QPointF& centreView) const
{
    const QPointF a = along() * (LENGTH_PX / 2);
    const QPointF b = across() * (WIDTH_PX / 2);
    QPolygonF box;
    box << centreView - a - b << centreView + a - b << centreView + a + b << centreView - a + b;
    return box.boundingRect().adjusted(-2, -2, 2, 2);
}

QString Ruler::angleText(bool radians) const
{
    if (radians) {
        return QStringLiteral("%1 rad").arg(qDegreesToRadians(qreal(angleDeg)), 0, 'f', 3);
    }
    return QStringLiteral("%1°").arg(angleDeg);
}

void Ruler::paint(QPainter& painter, const QPointF& centreView, qreal zoom, bool radians)
{
    const qreal dpr = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
    if (m_cache.isNull() || m_cacheZoom != zoom || m_cacheDpr != dpr
        || m_cacheMm != LengthUnit::useMm()) {
        rebuildCache(zoom, dpr);
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, angleDeg != 0);
    painter.translate(centreView);
    painter.rotate(-angleDeg);   // Qt turns clockwise; the angle turns the right end up
    painter.drawPixmap(QPointF(-LENGTH_PX / 2, -WIDTH_PX / 2), m_cache);

    // Rotate handle: a dark disc with a light, almost closed circle.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(40, 40, 40, 215));
    painter.drawEllipse(QPointF(0, 0), HANDLE_PX, HANDLE_PX);
    painter.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(QRectF(-7, -7, 14, 14), 60 * 16, 300 * 16);

    // The angle, written on the ruler. Tapping it opens the ruler's menu.
    const QRectF label(HANDLE_PX + 6, -LABEL_H / 2, LABEL_W, LABEL_H);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(40, 40, 40, 215));
    painter.drawRoundedRect(label, 6, 6);
    QFont font = painter.font();
    font.setPixelSize(13);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(label, Qt::AlignCenter, angleText(radians));
    painter.restore();
}

void Ruler::rebuildCache(qreal zoom, qreal dpr)
{
    m_cacheZoom = zoom;
    m_cacheDpr = dpr;
    m_cacheMm = LengthUnit::useMm();
    m_cache = QPixmap(qCeil(LENGTH_PX * dpr), qCeil(WIDTH_PX * dpr));
    m_cache.setDevicePixelRatio(dpr);
    m_cache.fill(Qt::transparent);

    QPainter p(&m_cache);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(90, 90, 90, 200), 1));
    p.setBrush(kBody);
    p.drawRoundedRect(QRectF(0.5, 0.5, LENGTH_PX - 1, WIDTH_PX - 1), 6, 6);

    // Screen px per shown unit (mm, or page px). Numbers at least 32 px apart,
    // on a 1, 2, 5 x 10^k step; ticks between them at least 3 px apart.
    const qreal unitPx = zoom / LengthUnit::factor();
    qreal labelStep = 0;
    for (qreal decade = 0.01; labelStep == 0 && decade < 1e7; decade *= 10) {
        for (qreal step : {1.0, 2.0, 5.0}) {
            if (step * decade * unitPx >= 32) {
                labelStep = step * decade;
                break;
            }
        }
    }
    if (labelStep == 0) labelStep = 1e7;
    int ticksPerLabel = 1;
    for (int div : {10, 5, 2}) {
        if (labelStep / div * unitPx >= 3) {
            ticksPerLabel = div;
            break;
        }
    }
    const qreal tickPx = labelStep / ticksPerLabel * unitPx;

    QVector<QLineF> ticks;
    QFont font = p.font();
    font.setPixelSize(10);
    p.setFont(font);
    p.setPen(kInk);
    for (int i = 0;; ++i) {
        const qreal x = TICK_MARGIN + i * tickPx;
        if (x > LENGTH_PX - TICK_MARGIN) break;
        const bool major = i % ticksPerLabel == 0;
        const bool half = ticksPerLabel == 10 && i % 5 == 0;
        const qreal len = major ? 16 : half ? 11 : 7;
        ticks << QLineF(x, 0, x, len) << QLineF(x, WIDTH_PX, x, WIDTH_PX - len);
        // No number under the handle and the angle label in the middle.
        const qreal fromCentre = x - LENGTH_PX / 2;
        if (major && (fromCentre < -HANDLE_PX - 20 || fromCentre > HANDLE_PX + 6 + LABEL_W + 20)) {
            const QString number = QString::number(i / ticksPerLabel * labelStep, 'g', 6);
            p.drawText(QRectF(x - 30, 17, 60, 12), Qt::AlignHCenter | Qt::AlignTop, number);
        }
    }
    p.drawLines(ticks);
    p.drawText(QRectF(TICK_MARGIN, WIDTH_PX - 30, 40, 12), Qt::AlignLeft | Qt::AlignVCenter,
               LengthUnit::name());
}
